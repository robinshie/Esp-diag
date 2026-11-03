#pragma once
#include "usb_serial_host.h"

namespace switchtransport {
// Composition for native Type-C -> PL2303 -> H3C RJ45 Console.
// A hardware driver is injected; no PC, Ethernet, UART GPIO or USB logging bridge.
class SwitchUsbConsole : public ConsoleSession {
  IUsbSerialHost& host;
  ConsoleSettings settings;
  bool enabled=false,attached=false;
  uint64_t generation=0;
  std::string hostState="disabled";
  bool selected()const{const auto d=host.device();return d.vendor==0x067b&&d.product==0x2303;}
public:
  SwitchUsbConsole(const switchdevice::IDeviceAdapter& adapter,IUsbSerialHost& driver):ConsoleSession(adapter,driver),host(driver){}
  void configure(const ConsoleSettings& profile,uint64_t now){
    auto off=profile;off.enabled=false;ConsoleSession::configure(off,now);
    if(enabled)host.end();
    enabled=attached=false;settings=profile;hostState="disabled";
    if(!profile.enabled)return;
    const auto error=profile.validate();if(!error.empty()){hostState=error;return;}
    UsbSerialSettings usb;usb.baud=profile.baud;
    enabled=host.begin(usb);hostState=enabled?"waiting_usb_device":"usb_host_start_failed";
  }
  void tick(uint64_t now){
    if(!enabled)return;
    host.tick();const auto device=host.device();
    const bool ready=host.ready()&&selected();
    if(attached&&(!ready||generation!=device.generation)){
      auto off=settings;off.enabled=false;ConsoleSession::configure(off,now);attached=false;
    }
    if(!ready){hostState=host.ready()?"unsupported_usb_device":host.stateName();return;}
    if(!attached){generation=device.generation;ConsoleSession::configure(settings,now);attached=true;}
    ConsoleSession::tick(now);
  }
  const char* kind()const override{return "usb_console";}
  const char* stateName()const override{return attached&&host.ready()&&selected()&&generation==host.device().generation?ConsoleSession::stateName():hostState.c_str();}
  bool connected()override{return attached&&host.ready()&&selected()&&generation==host.device().generation&&ConsoleSession::connected();}
  bool sessionAccepted()override{return connected();}
};
}
