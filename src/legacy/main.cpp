#include <Arduino.h>
#include <esp_system.h>
#include "../config/firmware_mode.h"
#include "../common/firmware_log.h"
#if !ESP_STANDALONE
#include "cluster_runtime.h"
#include "sd_journal.h"
#endif
#include "../device/network_portal.h"
#include "../net/ethernet_mqtt.h"
#include "../net/switch_ssh.h"
#include "../adapters/h3c/device_adapter.h"
#include "../application/switch_runtime.h"
#include "../application/action_ledger.h"
#include "../bdi/strategies/loop_discovery.h"
#include "../bdi/strategies/loop_isolation.h"
#include "../config/site_policy.h"
#include "../net/switch_heartbeat.h"
#include "../bdi/strategies/heartbeat.h"
#include "../storage/local_journal.h"
#include "../ai/tiny_log_service.h"
#include "../security/ssh_trust_store.h"
#if ESP_STANDALONE
#include "../ai/device_analysis.h"
#endif

std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
std::function<void(const std::string&)> firmware::output;
namespace {
#if !ESP_STANDALONE
ClusterRuntime* runtime;
#endif
NetworkPortal networkPortal;
EthernetMqtt ethernetMqtt;
h3c::DeviceAdapter deviceAdapter;
SwitchSsh switchSsh{deviceAdapter};
SshTrustStore sshTrust;
std::string bootId;
switchbdi::HeartbeatStrategy heartbeatStrategy;
SwitchHeartbeat switchHeartbeat{heartbeatStrategy};
#if ESP_STANDALONE
LocalJournal sdJournal;
switchbdi::LoopDiscoveryStrategy loopDiscovery;
switchbdi::LoopIsolationStrategy loopIsolation;
switchbdi::StrategySet strategies{&loopDiscovery,&loopIsolation};
switchapp::SwitchRuntime switchRuntime{deviceAdapter,strategies,site::switchPolicy()};
switchapp::ActionLedger actionLedger;
NvsTextStore actionStore;
DeviceAnalysis deviceAnalysis;
std::string endpoint(){const auto& profile=networkPortal.switchConfig();return SshHostKey::endpoint(profile.host,profile.port);}
std::string deviceExtraStatus(){return "\"bdi\":"+switchRuntime.json()+",\"analysis\":"+deviceAnalysis.json()+",\"mqtt\":{\"state\":"+journal::quote(ethernetMqtt.stateName())+",\"enabled\":"+(networkPortal.mqttConfig().enabled?"true":"false")+"}";}
void configureSwitch(){
  const auto& profile=networkPortal.switchConfig();actionLedger.disconnected();deviceAnalysis.reset();
  switchRuntime.configure(profile.host,networkPortal.switchRevision(),bootId,profile.transport=="shell");
}
#else
SdJournal sdJournal;
#endif
unsigned mqttRevision=0, switchRevision=0;
#if !ESP_STANDALONE
std::string usb_line, usbSshId;
switchcli::Result usbSshResult;
size_t usbSshOffset=0;
bool usbSshStarted=false;
bool sshRequest(const std::string& line){
  if(line.compare(0,8,"H3C SSH ")!=0)return false;
  cluster::Fields fields;bool valid=cluster::parse(line,fields)&&fields.size()==2;
  auto id=cluster::get(fields,"request_id"),name=cluster::get(fields,"command");
  std::string result=!valid||!switchcli::identifier(id)?"invalid_request":!usbSshId.empty()?"busy":switchSsh.submit(id,name);
  Serial.printf("[SSH][REQUEST] request_id=%s command=%s state=%s\n",switchcli::identifier(id)?id.c_str():"invalid",deviceAdapter.readCommand(name)?name.c_str():"invalid",result.c_str());
  if(result=="queued"||result=="running"||switchSsh.resultReady(id)){usbSshId=id;usbSshStarted=false;usbSshOffset=0;}
  return true;
}
void sshReplyTick(){
  if(usbSshId.empty())return;
  if(!usbSshStarted){
    if(!switchSsh.resultReady(usbSshId))return;
    usbSshResult=switchSsh.result(usbSshId);usbSshStarted=true;
    Serial.printf("[SSH][RESULT] request_id=%s command=%s target=%s state=%s truncated=%u\n",usbSshResult.id.c_str(),usbSshResult.command.c_str(),usbSshResult.target.c_str(),usbSshResult.state.c_str(),usbSshResult.truncated?1:0);
    return;
  }
  if(usbSshOffset<usbSshResult.output.size()){
    auto chunk=usbSshResult.output.substr(usbSshOffset,128);usbSshOffset+=chunk.size();
    Serial.printf("[SSH][DATA] request_id=%s data=%s\n",usbSshId.c_str(),switchcli::encode(chunk).c_str());return;
  }
  Serial.printf("[SSH][END] request_id=%s\n",usbSshId.c_str());usbSshId.clear();usbSshResult=switchcli::Result();
}
bool discard_frame = false;
#endif
uint64_t monotonic_ms = 0;
uint32_t previous_ms = 0;
}
void setup() {
#if !ESP_STANDALONE
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  delay(3000);
#endif
  char boot[33];
  snprintf(boot,sizeof(boot),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
#if ESP_STANDALONE
  bootId=boot;
  sdJournal.begin(boot);
  ethernetMqtt.attachJournal(sdJournal);
#else
  sdJournal.begin(boot,false);
#endif
  firmware::output=[](const std::string& line){
#if ESP_STANDALONE
    sdJournal.append(line);
#else
    Serial.println(line.c_str());
#endif
  };
  bdi_output=firmware::println;
  firmware::printf("[BDI][BOOT] boot=%s transport=%s log_source=%s ai_enabled=%u",boot,ESP_STANDALONE?"ETH":"USB",ESP_STANDALONE?"esp_native":"database",ESP_LOG_AI_ENABLED);
  firmware::printf("[MEMORY] psram_bytes=%u free_psram=%u free_heap=%u",(unsigned)ESP.getPsramSize(),(unsigned)ESP.getFreePsram(),(unsigned)ESP.getFreeHeap());
  networkPortal.begin();
  sshTrust.begin();
  switchSsh.setHostKeyVerifier([](const std::string& host,unsigned port,const std::string& observed,const std::string& legacy){return sshTrust.verify(host,port,observed,legacy);});
  switchSsh.begin(networkPortal.switchConfig());
  networkPortal.setSwitchKey([](){
    const auto& profile=networkPortal.switchConfig();
    return String(("sw_host_key_state="+std::string(switchSsh.hostKeyState())+"&sw_fingerprint="+switchcli::encode(sshTrust.fingerprint(profile.host,profile.port))).c_str());
  },[](){const auto& profile=networkPortal.switchConfig();return sshTrust.forget(profile.host,profile.port);});
#if ESP_STANDALONE
  actionStore.begin("h3c-actions");
  actionLedger.read=[](const std::string& key,std::string& value){return actionStore.read(key,value);};
  actionLedger.write=[](const std::string& key,const std::string& value){return actionStore.write(key,value);};
  actionLedger.erase=[](const std::string& key){return actionStore.erase(key);};
  switchRuntime.log=firmware::println;
  switchRuntime.submitRead=[](const std::string& id,const std::string& command){return switchSsh.submit(id,command);};
  switchRuntime.submitIsolation=[](const std::string& id,const std::string& port){return switchSsh.submitIsolation(id,port);};
  switchRuntime.resultReady=[](const std::string& id){return switchSsh.resultReady(id);};
  switchRuntime.result=[](const std::string& id){return switchSsh.result(id);};
  switchRuntime.claimIntent=[](const switchmodel::ActionPlan& plan,const std::string& incident){return actionLedger.claim(endpoint(),plan,incident);};
  switchRuntime.alreadyClaimed=[](const std::string& port){return actionLedger.alreadyClaimed(endpoint(),port);};
  switchRuntime.observeLedger=[](const switchmodel::Snapshot& snapshot,const switchmodel::Policy& policy,uint64_t now){
    for(const auto& entry:snapshot.interfaces.ports)if(actionLedger.observeHealthy(endpoint(),entry.first,snapshot,policy,now))
      firmware::println("[BDI][LEDGER] port="+entry.first+" state=healthy_incident_cleared");
  };
  deviceAnalysis.save=[](const std::string& json){return sdJournal.appendAnalysis(json);};
  switchRuntime.analyse=[](const std::string& text,uint64_t at,uint64_t cycle){deviceAnalysis.observe(text,at,cycle);};
  configureSwitch();
  networkPortal.setDeviceExtraStatus([](){return String(deviceExtraStatus().c_str());});
#endif
  networkPortal.setSwitchStatus([](){return String(switchSsh.stateName());});
  networkPortal.setSwitchCommands(
    [](const String& id,const String& command){return String(switchSsh.submit(id.c_str(),command.c_str()).c_str());},
    [](const String& id){return String(switchSsh.result(id.c_str()).fields().c_str());});
  ethernetMqtt.configure(networkPortal.mqttConfig());
  networkPortal.setStorageStatus([](){return sdJournal.status();});
  networkPortal.setStorageViewer([](const String& file,uint32_t offset,uint32_t job){return sdJournal.read(
#if ESP_STANDALONE
    file,
#else
    String("db:")+file,
#endif
    offset,job);});
  networkPortal.setLogFiles([](uint32_t before){return sdJournal.
#if ESP_STANDALONE
    filesPage(before);
#else
    databaseFiles(before);
#endif
  });
#if ESP_STANDALONE
  networkPortal.setViewerOpened([](){sdJournal.flush();});
#endif
#if ESP_LOG_AI_ENABLED
  networkPortal.setLogExplainer(tinyLogReply);
  firmware::printf("[LOG-AI] model=%s inference=ESP32 display_only=1",tinylog::VERSION);
#endif
  switchHeartbeat.configure(networkPortal.switchConfig().host,networkPortal.switchConfig().protocol=="ssh");
  networkPortal.setHeartbeatStatus([](){return switchHeartbeat.status();});
#if !ESP_STANDALONE
  runtime = new ClusterRuntime(boot);
  runtime->model.output = bdi_output;
#endif
}
void loop() {
  networkPortal.tick();
  if(switchRevision!=networkPortal.switchRevision()){
    switchRevision=networkPortal.switchRevision();switchSsh.configure(networkPortal.switchConfig(),true);
#if ESP_STANDALONE
    configureSwitch();
#endif
    switchHeartbeat.configure(networkPortal.switchConfig().host,networkPortal.switchConfig().protocol=="ssh");
  }
#if !ESP_STANDALONE
  sdJournal.tickCopy();
#endif
  if(mqttRevision!=networkPortal.mqttRevision()){mqttRevision=networkPortal.mqttRevision();ethernetMqtt.configure(networkPortal.mqttConfig());}
  ethernetMqtt.tick();
  uint32_t current = millis();
  monotonic_ms += static_cast<uint32_t>(current - previous_ms);
  previous_ms = current;
#if !ESP_STANDALONE
  runtime->model.tick(monotonic_ms);
#endif
  switchHeartbeat.tick(monotonic_ms,switchSsh.connected());
#if ESP_STANDALONE
  static bool lastConnected=false;
  const bool connected=switchSsh.connected();
  if(lastConnected&&!connected)actionLedger.disconnected();lastConnected=connected;
  switchRuntime.tick(monotonic_ms,connected,std::string(switchSsh.hostKeyState())=="trusted");
#endif
#if !ESP_STANDALONE
  sshReplyTick();
  // Bound bytes per iteration so a continuous event stream cannot starve timers.
  for (unsigned count = 0; count < 1024 && Serial.available(); ++count) {
    char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      if(!discard_frame&&sdJournal.receiveCopy(usb_line)){usb_line.clear();discard_frame=false;continue;}
#if ESP_LOG_AI_ENABLED
      // Lab diagnostics bypass BDI entirely: inference cannot dispatch an action.
      if(!discard_frame&&usb_line.compare(0,7,"H3C AI ")==0){
        Serial.print("[LOG-AI] ");Serial.println(tinyLogReply(String(usb_line.c_str()+7)));
        usb_line.clear();discard_frame=false;continue;
      }
#endif
      if (!discard_frame&&!sshRequest(usb_line))runtime->model.receive(usb_line);
      usb_line.clear(); discard_frame = false;
    } else if (value != '\r' && !discard_frame) {
      if (usb_line.size() >= 511) {
        usb_line.clear(); discard_frame = true;
        runtime->model.reject("FRAME_TOO_LONG");
      } else usb_line += value;
    }
  }
#endif
  static uint64_t last_status = 0;
  if (monotonic_ms - last_status >= 5000) {
    last_status = monotonic_ms;
#if ESP_STANDALONE
    const auto& hb=switchHeartbeat.bdi.state();
    const std::string telemetry="{\"schema_version\":1,\"source\":\"esp_native\",\"boot\":"+journal::quote(bootId)+",\"uptime_ms\":"+std::to_string(monotonic_ms)+",\"ip\":"+journal::quote(ETH.localIP().toString().c_str())+",\"ssh_state\":"+journal::quote(switchSsh.stateName())+",\"heartbeat\":{\"state\":"+journal::quote(switchHeartbeat.stateName())+",\"rtt_ms\":"+std::to_string(hb.rtt)+",\"misses\":"+std::to_string(hb.misses)+"},"+deviceExtraStatus()+"}";
    ethernetMqtt.publishStatus(telemetry);
#endif
    firmware::printf("[SSH][STATUS] state=%s ip=%s link=%s target=%s:%u free_heap=%u\n",switchSsh.stateName(),ETH.localIP().toString().c_str(),ETH.linkUp()?"up":"down",networkPortal.switchConfig().host.c_str(),networkPortal.switchConfig().port,(unsigned)ESP.getFreeHeap());
#if ESP_STANDALONE
    firmware::printf("[BDI][STATUS] reasoning_cycle=ACTIVE transport=ETH uptime_ms=%llu devices=1 free_heap=%u sd_state=%s log_source=esp_native",
        (unsigned long long)monotonic_ms,(unsigned)ESP.getFreeHeap(),sdJournal.stateName());
#else
    firmware::printf("[BDI][STATUS] reasoning_cycle=ACTIVE transport=USB uptime_ms=%llu devices=%u rejected=%u free_heap=%u sd_state=%s archive_copy=%u\r\n",
        (unsigned long long)monotonic_ms, (unsigned)runtime->model.devices.size(),
        runtime->model.rejected, (unsigned)ESP.getFreeHeap(),sdJournal.stateName(),sdJournal.copySupported());
#endif
  }
  delay(1);
}
