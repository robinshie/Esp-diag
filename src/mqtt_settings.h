#pragma once
#include <string>
#include <vector>
#include "network_settings.h"

struct MqttSettings {
  bool enabled=false;
  std::string host, username, password;
  unsigned port=1883;
  std::string prefix="esp-lab";
  static bool printable(const std::string& s,size_t limit) {
    if(s.size()>limit)return false;
    for(unsigned char c:s)if(c<32 || c==127)return false;
    return true;
  }
  static bool topicPart(const std::string& s) {
    if(s.empty() || s.size()>48)return false;
    for(char c:s)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='-'||c=='_'))return false;
    return true;
  }
  std::string validate() const {
    if(!port || port>65535)return "MQTT port must be 1..65535";
    if(!printable(username,64)||!printable(password,128))return "Invalid MQTT credentials";
    if(!topicPart(prefix)||prefix.compare(0,7,"esp-lab")!=0)return "Use an esp-lab experiment topic prefix";
    if(!host.empty()) {
      uint32_t ip;
      if(!NetworkSettings::address(host,ip)||(ip>>24)==0||(ip>>24)>=224||(ip>>24)==127)return "Use broker LAN IPv4 address (not localhost)";
    }
    if(enabled&&host.empty())return "MQTT broker IPv4 is required";
    return "";
  }
  static bool parsePort(const std::string& s,unsigned& port) {
    if(s.empty()||s.size()>5)return false;
    unsigned n=0;for(char c:s){if(c<'0'||c>'9')return false;n=n*10+c-'0';}
    if(!n||n>65535)return false;
    port=n;return true;
  }
  std::string encode() const {
    return std::string(enabled?"1":"0")+"\n"+host+"\n"+std::to_string(port)+"\n"+username+"\n"+password+"\n"+prefix;
  }
  bool decode(const std::string& raw) {
    if(raw.size()>320)return false;
    std::vector<std::string> fields;size_t start=0;
    for(size_t i=0;i<=raw.size();++i)if(i==raw.size()||raw[i]=='\n'){fields.push_back(raw.substr(start,i-start));start=i+1;}
    if(fields.size()!=6||(fields[0]!="0"&&fields[0]!="1"))return false;
    MqttSettings next;next.enabled=fields[0]=="1";next.host=fields[1];
    if(!parsePort(fields[2],next.port))return false;
    next.username=fields[3];next.password=fields[4];next.prefix=fields[5];
    if(!next.validate().empty())return false;
    *this=next;return true;
  }
};
