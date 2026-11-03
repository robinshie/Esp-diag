#pragma once
#include <functional>
#include "command_transport.h"
#include "terminal_capture.h"
#include "../config/console_settings.h"
#include "../adapters/device_adapter.h"

namespace switchtransport {
// Nonblocking byte port. write() may accept only part of the requested bytes.
// The hardware adapter and the session are both owned by the application loop.
class IConsolePort {
public:
  virtual ~IConsolePort()=default;
  virtual size_t read(char* bytes,size_t capacity)=0;
  virtual size_t write(const char* bytes,size_t size)=0;
};

class ConsoleSession : public ICommandTransport {
  enum class Phase {Stopped,Retry,Login,Setup,Idle,Probe,Sync,Read,Action};
  const switchdevice::IDeviceAdapter& adapter;
  IConsolePort& port;
  ConsoleSettings profile;
  Phase phase=Phase::Stopped;
  std::string state="disabled",prompt,wire;
  switchcli::TerminalCapture capture,transcript;
  switchcli::Result reply,userReply;
  std::vector<std::string> steps,expected,setup;
  size_t step=0,setupIndex=0;
  uint64_t deadline=0,retryAt=0,lastActivity=0;
  bool usernameSent=false,passwordSent=false;

  static std::string lower(std::string text){for(auto& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
  static bool ends(const std::string& text,const char* suffix){const std::string s=suffix;return text.size()>=s.size()&&text.compare(text.size()-s.size(),s.size(),s)==0;}
  static bool usernamePrompt(const std::string& tail){return ends(tail,"username:")||ends(tail,"login:")||ends(tail,"user name:");}
  static bool passwordPrompt(const std::string& tail){return ends(tail,"password:");}
  std::string redacted(std::string text)const{
    if(!profile.password.empty())for(size_t at=0;(at=text.find(profile.password,at))!=std::string::npos;at+=10)text.replace(at,profile.password.size(),"[REDACTED]");
    return text;
  }
  void status(const std::string& value){if(state!=value){state=value;if(log)log("[CONSOLE] state="+state);}}
  void send(const std::string& command,Phase next,uint64_t now,uint32_t timeout){
    capture=switchcli::TerminalCapture();wire=command+"\r";phase=next;deadline=now+timeout;
  }
  void appendCapture(){
    transcript.append(capture.output.data(),capture.output.size());
    transcript.truncated=transcript.truncated||capture.truncated;
    capture=switchcli::TerminalCapture();
  }
  void publish(const std::string& resultState){
    reply.state=resultState;reply.output=redacted(transcript.output);reply.truncated=transcript.truncated;
    if(!log)return;
    log("[CONSOLE][COMMAND] request_id="+reply.id+" command="+reply.command+" target="+reply.target+" state="+reply.state);
    for(size_t at=0;at<reply.output.size();){
      auto end=reply.output.find('\n',at);if(end==std::string::npos)end=reply.output.size();
      for(size_t offset=at;offset<end;offset+=800)log("[CONSOLE][OUTPUT] request_id="+reply.id+" command="+reply.command+" target="+reply.target+" text="+reply.output.substr(offset,std::min<size_t>(800,end-offset)));
      at=end+1;
    }
  }
  void stop(const std::string& reason,uint64_t now,bool retry=true){
    if(reply.state=="queued"||reply.state=="running"){
      if(phase==Phase::Read||phase==Phase::Action)appendCapture();
      publish(reason=="command_error"?reason:"timeout_or_disconnected");
    }
    status(reason);wire.clear();capture=switchcli::TerminalCapture();
    phase=retry?Phase::Retry:Phase::Stopped;retryAt=now+profile.retryMs;
  }
  void startLogin(uint64_t now){
    usernameSent=passwordSent=false;prompt.clear();
    status("connecting");send("",Phase::Login,now,profile.loginTimeoutMs);
  }
  void idle(uint64_t now){phase=Phase::Idle;lastActivity=now;capture=switchcli::TerminalCapture();}
  void ready(uint64_t now){
    // Runtime abandons its old requests on a disconnected session.
    if(reply.id.compare(0,4,"bdi-")==0&&reply.done())reply=switchcli::Result();
    status("connected");idle(now);
  }
  bool checkLogin(uint64_t now){
    const auto tail=lower(switchmodel::trim(capture.tailText()));
    const auto output=lower(capture.output);
    if(output.find("authentication fail")!=std::string::npos||output.find("login incorrect")!=std::string::npos||
       output.find("login failed")!=std::string::npos||output.find("wrong password")!=std::string::npos||output.find("access denied")!=std::string::npos){
      stop("authentication_failed",now,false);return true;
    }
    if(usernamePrompt(tail)){
      if(usernameSent||passwordSent){stop("authentication_failed",now,false);return true;}
      if(profile.username.empty()){stop("missing_username",now,false);return true;}
      usernameSent=true;status("authenticating");send(profile.username,Phase::Login,now,profile.loginTimeoutMs);return true;
    }
    if(passwordPrompt(tail)){
      if(passwordSent){stop("authentication_failed",now,false);return true;}
      if(profile.password.empty()){stop("missing_password",now,false);return true;}
      passwordSent=true;status("authenticating");send(profile.password,Phase::Login,now,profile.loginTimeoutMs);return true;
    }
    return false;
  }
  void received(uint64_t now){
    if(phase==Phase::Login){
      if(checkLogin(now))return;
      const auto found=adapter.prompt(capture.tailText());
      if(found.empty())return;
      if(!adapter.userPrompt(found)){stop("unexpected_view",now);return;}
      prompt=found;setup=adapter.sessionSetup();setupIndex=0;
      if(setup.empty())ready(now);else send(setup[0],Phase::Setup,now,profile.commandTimeoutMs);
      return;
    }
    const auto tail=lower(switchmodel::trim(capture.tailText()));
    if(usernamePrompt(tail)||passwordPrompt(tail)){stop("session_expired",now);return;}
    if(phase==Phase::Idle)return;
    const auto found=adapter.prompt(capture.tailText());if(found.empty())return;
    if(phase==Phase::Setup){
      if(found!=prompt||adapter.failed(capture.output)||capture.truncated){stop("session_setup_failed",now);return;}
      if(++setupIndex<setup.size())send(setup[setupIndex],Phase::Setup,now,profile.commandTimeoutMs);else ready(now);
    }else if(phase==Phase::Probe){
      if(found!=prompt||!adapter.userPrompt(found)){stop("unexpected_view",now);return;}
      idle(now);
    }else if(phase==Phase::Sync){
      if(found!=prompt||!adapter.userPrompt(found)){stop("unexpected_view",now);return;}
      if(steps.empty())send(adapter.readCommand(reply.command),Phase::Read,now,profile.commandTimeoutMs);
      else send(steps[0],Phase::Action,now,profile.commandTimeoutMs);
    }else if(phase==Phase::Read){
      appendCapture();
      if(found!=prompt||!adapter.userPrompt(found)){stop("unexpected_view",now);return;}
      publish(adapter.failed(transcript.output)?"command_error":transcript.truncated?"truncated":"ok");idle(now);
    }else if(phase==Phase::Action){
      const bool failed=adapter.failed(capture.output)||capture.truncated;
      appendCapture();
      if(step>=expected.size()||found!=expected[step]||failed||transcript.truncated){stop("command_error",now);return;}
      if(++step<steps.size())send(steps[step],Phase::Action,now,profile.commandTimeoutMs);
      else {publish("ok");idle(now);}
    }
  }
  std::string enqueue(const std::string& id,const std::string& name,const std::vector<std::string>& commands){
    if(reply.id==id)return reply.command==name?reply.state:"id_conflict";
    if(!connected())return "not_connected";
    if(reply.state=="queued"||reply.state=="running"||reply.id.compare(0,4,"bdi-")==0)return "busy";
    if(reply.id.compare(0,4,"bdi-")!=0)userReply=reply;
    reply=switchcli::Result();reply.id=id;reply.command=name;reply.target=profile.target;reply.state="queued";
    steps=commands;expected.clear();step=0;transcript=switchcli::TerminalCapture();return "queued";
  }
public:
  std::function<void(const std::string&)> log;
  ConsoleSession(const switchdevice::IDeviceAdapter& device,IConsolePort& io):adapter(device),port(io){}
  void configure(const ConsoleSettings& settings,uint64_t now){
    if(reply.state=="queued"||reply.state=="running")publish("cancelled");
    // Clear abandoned BDI slots on explicit reconfiguration, preserving a UI reply.
    if(reply.id.compare(0,4,"bdi-")==0)reply=switchcli::Result();
    profile=settings;phase=Phase::Stopped;wire.clear();capture=switchcli::TerminalCapture();transcript=switchcli::TerminalCapture();
    if(!profile.enabled){status("disabled");return;}
    const auto error=profile.validate();if(!error.empty()){status(error);return;}
    startLogin(now);
  }
  void tick(uint64_t now){
    if(phase==Phase::Stopped)return;
    if(phase==Phase::Retry){
      // Discard stale command bytes before opening a new logical session.
      char discard[256];port.read(discard,sizeof(discard));
      if(now>=retryAt)startLogin(now);return;
    }
    if(phase!=Phase::Idle&&now>=deadline){stop("timeout_or_disconnected",now);return;}
    if(!wire.empty()){
      const size_t sent=std::min(port.write(wire.data(),wire.size()),wire.size());wire.erase(0,sent);
      if(!wire.empty())return;
    }
    char buffer[256];size_t total=0;
    while(total<2048){const auto count=port.read(buffer,sizeof(buffer));if(!count)break;capture.append(buffer,count);total+=count;}
    if(total)received(now);
    if(phase==Phase::Idle&&reply.state=="queued"){
      if(!steps.empty()){
        expected=adapter.actionPrompts(switchmodel::Action::Isolate,reply.command.substr(8),prompt);
        if(expected.empty()||expected.size()!=steps.size()||expected.size()>16||expected.back()!=prompt){publish("command_error");return;}
      }
      reply.state="running";send("",Phase::Sync,now,profile.commandTimeoutMs);
    }else if(phase==Phase::Idle&&now-lastActivity>=profile.probeIntervalMs)send("",Phase::Probe,now,profile.loginTimeoutMs);
  }
  const char* kind()const override{return "console";}
  const char* stateName()const override{return state.c_str();}
  bool connected()override{return state=="connected"&&phase!=Phase::Stopped&&phase!=Phase::Retry;}
  bool sessionAccepted()override{return profile.enabled&&connected();}
  std::string submit(const std::string& id,const std::string& command)override{
    if(!switchcli::identifier(id)||!adapter.readCommand(command))return "invalid_command";
    return enqueue(id,command,{});
  }
  std::string submitIsolation(const std::string& id,const std::string& requested)override{
    const auto canonical=adapter.canonicalPort(requested);const auto commands=adapter.actionCommands(switchmodel::Action::Isolate,canonical);
    if(!switchcli::identifier(id)||id.compare(0,4,"bdi-")!=0||canonical.empty()||commands.empty())return "invalid_command";
    return enqueue(id,"isolate:"+canonical,commands);
  }
  bool resultReady(const std::string& id)override{return reply.id==id&&reply.done();}
  switchcli::Result result(const std::string& id)override{
    auto value=reply.id==id?reply:userReply;if(value.id!=id){value=switchcli::Result();value.id=id;value.state="not_found";}return value;
  }
  switchcli::Result takeResult(const std::string& id)override{
    if(reply.id==id&&reply.done()){auto value=reply;reply=switchcli::Result();return value;}
    switchcli::Result value;value.id=id;value.state="not_found";return value;
  }
};
}
