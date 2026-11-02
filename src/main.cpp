#include <Arduino.h>
#include <esp_system.h>
#include "config/firmware_mode.h"
#include "common/firmware_log.h"
#include "device/network_portal.h"
#include "net/ethernet_mqtt.h"
#include "net/switch_ssh.h"
#include "net/switch_heartbeat.h"
#include "storage/local_journal.h"
#include "ai/tiny_log_service.h"
#include "security/ssh_trust_store.h"
#include "h3c/runtime.h"
#include "h3c/action_ledger.h"
#include "ai/device_analysis.h"

std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
std::function<void(const std::string&)> firmware::output;
namespace {
NetworkPortal networkPortal;
EthernetMqtt ethernetMqtt;
SwitchSsh switchSsh;
SshTrustStore sshTrust;
std::string bootId;
SwitchHeartbeat switchHeartbeat;
LocalJournal sdJournal;
h3c::Runtime h3cRuntime;
h3c::ActionLedger actionLedger;
NvsTextStore actionStore;
DeviceAnalysis deviceAnalysis;
std::string endpoint(){const auto& profile=networkPortal.switchConfig();return SshHostKey::endpoint(profile.host,profile.port);}
std::string deviceExtraStatus(){return "\"bdi\":"+h3cRuntime.json()+",\"analysis\":"+deviceAnalysis.json()+",\"mqtt\":{\"state\":"+journal::quote(ethernetMqtt.stateName())+",\"enabled\":"+(networkPortal.mqttConfig().enabled?"true":"false")+"}";}
void configureH3c(){
  const auto& profile=networkPortal.switchConfig();actionLedger.disconnected();deviceAnalysis.reset();
  if(profile.host!="192.168.10.2"||profile.port!=22)actionStore.write("site-revoked","1");
  h3cRuntime.configure(profile.host,networkPortal.switchRevision(),bootId,profile.transport=="shell",profile.port);
}
unsigned mqttRevision=0, switchRevision=0;
uint64_t monotonic_ms = 0;
uint32_t previous_ms = 0;
}
void setup() {
  char boot[33];
  snprintf(boot,sizeof(boot),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
  bootId=boot;
  sdJournal.begin(boot);
  ethernetMqtt.attachJournal(sdJournal);
  firmware::output=[](const std::string& line){
    sdJournal.append(line);
  };
  bdi_output=firmware::println;
  firmware::printf("[BDI][BOOT] boot=%s transport=%s log_source=%s ai_enabled=%u",boot,"ETH","esp_native",ESP_LOG_AI_ENABLED);
  firmware::printf("[MEMORY] psram_bytes=%u free_psram=%u free_heap=%u",(unsigned)ESP.getPsramSize(),(unsigned)ESP.getFreePsram(),(unsigned)ESP.getFreeHeap());
  networkPortal.begin();
  sshTrust.begin();
  switchSsh.setHostKeyVerifier([](const std::string& host,unsigned port,const std::string& observed,const std::string& legacy){return sshTrust.verify(host,port,observed,legacy);});
  switchSsh.begin(networkPortal.switchConfig());
  networkPortal.setSwitchKey([](){
    const auto& profile=networkPortal.switchConfig();
    return String(("sw_host_key_state="+std::string(switchSsh.hostKeyState())+"&sw_fingerprint="+switchcli::encode(sshTrust.fingerprint(profile.host,profile.port))).c_str());
  },[](){const auto& profile=networkPortal.switchConfig();return sshTrust.forget(profile.host,profile.port);});
  actionStore.begin("h3c-actions");
  actionLedger.read=[](const std::string& key,std::string& value){return actionStore.read(key,value);};
  actionLedger.write=[](const std::string& key,const std::string& value){return actionStore.write(key,value);};
  actionLedger.erase=[](const std::string& key){return actionStore.erase(key);};
  h3cRuntime.connectionLost=[](){actionLedger.disconnected();};
  h3cRuntime.log=firmware::println;
  h3cRuntime.submitRead=[](const std::string& id,const std::string& command){return switchSsh.submit(id,command);};
  h3cRuntime.submitIsolation=[](const std::string& id,const std::string& port){return switchSsh.submitIsolation(id,port);};
  h3cRuntime.resultReady=[](const std::string& id){return switchSsh.resultReady(id);};
  h3cRuntime.result=[](const std::string& id){return switchSsh.takeResult(id);};
  h3cRuntime.claimIntent=[](const h3c::ActionPlan& plan,const std::string& incident){return actionLedger.claim(endpoint(),plan,incident);};
  h3cRuntime.alreadyClaimed=[](const std::string& port){return actionLedger.alreadyClaimed(endpoint(),port);};
  h3cRuntime.observeLedger=[](const h3c::Snapshot& snapshot,const h3c::Policy& policy,uint64_t now){
    for(const auto& entry:snapshot.interfaces.ports)if(actionLedger.observeHealthy(endpoint(),entry.first,snapshot,policy,now))
      firmware::println("[BDI][LEDGER] port="+entry.first+" state=healthy_incident_cleared");
  };
  deviceAnalysis.save=[](const std::string& json){return sdJournal.appendAnalysis(json);};
  h3cRuntime.analyse=[](const std::string& text,uint64_t at,uint64_t cycle){deviceAnalysis.observe(text,at,cycle);};
  std::string bindingRevoked;
  if(actionStore.read("site-revoked",bindingRevoked)&&bindingRevoked.empty())
    h3cRuntime.bindManagementProfile("192.168.10.2",22,"GigabitEthernet1/0/1");
  configureH3c();
  networkPortal.setDeviceExtraStatus([](){return String(("\"firmware_version\":\"native-bdi-20261102-r3\",\"boot\":"+journal::quote(bootId)+",\"ssh_host_key_state\":"+journal::quote(switchSsh.hostKeyState())+","+deviceExtraStatus()).c_str());});
  networkPortal.setSwitchStatus([](){return String(switchSsh.stateName());});
  networkPortal.setSwitchCommands(
    [](const String& id,const String& command){return String(switchSsh.submit(id.c_str(),command.c_str()).c_str());},
    [](const String& id){return String(switchSsh.result(id.c_str()).fields().c_str());});
  ethernetMqtt.configure(networkPortal.mqttConfig());
  networkPortal.setStorageStatus([](){return sdJournal.status();});
  networkPortal.setStorageViewer([](const String& file,uint32_t offset,uint32_t job){return sdJournal.read(file,offset,job);});
  networkPortal.setLogFiles([](uint32_t before){return sdJournal.filesPage(before);});
  networkPortal.setViewerOpened([](){sdJournal.flush();});
#if ESP_LOG_AI_ENABLED
  networkPortal.setLogExplainer(tinyLogReply);
  firmware::printf("[LOG-AI] model=%s inference=ESP32 display_only=1",tinylog::VERSION);
#endif
  switchHeartbeat.configure(networkPortal.switchConfig().host,networkPortal.switchConfig().protocol=="ssh");
  networkPortal.setHeartbeatStatus([](){return switchHeartbeat.status();});
}
void loop() {
  networkPortal.tick();
  if(switchRevision!=networkPortal.switchRevision()){
    switchRevision=networkPortal.switchRevision();switchSsh.configure(networkPortal.switchConfig(),true);
    configureH3c();
    switchHeartbeat.configure(networkPortal.switchConfig().host,networkPortal.switchConfig().protocol=="ssh");
  }
  if(mqttRevision!=networkPortal.mqttRevision()){mqttRevision=networkPortal.mqttRevision();ethernetMqtt.configure(networkPortal.mqttConfig());}
  ethernetMqtt.tick();
  uint32_t current = millis();
  monotonic_ms += static_cast<uint32_t>(current - previous_ms);
  previous_ms = current;
  switchHeartbeat.tick(monotonic_ms,switchSsh.connected());
  const bool connected=switchSsh.connected();
  h3cRuntime.tick(monotonic_ms,connected,std::string(switchSsh.hostKeyState())=="trusted");
  static uint64_t last_status = 0;
  if (monotonic_ms - last_status >= 5000) {
    last_status = monotonic_ms;
    const auto& hb=switchHeartbeat.bdi.evidence;
    const std::string telemetry="{\"schema_version\":1,\"source\":\"esp_native\",\"boot\":"+journal::quote(bootId)+",\"uptime_ms\":"+std::to_string(monotonic_ms)+",\"ip\":"+journal::quote(ETH.localIP().toString().c_str())+",\"ssh_state\":"+journal::quote(switchSsh.stateName())+",\"heartbeat\":{\"state\":"+journal::quote(switchHeartbeat.stateName())+",\"rtt_ms\":"+std::to_string(hb.rtt)+",\"misses\":"+std::to_string(hb.misses)+"},"+deviceExtraStatus()+"}";
    ethernetMqtt.publishStatus(telemetry);
    firmware::printf("[SSH][STATUS] state=%s ip=%s link=%s target=%s:%u free_heap=%u\n",switchSsh.stateName(),ETH.localIP().toString().c_str(),ETH.linkUp()?"up":"down",networkPortal.switchConfig().host.c_str(),networkPortal.switchConfig().port,(unsigned)ESP.getFreeHeap());
    firmware::printf("[BDI][STATUS] reasoning_cycle=ACTIVE transport=ETH uptime_ms=%llu devices=1 free_heap=%u sd_state=%s log_source=esp_native",
        (unsigned long long)monotonic_ms,(unsigned)ESP.getFreeHeap(),sdJournal.stateName());
  }
  delay(1);
}
