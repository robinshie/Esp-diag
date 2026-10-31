#pragma once
#include <Arduino.h>
#include <ETH.h>
#include <atomic>
#include "mqtt_settings.h"
#ifndef ESP_MQTT_EXPERIMENT
#define ESP_MQTT_EXPERIMENT 0
#endif
#if ESP_MQTT_EXPERIMENT
#include <mqtt_client.h>
#include <net/if.h>
#endif

// Deliberately separate from production report topics and USB protocol input.
class EthernetMqtt {
  MqttSettings settings;
  uint32_t lastStatus=0, lastAttempt=0;
  unsigned dropped=0;
#if ESP_MQTT_EXPERIMENT
  esp_mqtt_client_handle_t client=nullptr;
  struct ifreq interfaceName{};
  std::atomic<bool> connected{false}, announce{false};
  String base;
  static void event(void* arg,esp_event_base_t,int32_t id,void*) {
    auto self=static_cast<EthernetMqtt*>(arg);
    if(id==MQTT_EVENT_CONNECTED){self->connected=true;self->announce=true;Serial.println("[MQTT-LAB] connected transport=ETH");}
    else if(id==MQTT_EVENT_DISCONNECTED){self->connected=false;Serial.println("[MQTT-LAB] disconnected");}
    else if(id==MQTT_EVENT_ERROR)Serial.println("[MQTT-LAB] connection error; check broker/auth/network");
  }
  void stop() {
    if(client){esp_mqtt_client_stop(client);esp_mqtt_client_destroy(client);client=nullptr;}
    connected=false;announce=false;
  }
  void send(const char* suffix,const char* payload,size_t size,bool retained=false) {
    if(!client||!connected||size>8192||esp_mqtt_client_get_outbox_size(client)+size+128>16384){++dropped;return;}
    String topic=base+suffix;
    if(esp_mqtt_client_enqueue(client,topic.c_str(),payload,size,1,retained,true)<0)++dropped;
  }
  void start() {
    String name=ETH.impl_name();
    if(name.isEmpty()){Serial.println("[MQTT-LAB] Ethernet interface unavailable");return;}
    memset(&interfaceName,0,sizeof(interfaceName));
    strlcpy(interfaceName.ifr_name,name.c_str(),sizeof(interfaceName.ifr_name));
    String id="esp-lab-"+String(uint32_t(ESP.getEfuseMac()),HEX);
    base=String(settings.prefix.c_str())+"/"+id;
    String will=base+"/availability";
    esp_mqtt_client_config_t config{};
    config.broker.address.hostname=settings.host.c_str();config.broker.address.port=settings.port;
    config.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;
    config.credentials.client_id=id.c_str();
    if(!settings.username.empty()){config.credentials.username=settings.username.c_str();config.credentials.authentication.password=settings.password.c_str();}
    config.session.keepalive=15;
    config.session.last_will.topic=will.c_str();config.session.last_will.msg="OFFLINE";
    config.session.last_will.qos=1;config.session.last_will.retain=1;
    config.network.if_name=&interfaceName;
    config.network.timeout_ms=2000;config.network.reconnect_timeout_ms=10000;
    config.buffer.size=1024;config.outbox.limit=16384;
    client=esp_mqtt_client_init(&config);
    if(!client){Serial.println("[MQTT-LAB] init failed");return;}
    if(esp_mqtt_client_register_event(client,static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),event,this)!=ESP_OK||esp_mqtt_client_start(client)!=ESP_OK){stop();Serial.println("[MQTT-LAB] start failed");}
  }
#endif
public:
  static bool compiled() {return ESP_MQTT_EXPERIMENT;}
  void configure(const MqttSettings& next) {
#if ESP_MQTT_EXPERIMENT
    stop();
#endif
    settings=next;lastAttempt=millis()-10000;
    Serial.printf("[MQTT-LAB] compiled=%u enabled=%u (credentials hidden)\n",compiled(),settings.enabled);
  }
  void output(const std::string& line) {
#if ESP_MQTT_EXPERIMENT
    if(settings.enabled)send("/serial",line.data(),line.size());
#endif
  }
  void tick() {
#if ESP_MQTT_EXPERIMENT
    if(!settings.enabled)return;
    if(!ETH.linkUp()||!ETH.hasIP()){if(client)stop();return;}
    if(!client&&uint32_t(millis()-lastAttempt)>=10000){lastAttempt=millis();start();}
    if(announce.exchange(false)){send("/availability","ONLINE",6,true);}
    if(connected&&uint32_t(millis()-lastStatus)>=5000){
      lastStatus=millis();char data[256];
      snprintf(data,sizeof(data),"{\"experiment\":true,\"transport\":\"ETH\",\"ip\":\"%s\",\"uptime_ms\":%lu,\"dropped\":%u,\"free_heap\":%u}",ETH.localIP().toString().c_str(),(unsigned long)millis(),dropped,(unsigned)ESP.getFreeHeap());
      send("/status",data,strlen(data));
    }
#endif
  }
};
