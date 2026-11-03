#pragma once
#include <Arduino.h>
#include "../common/firmware_log.h"
#include <ETH.h>
#include <atomic>
#include <cstring>
#include <libssh_esp32.h>
#include <libssh/libssh.h>
#include "../config/switch_settings.h"
#include "command_result.h"
#include "command_transport.h"
#include "terminal_capture.h"
#include "../adapters/device_adapter.h"

// All libssh objects belong to the worker; the Arduino loop only publishes profiles.
class SwitchSsh : public switchtransport::ICommandTransport {
  const switchdevice::IDeviceAdapter& adapter;
  SemaphoreHandle_t lock=nullptr;
  SwitchSettings pending;
  unsigned revision=0,commandRevision=0;
  switchcli::Result reply, userReply;
  std::vector<std::string> script;
  switchmodel::Action scriptAction=switchmodel::Action::None;
  std::function<std::string(const std::string&,unsigned,const std::string&,const std::string&)> verifyHostKey;
  std::atomic<const char*> keyState{"unpaired"};
  std::atomic<unsigned> connectedRevision{~0u};
  std::atomic<const char*> state{"starting"};
  void status(const char* value) {
    if(state.exchange(value)!=value) firmware::printf("[SSH] state=%s\n",value);
  }
  bool current(unsigned expected) {
    xSemaphoreTake(lock,portMAX_DELAY);bool same=revision==expected;xSemaphoreGive(lock);return same;
  }
  static bool linkReady(){return ETH.linkUp()&&uint32_t(ETH.localIP())!=0;}
  static std::string fingerprint(ssh_session session) {
    ssh_key key=nullptr;unsigned char* hash=nullptr;size_t size=0;
    if(ssh_get_server_publickey(session,&key)!=SSH_OK)return "";
    int result=ssh_get_publickey_hash(key,SSH_PUBLICKEY_HASH_SHA256,&hash,&size);ssh_key_free(key);
    if(result!=SSH_OK)return "";
    char* fingerprint=ssh_get_fingerprint_hash(SSH_PUBLICKEY_HASH_SHA256,hash,size);
    std::string observed=fingerprint?fingerprint:"";
    ssh_string_free_char(fingerprint);ssh_clean_pubkey_hash(&hash);return observed;
  }
  bool exchange(ssh_channel channel,const std::string& command,std::string& prompt,unsigned version,unsigned timeout,switchcli::TerminalCapture& capture,bool changeView=false){
    // A PTY accepts CR as Enter; keep one input line per command.
    std::string wire=command+"\r";size_t sent=0;uint32_t started=millis();
    while(sent<wire.size()){
      if(!current(version)||!linkReady()||uint32_t(millis()-started)>=timeout)return false;
      int count=ssh_channel_write(channel,wire.data()+sent,wire.size()-sent);
      if(count<=0)return false;
      sent+=count;
    }
    while(current(version)&&linkReady()&&uint32_t(millis()-started)<timeout){
      char buffer[256];int count=ssh_channel_read_nonblocking(channel,buffer,sizeof(buffer),0);
      if(count==SSH_ERROR||ssh_channel_is_eof(channel)||ssh_channel_is_closed(channel))return false;
      if(count>0){
        capture.append(buffer,count);auto found=adapter.prompt(capture.tailText());
        if(!found.empty()&&(prompt.empty()||found==prompt||changeView)){prompt=found;return true;}
      }else vTaskDelay(pdMS_TO_TICKS(10));
    }return false;
  }
  bool commandStep(ssh_session session,ssh_channel shell,const SwitchSettings& profile,unsigned version){
    xSemaphoreTake(lock,portMAX_DELAY);
    if(reply.state!="queued"){xSemaphoreGive(lock);return true;}
    if(commandRevision!=version){reply.state="cancelled";xSemaphoreGive(lock);return true;}
    reply.state="running";auto id=reply.id;auto name=reply.command;auto steps=script;auto stepAction=scriptAction;xSemaphoreGive(lock);
    switchcli::TerminalCapture capture;bool ok=false;int exitStatus=0;ssh_channel exec=nullptr;
    if(shell){
      std::string prompt;switchcli::TerminalCapture sync,setup;
      ok=exchange(shell,"",prompt,version,profile.commandTimeout*1000,sync)&&adapter.userPrompt(prompt);
      for(const auto& command:adapter.sessionSetup()){
        if(!ok)break;
        setup=switchcli::TerminalCapture();
        ok=exchange(shell,command,prompt,version,profile.commandTimeout*1000,setup)&&!adapter.failed(setup.output)&&!setup.truncated;
      }
      if(ok&&steps.empty())ok=exchange(shell,adapter.readCommand(name),prompt,version,profile.commandTimeout*1000,capture);
      else if(ok){
        const auto userPrompt=prompt;
        const auto expectedPort=name.substr(name.find(':')+1);
        const auto expected=adapter.actionPrompts(stepAction,expectedPort,userPrompt);
        ok=expected.size()==steps.size()&&!expected.empty()&&expected.size()<=16&&expected.back()==userPrompt;
        for(size_t i=0;i<steps.size()&&ok;++i){
          switchcli::TerminalCapture step;
          ok=exchange(shell,steps[i],prompt,version,profile.commandTimeout*1000,step,true)&&!adapter.failed(step.output)&&!step.truncated;
          capture.append(step.output.data(),step.output.size());
          // Every view transition must finish; returning to user view is mandatory.
          if(ok)ok=prompt==expected[i];
        }
      }
    }else{
      exec=ssh_channel_new(session);
      ok=exec&&ssh_channel_open_session(exec)==SSH_OK&&ssh_channel_request_exec(exec,adapter.readCommand(name))==SSH_OK;
      bool finished=false;uint32_t started=millis();
      while(ok&&current(version)&&linkReady()&&uint32_t(millis()-started)<profile.commandTimeout*1000){
        char buffer[256];
        for(int stream=0;stream<2;++stream){int count=ssh_channel_read_nonblocking(exec,buffer,sizeof(buffer),stream);if(count==SSH_ERROR)ok=false;else if(count>0)capture.append(buffer,count);}
        if(ssh_channel_is_eof(exec)||ssh_channel_is_closed(exec)){finished=true;break;}
        vTaskDelay(pdMS_TO_TICKS(10));
      }
      ok=ok&&finished;if(exec){if(finished){uint32_t code=0;exitStatus=ssh_channel_get_exit_state(exec,&code,nullptr,nullptr)==SSH_OK?int(code):-1;}ssh_channel_free(exec);}
    }
    bool same=current(version);
    xSemaphoreTake(lock,portMAX_DELAY);
    if(reply.id==id){reply.output=capture.output;reply.truncated=capture.truncated;
      reply.state=!same?"cancelled":!ok?"timeout_or_disconnected":(adapter.failed(capture.output)||exitStatus!=0)?"command_error":capture.truncated?"truncated":"ok";}
    xSemaphoreGive(lock);
    firmware::println("[SSH][COMMAND] request_id="+id+" command="+name+" target="+profile.host+" state="+(!same?"cancelled":!ok?"timeout_or_disconnected":(adapter.failed(capture.output)||exitStatus!=0)?"command_error":capture.truncated?"truncated":"ok"));
    size_t at=0;while(at<capture.output.size()){
      auto end=capture.output.find('\n',at);if(end==std::string::npos)end=capture.output.size();
      for(size_t offset=at;offset<end;offset+=800)firmware::println("[SSH][OUTPUT] request_id="+id+" command="+name+" target="+profile.host+" text="+capture.output.substr(offset,std::min<size_t>(800,end-offset)));
      at=end+1;
    }
    return ok&&same; // Never reuse a shell with an unknown command boundary.
  }
  void cancelCommand(){xSemaphoreTake(lock,portMAX_DELAY);if(reply.state=="queued"||reply.state=="running")reply.state="disconnected";xSemaphoreGive(lock);}
  void run() {
    libssh_begin();
    for(;;) {
      xSemaphoreTake(lock,portMAX_DELAY);SwitchSettings profile=pending;unsigned version=revision;xSemaphoreGive(lock);
      if(profile.protocol!="ssh"){status(profile.protocol=="none"?"disabled":"telnet_unsupported");vTaskDelay(pdMS_TO_TICKS(500));continue;}
      if(!linkReady()){status("waiting_network");vTaskDelay(pdMS_TO_TICKS(500));continue;}
      if(profile.password.empty()){status("missing_password");vTaskDelay(pdMS_TO_TICKS(500));continue;}
      status("connecting");
      ssh_session session=ssh_new();ssh_channel channel=nullptr;bool stopRetry=false;
      if(session) {
        unsigned port=profile.port;long timeout=profile.connectTimeout;int verbosity=SSH_LOG_NOLOG;
        bool options=ssh_options_set(session,SSH_OPTIONS_HOST,profile.host.c_str())==SSH_OK&&
          ssh_options_set(session,SSH_OPTIONS_PORT,&port)==SSH_OK&&
          // Optional device-profile compatibility appended to modern defaults.
          (!adapter.hostKeyAlgorithms()||ssh_options_set(session,SSH_OPTIONS_HOSTKEYS,adapter.hostKeyAlgorithms())==SSH_OK)&&
          ssh_options_set(session,SSH_OPTIONS_USER,profile.username.c_str())==SSH_OK&&
          ssh_options_set(session,SSH_OPTIONS_TIMEOUT,&timeout)==SSH_OK&&
          ssh_options_set(session,SSH_OPTIONS_LOG_VERBOSITY,&verbosity)==SSH_OK;
        if(!options||ssh_connect(session)!=SSH_OK){
          status("connect_failed");
          firmware::printf("[SSH] connect_error=%.240s\n",ssh_get_error(session));
        }
        else if(!current(version))status("reconfiguring");
        else {
          auto trust=verifyHostKey?verifyHostKey(profile.host,profile.port,fingerprint(session),profile.fingerprint):"error";
          keyState.store(trust=="trusted"?"trusted":trust=="mismatch"?"mismatch":"error");
          if(trust!="trusted"){status(trust=="mismatch"?"host_key_mismatch":"host_key_store_error");stopRetry=true;}
          else if(!current(version))status("reconfiguring");
          else {
          status("authenticating");
          if(ssh_userauth_password(session,nullptr,profile.password.c_str())!=SSH_AUTH_SUCCESS){status("authentication_failed");stopRetry=true;}
          else {
            timeout=profile.commandTimeout;ssh_options_set(session,SSH_OPTIONS_TIMEOUT,&timeout);
            bool ready=true;
            if(profile.transport=="shell") {
              channel=ssh_channel_new(session);
              ready=channel&&ssh_channel_open_session(channel)==SSH_OK&&
                ssh_channel_request_pty(channel)==SSH_OK&&ssh_channel_request_shell(channel)==SSH_OK;
            }
            if(!ready)status("shell_failed");
            else {
              xSemaphoreTake(lock,portMAX_DELAY);
              if(reply.id.compare(0,4,"bdi-")==0)reply=switchcli::Result();
              xSemaphoreGive(lock);
              connectedRevision.store(version);
              status("connected");uint32_t lastPing=millis();
              while(current(version)&&linkReady()&&ssh_is_connected(session)) {
                if(!commandStep(session,channel,profile,version))break;
                // Consume banners without logging remote output or interpreting commands.
                char buffer[256];
                if(channel) {
                  int read=ssh_channel_read_nonblocking(channel,buffer,sizeof(buffer),0);
                  if(read==SSH_ERROR||ssh_channel_is_eof(channel)||ssh_channel_is_closed(channel))break;
                }
                if(uint32_t(millis()-lastPing)>=10000) {
                  lastPing=millis();if(ssh_send_ignore(session,"")!=SSH_OK)break;
                }
                vTaskDelay(pdMS_TO_TICKS(50));
              }
              status("disconnected");
            }
          }
        }
        }
        cancelCommand();
        if(channel)ssh_channel_free(channel);
        ssh_disconnect(session);ssh_free(session);
      }else status("out_of_memory");
      // Authentication/identity failures require a changed profile or reboot.
      if(stopRetry){while(current(version))vTaskDelay(pdMS_TO_TICKS(500));}
      else for(unsigned i=0;i<60&&current(version);++i)vTaskDelay(pdMS_TO_TICKS(500));
    }
  }
public:
  const char* kind()const override{return "ssh";}
  bool sessionAccepted()override{return connected()&&std::string(hostKeyState())=="trusted";}
  explicit SwitchSsh(const switchdevice::IDeviceAdapter& device):adapter(device){}
  void setHostKeyVerifier(std::function<std::string(const std::string&,unsigned,const std::string&,const std::string&)> verifier){verifyHostKey=verifier;}
  const char* hostKeyState() const{return keyState.load();}
  void begin(const SwitchSettings& profile) {
    lock=xSemaphoreCreateMutex();if(!lock){status("out_of_memory");return;}
    pending=profile;
    if(xTaskCreate([](void* self){static_cast<SwitchSsh*>(self)->run();},"switch-ssh",51200,this,1,nullptr)!=pdPASS)status("task_failed");
  }
  void configure(const SwitchSettings& profile,bool force=false) {
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(force||profile.encode()!=pending.encode()){pending=profile;++revision;keyState.store("unpaired");if(reply.id.compare(0,4,"bdi-")==0)reply=switchcli::Result();}
    xSemaphoreGive(lock);
  }
  // Application requests a pause; stopped() acknowledges worker cleanup.
  // The caller retains the saved profile and supplies it when resuming.
  void pause(){
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);auto profile=pending;xSemaphoreGive(lock);
    profile.protocol="none";configure(profile,true);
  }
  void resume(const SwitchSettings& profile){configure(profile,true);}
  bool stopped()const{return std::strcmp(state.load(),"disabled")==0;}
  std::string submit(const std::string& id,const std::string& name){
    if(!switchcli::identifier(id)||!adapter.readCommand(name))return "invalid_command";
    if(!lock)return "not_ready";
    xSemaphoreTake(lock,portMAX_DELAY);
    std::string answer="queued";
    if(reply.id==id)answer=reply.command==name?reply.state:"id_conflict";
    else if(std::string(state.load())!="connected"||connectedRevision.load()!=revision)answer="not_connected";
    else if(reply.state=="queued"||reply.state=="running"||reply.id.compare(0,4,"bdi-")==0)answer="busy";
    else {if(reply.id.compare(0,4,"bdi-")!=0)userReply=reply;script.clear();reply=switchcli::Result();reply.id=id;reply.command=name;reply.target=pending.host;reply.state="queued";commandRevision=revision;}
    xSemaphoreGive(lock);return answer;
  }
  // Internal policy-only write capability. HTTP accepts named read commands only.
  std::string submitIsolation(const std::string& id,const std::string& requested){
    return submitAction(id,switchmodel::Action::Isolate,requested);
  }
  std::string submitAction(const std::string& id,switchmodel::Action action,const std::string& requested)override{
    const auto port=adapter.canonicalPort(requested);
    const auto commands=adapter.actionCommands(action,port);
    if(port.empty()||commands.empty()||!switchcli::identifier(id)||id.compare(0,4,"bdi-")!=0)return "invalid_command";
    if(!lock)return "not_ready";
    xSemaphoreTake(lock,portMAX_DELAY);
    std::string answer="queued";
    if(reply.id==id)answer=reply.command==switchmodel::actionCommandKey(action,port)?reply.state:"id_conflict";
    else if(std::string(state.load())!="connected"||connectedRevision.load()!=revision)answer="not_connected";
    else if(pending.transport!="shell")answer="shell_required";
    else if(reply.state=="queued"||reply.state=="running"||reply.id.compare(0,4,"bdi-")==0)answer="busy";
    else {
      if(reply.id.compare(0,4,"bdi-")!=0)userReply=reply;
      reply=switchcli::Result();reply.id=id;reply.command=switchmodel::actionCommandKey(action,port);reply.target=pending.host;reply.state="queued";commandRevision=revision;
      script=commands;scriptAction=action;
    }
    xSemaphoreGive(lock);return answer;
  }
  bool resultReady(const std::string& id){if(!lock)return false;xSemaphoreTake(lock,portMAX_DELAY);bool ready=reply.id==id&&reply.done();xSemaphoreGive(lock);return ready;}
  switchcli::Result result(const std::string& id){
    if(!lock){switchcli::Result value;value.state="not_ready";return value;}
    xSemaphoreTake(lock,portMAX_DELAY);auto value=reply.id==id?reply:userReply;xSemaphoreGive(lock);
    if(value.id!=id){value=switchcli::Result();value.id=id;value.state="not_found";}return value;
  }
  switchcli::Result takeResult(const std::string& id){
    if(!lock)return result(id);
    xSemaphoreTake(lock,portMAX_DELAY);auto value=reply;
    if(reply.id==id&&reply.done())reply=switchcli::Result();
    else {value=switchcli::Result();value.id=id;value.state="not_found";}
    xSemaphoreGive(lock);return value;
  }
  bool connected() {
    if(!lock)return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ready=std::string(state.load())=="connected"&&connectedRevision.load()==revision;
    xSemaphoreGive(lock);return ready;
  }
  const char* stateName() const{return state.load();}
};
