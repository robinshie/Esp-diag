#pragma once
#include "console_session.h"

namespace switchtransport {
// Physical link lifecycle is independent of CLI login and BDI policy.
class IManagedConsolePort : public IConsolePort {
public:
  virtual bool begin(unsigned baud)=0;
  virtual void end()=0;
  virtual void tick(uint64_t now)=0;
  virtual bool ready()const=0;
  virtual uint64_t generation()const=0;
  virtual const char* stateName()const=0;
};
class ManagedConsole : public ConsoleSession {
  IManagedConsolePort& port;
  const char* mode;
  ConsoleSettings settings;
  bool enabled=false,attached=false;
  uint64_t epoch=0;
  std::string state="disabled";
public:
  ManagedConsole(const switchdevice::IDeviceAdapter& adapter,IManagedConsolePort& link,const char* name)
    :ConsoleSession(adapter,link),port(link),mode(name){}
  void configure(const ConsoleSettings& profile,uint64_t now){
    auto off=profile;off.enabled=false;ConsoleSession::configure(off,now);
    port.end();attached=enabled=false;settings=profile;state="disabled";
    if(!profile.enabled)return;
    state=profile.validate();if(!state.empty())return;
    enabled=port.begin(profile.baud);state=enabled?"waiting_console_link":"console_link_start_failed";
  }
  void tick(uint64_t now){
    if(!enabled)return;
    port.tick(now);
    if(attached&&(!port.ready()||epoch!=port.generation())){
      auto off=settings;off.enabled=false;ConsoleSession::configure(off,now);attached=false;
    }
    if(!port.ready()){state=port.stateName();return;}
    if(!attached){epoch=port.generation();ConsoleSession::configure(settings,now);attached=true;}
    ConsoleSession::tick(now);
  }
  const char* kind()const override{return mode;}
  const char* stateName()const override{return attached&&port.ready()&&epoch==port.generation()?ConsoleSession::stateName():state.c_str();}
  bool connected()override{return attached&&port.ready()&&epoch==port.generation()&&ConsoleSession::connected();}
  bool sessionAccepted()override{return connected();}
};
}
