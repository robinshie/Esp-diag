#pragma once
#include "../storage/nvs_text_store.h"
#include "ssh_host_key.h"
#include "../config/network_settings.h"

class SshTrustStore {
  NvsTextStore store;
  SemaphoreHandle_t lock=nullptr;
  SshHostKey trust;
  static std::string key(const std::string& endpoint){
    auto split=endpoint.find(':');uint32_t ip=0;
    if(split==std::string::npos||!NetworkSettings::address(endpoint.substr(0,split),ip))return "";
    char name[16];snprintf(name,sizeof(name),"k%08lx%04x",(unsigned long)ip,unsigned(strtoul(endpoint.c_str()+split+1,nullptr,10)));return name;
  }
public:
  void begin(){
    lock=xSemaphoreCreateMutex();store.begin("ssh-trust");
    trust.read=[this](const std::string& endpoint,std::string& value){return store.read(key(endpoint),value);};
    trust.write=[this](const std::string& endpoint,const std::string& value){return store.write(key(endpoint),value);};
  }
  std::string verify(const std::string& host,unsigned port,const std::string& observed,const std::string& legacy){
    if(!lock)return "error";
    xSemaphoreTake(lock,portMAX_DELAY);auto result=trust.verify(host,port,observed,legacy);xSemaphoreGive(lock);return result;
  }
  std::string fingerprint(const std::string& host,unsigned port){
    std::string result;store.read(key(SshHostKey::endpoint(host,port)),result);return result;
  }
  bool forget(const std::string& host,unsigned port){
    if(!lock)return false;
    xSemaphoreTake(lock,portMAX_DELAY);bool result=store.erase(key(SshHostKey::endpoint(host,port)));xSemaphoreGive(lock);return result;
  }
};
