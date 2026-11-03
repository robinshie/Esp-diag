#include "../src/net/switch_usb_console.h"
#include "../src/net/managed_console.h"
#include "../src/adapters/h3c/device_adapter.h"
#include <cassert>
#include <deque>
#include <iostream>

using namespace switchtransport;
struct FakeHost : IUsbSerialHost {
  bool plugged=true,started=false,open=true,passwordOnly=false,reject=false,wrongView=false,silent=false,failRead=false;
  size_t readChunk=7,writeChunk=3;
  std::string rx,tx,view="<H3C>",payload="H3C Comware Software\n",password="test-console-secret";
  unsigned auth=0;
  UsbSerialDevice descriptor;
  std::vector<std::string> commands;
  FakeHost(){descriptor.vendor=0x067b;descriptor.product=0x2303;descriptor.revision=0x0400;descriptor.generation=1;}
  bool begin(const UsbSerialSettings& s)override{assert(s.vendor==0x067b&&s.product==0x2303&&s.baud==9600);started=true;return true;}
  void end()override{started=false;rx.clear();tx.clear();}
  void tick()override{}
  bool ready()const override{return started&&plugged;}
  UsbSerialDevice device()const override{return descriptor;}
  const char* stateName()const override{return "waiting_usb_device";}
  size_t read(char* data,size_t capacity)override{
    const auto count=std::min(std::min(capacity,readChunk),rx.size());rx.copy(data,count);rx.erase(0,count);return count;
  }
  void line(const std::string& text){
    commands.push_back(text);
    if(!open){
      if(auth==0){rx+=passwordOnly?"Password:":"Username:";auth=passwordOnly?2:1;}
      else if(auth==1){assert(text=="admin");rx+="Password:";auth=2;}
      else {assert(text==password);rx+=reject?"Authentication failed\nUsername:":password+"\r\n<H3C>";open=!reject;++auth;}
      return;
    }
    if(text.empty()||text=="screen-length disable")rx+="\r\n"+view;
    else if(text=="system-view"){view="[H3C]";rx+="\n"+view;}
    else if(text=="interface GigabitEthernet 1/0/2"){view=wrongView?"[H3C-GigabitEthernet1/0/3]":"[H3C-GigabitEthernet1/0/2]";rx+="\n"+view;}
    else if(text=="shutdown")rx+="\n"+view;
    else if(text=="return"){view="<H3C>";rx+="\n"+view;}
    else if(!silent)rx+=text+"\r\n"+(failRead?"% Unrecognized command found at '^' position.\n":payload)+"\n<H3C>";
  }
  size_t write(const char* data,size_t length)override{
    if(!ready())return 0;
    const auto count=std::min(length,writeChunk);
    for(size_t n=0;n<count;++n){if(data[n]=='\r'){auto text=tx;tx.clear();line(text);}else tx+=data[n];}return count;
  }
  unsigned count(const std::string& command)const{return unsigned(std::count(commands.begin(),commands.end(),command));}
};
// Exercise the same CLI through UART/TCP-style lifecycle adapters.
struct FakePort : IManagedConsolePort {
  FakeHost host;
  uint64_t epoch=1;
  bool begin(unsigned baud)override{UsbSerialSettings s;s.baud=baud;return host.begin(s);}
  void end()override{host.end();++epoch;}
  void tick(uint64_t)override{}
  bool ready()const override{return host.ready();}
  uint64_t generation()const override{return epoch;}
  const char* stateName()const override{return "waiting_link";}
  size_t read(char* p,size_t n)override{return host.read(p,n);}
  size_t write(const char* p,size_t n)override{return host.write(p,n);}
};
void managedScenarios(){
  for(const char* mode:{"uart_console","tcp_console"}){
    h3c::DeviceAdapter adapter;FakePort port;ManagedConsole session(adapter,port,mode);
    ConsoleSettings profile;profile.enabled=true;profile.target="192.168.10.2";
    session.configure(profile,0);uint64_t now=0;
    auto spin=[&](){for(unsigned n=0;n<1000&&!session.connected();++n)session.tick(now+=5);assert(session.connected());};
    spin();assert(std::string(session.kind())==mode);
    assert(session.submit("pending","version")=="queued");
    port.host.plugged=false;++port.epoch;session.tick(now+=5);
    assert(!session.connected()&&session.result("pending").state=="cancelled");
    port.host.plugged=true;++port.epoch;spin();assert(port.host.count("display version")==0);
    assert(session.submit("read-2","version")=="queued");
    for(unsigned n=0;n<1000&&!session.resultReady("read-2");++n)session.tick(now+=5);
    assert(session.result("read-2").state=="ok");
    profile.enabled=false;session.configure(profile,now);assert(!session.connected()&&!port.host.started);
  }
}
struct Harness {
  h3c::DeviceAdapter adapter;
  FakeHost host;
  SwitchUsbConsole console{adapter,host};
  ConsoleSettings profile;
  uint64_t now=0;
  std::string logs;
  Harness(){profile.enabled=true;profile.target="switch-console-1";profile.username="admin";profile.password=host.password;profile.commandTimeoutMs=1000;profile.loginTimeoutMs=1000;console.log=[this](const std::string& text){logs+=text+'\n';};}
  void begin(){console.configure(profile,now);}
  void step(unsigned ms=5){now+=ms;console.tick(now);}
  template<class Predicate> void until(Predicate condition,unsigned limit=10000){while(limit--&&!condition())step();assert(condition());}
  void connect(){begin();until([&](){return console.connected();});assert(console.sessionAccepted());}
  switchcli::Result query(const std::string& id="read-1"){
    assert(console.submit(id,"version")=="queued");until([&](){return console.resultReady(id);});return console.result(id);
  }
};
int main(){
  managedScenarios();
  {Harness h;h.profile.enabled=false;h.begin();for(unsigned n=0;n<100;++n)h.step();assert(h.host.commands.empty()&&!h.console.connected());}
  {Harness h;h.profile.password="bad\rinput";h.begin();assert(!h.host.started&&h.host.commands.empty());}
  {Harness h;h.host.descriptor.vendor=0x1234;h.begin();for(unsigned n=0;n<100;++n)h.step();assert(h.host.commands.empty()&&std::string(h.console.stateName())=="unsupported_usb_device");}
  {Harness h;h.host.open=false;h.host.readChunk=1;h.host.writeChunk=1;h.connect();assert(h.host.auth==3);assert(h.logs.find(h.profile.password)==std::string::npos);assert(h.query().state=="ok");}
  {Harness h;h.host.open=false;h.host.passwordOnly=true;h.connect();assert(h.host.auth==3&&h.host.count("admin")==0);}
  {Harness h;h.host.open=false;h.host.reject=true;h.begin();h.until([&](){return std::string(h.console.stateName())=="authentication_failed";});for(unsigned n=0;n<15000;++n)h.step();assert(h.host.count(h.profile.password)==1&&!h.console.connected());}
  {Harness h;h.connect();assert(h.console.submit("bad;id","version")=="invalid_command");assert(h.console.submit("r1","system-view")=="invalid_command");assert(h.console.submitIsolation("user-1","GE1/0/2")=="invalid_command");assert(h.console.submitIsolation("bdi-act-1","GE1/0/2\rshutdown")=="invalid_command");
   assert(h.query("bdi-read-1").state=="ok");assert(h.console.submit("r2","cpu")=="busy");assert(h.console.takeResult("bdi-read-1").state=="ok");assert(h.query("r2").state=="ok");assert(h.console.submit("r2","cpu")=="id_conflict");}
  {Harness h;h.connect();h.host.payload="secret echo "+h.profile.password;const auto result=h.query();assert(result.state=="ok"&&result.output.find(h.profile.password)==std::string::npos);assert(h.logs.find(h.profile.password)==std::string::npos);}
  {Harness h;h.connect();h.host.readChunk=256;h.host.payload=std::string(20000,'x');assert(h.console.submit("large","version")=="queued");h.until([&](){return h.console.resultReady("large");});const auto result=h.console.result("large");assert(result.state=="truncated"&&result.truncated&&result.output.size()==8192);}
  {Harness h;h.connect();h.host.failRead=true;assert(h.query().state=="command_error");}
  {Harness h;h.connect();assert(h.console.submitIsolation("bdi-act-1","GE1/0/2")=="queued");h.until([&](){return h.console.resultReady("bdi-act-1");});assert(h.console.result("bdi-act-1").state=="ok"&&h.host.count("shutdown")==1&&h.host.count("return")==1);}
  {Harness h;h.connect();h.host.wrongView=true;assert(h.console.submitIsolation("bdi-act-1","GE1/0/2")=="queued");h.until([&](){return h.console.resultReady("bdi-act-1");});assert(h.console.result("bdi-act-1").state=="command_error"&&h.host.count("shutdown")==0&&!h.console.connected());}
  {Harness h;h.connect();h.host.silent=true;assert(h.console.submit("timeout","version")=="queued");h.until([&](){return h.console.resultReady("timeout");});assert(h.console.result("timeout").state=="timeout_or_disconnected"&&!h.console.connected());h.host.silent=false;h.until([&](){return h.console.connected();});assert(h.host.count("display version")==1);}
  {Harness h;h.connect();assert(h.console.submit("pending","version")=="queued");h.host.plugged=false;++h.host.descriptor.generation;h.step();assert(!h.console.connected()&&h.console.result("pending").state=="cancelled");h.host.plugged=true;++h.host.descriptor.generation;h.until([&](){return h.console.connected();});assert(h.host.count("display version")==0);}
  {Harness h;h.connect();const auto previous=h.host.count("screen-length disable");++h.host.descriptor.generation;assert(!h.console.connected());h.step();h.until([&](){return h.console.connected();});assert(h.host.count("screen-length disable")==previous+1);}
  {Harness h;h.connect();const auto probes=h.host.count("");for(unsigned n=0;n<2400;++n)h.step();assert(h.host.count("")>probes&&h.console.connected());}
  std::cout<<"16 USB Console interface scenarios passed (simulated driver, no physical USB claim)\n";
}
