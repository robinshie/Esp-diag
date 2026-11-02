#pragma once
#include <Arduino.h>
#include <ETH.h>
#include <atomic>
#include <mqtt_client.h>
#include <net/if.h>
#include "../common/firmware_log.h"
#include "../config/mqtt_settings.h"
#include "mqtt_report_cursor.h"
#include "../storage/local_journal.h"

// Optional telemetry only. All client/settings operations run on Arduino loop;
// the IDF event task writes atomics and never touches TF or user callbacks.
class EthernetMqtt {
  MqttSettings settings;
  LocalJournal* journal=nullptr;
  esp_mqtt_client_handle_t client=nullptr;
  struct ifreq interfaceName{};
  std::atomic<bool> connected{false},announce{false},error{false};
  std::atomic<const char*> visibleState{"disabled"};
  std::atomic<int> publishedId{-1};
  mqtt_report::Flight flight;
  String base,clientId;
  uint32_t lastAttempt=0,sentAt=0,flightGeneration=0;
  std::atomic<unsigned> dropped{0};
  bool lastConnected=false;
  static void event(void* arg,esp_event_base_t,int32_t id,void* data){
    auto self=static_cast<EthernetMqtt*>(arg);
    if(id==MQTT_EVENT_CONNECTED){self->connected=true;self->announce=true;}
    else if(id==MQTT_EVENT_DISCONNECTED)self->connected=false;
    else if(id==MQTT_EVENT_ERROR)self->error=true;
    else if(id==MQTT_EVENT_PUBLISHED&&data)self->publishedId=static_cast<esp_mqtt_event_handle_t>(data)->msg_id;
  }
  void identity(){
    if(!clientId.isEmpty())return;
    char out[24];
    snprintf(out,sizeof(out),"esp-%012llx",static_cast<unsigned long long>(ESP.getEfuseMac())&0xffffffffffffULL);clientId=out;
  }
  void configureJournal(){
    if(!journal)return;
    identity();
    journal->configureMqtt(settings.enabled?settings.host+":"+std::to_string(settings.port)+"/"+settings.prefix+"/"+clientId.c_str():"");
  }
  void stop(){
    if(client){esp_mqtt_client_stop(client);esp_mqtt_client_destroy(client);client=nullptr;}
    connected=false;announce=false;publishedId=-1;flight.reset();
  }
  int send(const char* suffix,const char* payload,size_t size,int qos,bool retained=false){
    if(!client||!connected||size>LocalReport::MaxWireBytes||esp_mqtt_client_get_outbox_size(client)+size+128>98304){++dropped;return -1;}
    String topic=base+suffix;
    int id=esp_mqtt_client_enqueue(client,topic.c_str(),payload,size,qos,retained,true);if(id<0)++dropped;return id;
  }
  void start(){
    String name=ETH.impl_name();if(name.isEmpty())return;
    memset(&interfaceName,0,sizeof(interfaceName));strlcpy(interfaceName.ifr_name,name.c_str(),sizeof(interfaceName.ifr_name));
    identity();base=String(settings.prefix.c_str())+"/"+clientId;String will=base+"/availability";
    esp_mqtt_client_config_t config{};
    config.broker.address.hostname=settings.host.c_str();config.broker.address.port=settings.port;
    config.broker.address.transport=MQTT_TRANSPORT_OVER_TCP;config.credentials.client_id=clientId.c_str();
    if(!settings.username.empty()){config.credentials.username=settings.username.c_str();config.credentials.authentication.password=settings.password.c_str();}
    config.session.keepalive=15;config.session.last_will.topic=will.c_str();config.session.last_will.msg="OFFLINE";
    config.session.last_will.qos=1;config.session.last_will.retain=1;config.network.if_name=&interfaceName;
    config.network.timeout_ms=2000;config.network.reconnect_timeout_ms=10000;config.buffer.size=2048;config.outbox.limit=98304;
    client=esp_mqtt_client_init(&config);
    if(!client){firmware::println("[MQTT] state=init_failed");return;}
    if(esp_mqtt_client_register_event(client,static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID),event,this)!=ESP_OK||esp_mqtt_client_start(client)!=ESP_OK){stop();firmware::println("[MQTT] state=start_failed");}
  }
public:
  static bool compiled(){return true;}
  void attachJournal(LocalJournal& source){journal=&source;configureJournal();}
  void configure(const MqttSettings& next){
    stop();settings=next;if(!settings.validate().empty())settings.enabled=false;
    visibleState=settings.enabled?"connecting":"disabled";
    lastAttempt=millis()-10000;configureJournal();
    firmware::printf("[MQTT] enabled=%u transport=ETH telemetry_only=1\n",settings.enabled);
  }
  // Compatibility for the lab log sink. Native evidence only leaves committed TF.
  void output(const std::string&){}
  bool publishStatus(const std::string& json){
    if(!settings.enabled||json.size()>16384)return false;
    // Status is replaceable. Only reports use QoS 1 / durable ACKs.
    return send("/status",json.data(),json.size(),0,true)>=0;
  }
  const char* stateName() const{return visibleState.load();}
  unsigned droppedCount() const{return dropped.load();}
  void tick(){
    if(!settings.enabled)return;
    if(!ETH.linkUp()||!ETH.hasIP()){visibleState="waiting_ethernet";if(client)stop();return;}
    visibleState=connected.load()?"connected":"connecting";
    if(!client&&uint32_t(millis()-lastAttempt)>=10000){lastAttempt=millis();start();}
    if(connected.load()!=lastConnected){lastConnected=connected.load();firmware::printf("[MQTT] state=%s\n",lastConnected?"connected":"disconnected");}
    if(error.exchange(false))firmware::println("[MQTT] state=connection_error");
    if(announce.exchange(false))send("/availability","ONLINE",6,0,true);
    const int ack=publishedId.exchange(-1);
    if(ack>=0){const auto sequence=flight.acknowledge(ack);if(sequence&&journal)journal->acknowledgeMqtt(sequence,flightGeneration);}
    // Expired RAM outbox resets without advancing TF, retaining report_id.
    if(flight.busy()&&uint32_t(millis()-sentAt)>=60000){stop();return;}
    if(connected&&journal&&!flight.busy()){
      std::string body;uint32_t seq,generation;
      if(journal->nextMqttReport(body,seq,generation)){int id=send("/report",body.data(),body.size(),1);if(id>=0){flight.sent(id,seq);flightGeneration=generation;sentAt=millis();}}
    }
  }
};
