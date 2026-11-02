#pragma once
#include "switch_settings.h"

// One versioned NVS value: no partially saved network/switch/MQTT combination.
struct SetupSettings {
  NetworkSettings network;
  SwitchSettings device;
  MqttSettings mqtt;
  std::string validate() const {
    auto error=network.validate();if(!error.empty())return error;
    error=device.validateNetwork(network);if(!error.empty())return error;
    return mqtt.validate();
  }
  std::string encode() const {
    std::string result="2\n";
    for(const auto& part:{network.encode(),device.encode(),mqtt.encode()})
      result+=std::to_string(part.size())+":"+part;
    return result;
  }
  bool decode(const std::string& raw) {
    if(raw.size()>1600||raw.compare(0,2,"2\n")!=0)return false;
    size_t position=2;std::string parts[3];
    for(auto& part:parts){
      size_t end=raw.find(':',position);if(end==std::string::npos||end==position||end-position>4)return false;
      size_t length=0;for(size_t i=position;i<end;++i){if(raw[i]<'0'||raw[i]>'9')return false;length=length*10+raw[i]-'0';}
      position=end+1;if(length>raw.size()-position)return false;
      part=raw.substr(position,length);position+=length;
    }
    if(position!=raw.size())return false;
    SetupSettings next;
    if(!next.network.decode(parts[0])||!next.device.decode(parts[1])||!next.mqtt.decode(parts[2])||!next.validate().empty())return false;
    *this=next;return true;
  }
};
