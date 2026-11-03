#pragma once
#include <string>
#include <cstdint>

// Console session settings, independent of USB/UART hardware and IP connectivity.
struct ConsoleSettings {
  bool enabled=false;
  unsigned baud=9600;
  uint32_t commandTimeoutMs=30000,loginTimeoutMs=15000;
  uint32_t probeIntervalMs=10000,retryMs=5000;
  std::string target,username,password;
  std::string validate()const{
    if(baud!=9600&&baud!=19200&&baud!=38400&&baud!=57600&&baud!=115200)return "console_baud_invalid";
    if(commandTimeoutMs<1000||commandTimeoutMs>60000||loginTimeoutMs<1000||loginTimeoutMs>60000||
       probeIntervalMs<1000||probeIntervalMs>60000||retryMs<1000||retryMs>60000)return "console_timeout_invalid";
    if(target.empty()||target.size()>192||target.find_first_of(" \t\r\n")!=std::string::npos)return "console_target_invalid";
    if(username.size()>64||password.size()>128)return "console_credentials_invalid";
    for(const auto* value:{&username,&password})for(unsigned char c:*value)if(c<32||c==127)return "console_credentials_invalid";
    return "";
  }
};
