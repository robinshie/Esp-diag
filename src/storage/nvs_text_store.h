#pragma once
#include <Arduino.h>
#include <nvs.h>
#include <string>
#include <vector>

// Atomic records; a missing key is distinguished from an I/O failure.
class NvsTextStore {
  nvs_handle_t handle=0;
  SemaphoreHandle_t lock=nullptr;
  bool ready=false;
public:
  bool begin(const char* name){lock=xSemaphoreCreateMutex();ready=lock&&nvs_open(name,NVS_READWRITE,&handle)==ESP_OK;return ready;}
  bool read(const std::string& key,std::string& out){
    out.clear();if(!ready)return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    size_t size=0;auto rc=nvs_get_str(handle,key.c_str(),nullptr,&size);bool ok=false;
    if(rc==ESP_ERR_NVS_NOT_FOUND)ok=true;
    else if(rc==ESP_OK&&size>0&&size<=4096){
      std::vector<char> buffer(size);rc=nvs_get_str(handle,key.c_str(),buffer.data(),&size);
      if(rc==ESP_OK){out.assign(buffer.data(),size-1);ok=true;}
    }
    xSemaphoreGive(lock);return ok;
  }
  bool write(const std::string& key,const std::string& value){
    if(!ready||value.size()>4095)return false;
    xSemaphoreTake(lock,portMAX_DELAY);
    bool ok=nvs_set_str(handle,key.c_str(),value.c_str())==ESP_OK&&nvs_commit(handle)==ESP_OK;
    xSemaphoreGive(lock);return ok;
  }
  bool erase(const std::string& key){
    if(!ready)return false;
    xSemaphoreTake(lock,portMAX_DELAY);auto rc=nvs_erase_key(handle,key.c_str());
    bool ok=rc==ESP_ERR_NVS_NOT_FOUND||(rc==ESP_OK&&nvs_commit(handle)==ESP_OK);
    xSemaphoreGive(lock);return ok;
  }
};
