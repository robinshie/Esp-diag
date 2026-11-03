#pragma once
#include "switch_ssh.h"
#include "console_ports.h"
#include "failover_transport.h"

// Composition root. Automatic failover is enabled only in the dual-channel build.
class SwitchCommunication {
  SwitchSsh& ssh;
  SwitchSettings appliedProfile;
  bool configured=false;
#if ESP_SWITCH_CHANNEL == 1 || ESP_SWITCH_CHANNEL == 3
  switchtransport::UartConsolePort port;
  switchtransport::ManagedConsole console;
#if ESP_SWITCH_CHANNEL == 3
  switchtransport::FailoverTransport failover;
  ConsoleSettings backupSettings;
#endif
#elif ESP_SWITCH_CHANNEL == 2
  switchtransport::TcpConsolePort port;
  switchtransport::ManagedConsole console;
#endif
public:
  SwitchCommunication(SwitchSsh& value,const switchdevice::IDeviceAdapter& adapter):ssh(value)
#if ESP_SWITCH_CHANNEL == 1 || ESP_SWITCH_CHANNEL == 3
    ,console(adapter,port,"uart_console")
#if ESP_SWITCH_CHANNEL == 3
    ,failover(value,console)
#endif
#elif ESP_SWITCH_CHANNEL == 2
    ,console(adapter,port,"tcp_console")
#endif
  { (void)adapter;
#if ESP_SWITCH_CHANNEL == 3
    failover.setPrimaryEnabled=[this](bool enabled){
      if(enabled)ssh.resume(appliedProfile);else ssh.pause();
    };
    failover.primaryStopped=[this](){return ssh.stopped();};
    failover.setBackupEnabled=[this](bool enabled,uint64_t now){
      auto settings=backupSettings;settings.enabled=enabled;console.configure(settings,now);
    };
#endif
  }
  switchtransport::ICommandTransport& connection(){
#if ESP_SWITCH_CHANNEL == 0
    return ssh;
#elif ESP_SWITCH_CHANNEL == 3
    return failover;
#else
    return console;
#endif
  }
  const char* policy()const{
#if ESP_SWITCH_CHANNEL == 3
    return "ssh_uart_failover";
#else
    return "fixed";
#endif
  }
  uint64_t generation()const{
#if ESP_SWITCH_CHANNEL == 3
    return failover.generation();
#else
    return 0;
#endif
  }
  std::string consoleState()const{
#if ESP_SWITCH_CHANNEL != 0
    return console.stateName();
#else
    return "disabled";
#endif
  }
  std::string identity()const{
#if ESP_SWITCH_CHANNEL == 3
    if(!failover.usingBackup())return "";
#endif
#if ESP_SWITCH_CHANNEL == 1 || ESP_SWITCH_CHANNEL == 3
    return "uart1:"+std::to_string(ESP_CONSOLE_TX)+":"+std::to_string(ESP_CONSOLE_RX)+":"+std::to_string(ESP_CONSOLE_BAUD);
#elif ESP_SWITCH_CHANNEL == 2
    return std::string(ESP_CONSOLE_BRIDGE_HOST)+":"+std::to_string(ESP_CONSOLE_BRIDGE_PORT)+":"+std::to_string(ESP_CONSOLE_BAUD);
#else
    return "";
#endif
  }
  unsigned endpointPort(unsigned sshPort)const{
#if ESP_SWITCH_CHANNEL == 3
    if(!failover.usingBackup())return sshPort;
#endif
#if ESP_SWITCH_CHANNEL == 0
    return sshPort;
#else
    (void)sshPort;return 1;
#endif
  }
  void begin(const SwitchSettings& profile,uint64_t now){
#if ESP_SWITCH_CHANNEL == 0
    (void)now;ssh.begin(profile);appliedProfile=profile;configured=true;
#else
    console.log=firmware::println;
#if ESP_SWITCH_CHANNEL == 3
    failover.log=firmware::println;ssh.begin(profile);
#endif
    configure(profile,now);
#endif
  }
  bool configure(const SwitchSettings& profile,uint64_t now){
    auto comparable=profile;comparable.failoverWaitSeconds=appliedProfile.failoverWaitSeconds;
    if(configured&&profile.failoverWaitSeconds!=appliedProfile.failoverWaitSeconds&&comparable.encode()==appliedProfile.encode()){
#if ESP_SWITCH_CHANNEL == 3
      failover.setWaitSeconds(profile.failoverWaitSeconds);
#endif
      appliedProfile=profile;return false; // Policy-only change: preserve live requests and evidence.
    }
    appliedProfile=profile;configured=true;
#if ESP_SWITCH_CHANNEL == 0
    (void)now;ssh.configure(profile,true);
#else
#if ESP_SWITCH_CHANNEL == 3
    ssh.configure(profile,true);
#endif
    ConsoleSettings settings;settings.enabled=profile.protocol!="none";
#if ESP_SWITCH_CHANNEL == 3
    settings.enabled=profile.protocol=="ssh";
#endif
    settings.target=profile.host;settings.username=profile.username;settings.password=profile.password;
    settings.baud=ESP_CONSOLE_BAUD;
#if ESP_SWITCH_CHANNEL == 3
    backupSettings=settings;settings.enabled=false;console.configure(settings,now);
    failover.configure(profile.protocol=="ssh",now,profile.failoverWaitSeconds);
#else
    console.configure(settings,now);
#endif
#endif
    return true;
  }
  void tick(uint64_t now){
#if ESP_SWITCH_CHANNEL == 0
    (void)now;
#else
    console.tick(now);
#if ESP_SWITCH_CHANNEL == 3
    failover.tick(now);
#endif
#endif
  }
};
