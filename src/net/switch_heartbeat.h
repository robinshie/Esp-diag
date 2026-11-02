#pragma once
#include <Arduino.h>
#include <ETH.h>
#include <atomic>
#include <ping/ping_sock.h>
#include "../bdi/switch_heartbeat_bdi.h"

// The IDF ping task only publishes one POD result. All BDI reasoning stays in loop().
class SwitchHeartbeat {
  struct Result {bool ok=false;uint32_t at=0,rtt=0;};
  Result result;
  std::atomic<bool> completed{false};
  esp_ping_handle_t session=nullptr;
  ip_addr_t sessionAddress{};
  std::string target;
  uint32_t revision=0,sessionRevision=0;
  bool requested=false,inFlight=false,enabled=false;
  static void success(esp_ping_handle_t handle,void* arg){
    auto& self=*static_cast<SwitchHeartbeat*>(arg);ip_addr_t source{};uint32_t rtt=0;
    bool ok=esp_ping_get_profile(handle,ESP_PING_PROF_IPADDR,&source,sizeof(source))==ESP_OK&&ip_addr_cmp(&source,&self.sessionAddress);
    esp_ping_get_profile(handle,ESP_PING_PROF_TIMEGAP,&rtt,sizeof(rtt));
    self.result.ok=ok;self.result.at=millis();self.result.rtt=rtt;
  }
  static void timeout(esp_ping_handle_t,void* arg){
    auto& self=*static_cast<SwitchHeartbeat*>(arg);self.result.ok=false;self.result.at=millis();self.result.rtt=0;
  }
  static void ended(esp_ping_handle_t,void* arg){static_cast<SwitchHeartbeat*>(arg)->completed.store(true,std::memory_order_release);}
  void report(uint64_t now){
    const auto& state=bdi.evidence;
    bdi_output("[BDI][HEARTBEAT] target="+target+" transport=ICMP state="+state.name()+
      " rtt_ms="+std::to_string(state.rtt)+" misses="+std::to_string(state.misses)+
      " successes="+std::to_string(state.successes)+" reason="+state.reason+" observed_ms="+std::to_string(now));
  }
  void error(uint64_t now){bdi.probeError();report(now);}
public:
  SwitchHeartbeatBdi bdi;
  SwitchHeartbeat(){bdi.requestPing=[this](){requested=true;};}
  void configure(const std::string& host,bool active){
    ++revision;target=host;enabled=active;requested=false;bdi.reset(host);
    // An old one-shot completes within its timeout; discard by revision before reuse.
    if(session&&!inFlight){esp_ping_delete_session(session);session=nullptr;}
  }
  void tick(uint64_t now,bool sshConnected){
    if(inFlight&&completed.load(std::memory_order_acquire)){
      inFlight=false;
      if(sessionRevision==revision&&enabled){
        uint32_t age=uint32_t(millis()-result.at);
        if(age<=HeartbeatState::FreshMs){bdi.observe(result.ok,now>=age?now-age:0,result.rtt,result.ok?"ECHO_REPLY":"PING_TIMEOUT");report(now);}
        else error(now);
      }
    }
    if(session&&sessionRevision!=revision&&!inFlight){esp_ping_delete_session(session);session=nullptr;}
    if(!enabled)return;
    bdi.tick(now,sshConnected);
    if(!requested||inFlight)return;
    requested=false;
    if(!ETH.linkUp()||uint32_t(ETH.localIP())==0){bdi.observe(false,now,0,"LOCAL_LINK_DOWN");report(now);return;}
    if(!session){
      esp_ping_config_t config=ESP_PING_DEFAULT_CONFIG();
      config.count=1;config.interval_ms=0;config.timeout_ms=1000;config.data_size=32;
      int index=ETH.impl_index();
      if(index<=0||!ipaddr_aton(target.c_str(),&config.target_addr)){error(now);return;}
      config.interface=static_cast<uint32_t>(index);sessionAddress=config.target_addr;
      esp_ping_callbacks_t callbacks{};callbacks.cb_args=this;callbacks.on_ping_success=success;callbacks.on_ping_timeout=timeout;callbacks.on_ping_end=ended;
      if(esp_ping_new_session(&config,&callbacks,&session)!=ESP_OK){session=nullptr;error(now);return;}
      sessionRevision=revision;
    }
    completed.store(false,std::memory_order_relaxed);result=Result();inFlight=true;
    if(esp_ping_start(session)!=ESP_OK){inFlight=false;esp_ping_delete_session(session);session=nullptr;error(now);}
  }
  const char* stateName() const{return enabled?bdi.evidence.name():"disabled";}
  String status() const{
    return String("heartbeat_state=")+stateName()+"&heartbeat_rtt_ms="+String(bdi.evidence.rtt)+
      "&heartbeat_misses="+String(bdi.evidence.misses)+"&heartbeat_reason="+bdi.evidence.reason;
  }
};
