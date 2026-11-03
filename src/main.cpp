#include <Arduino.h>
#include <esp_system.h>
#include "config/firmware_mode.h"
#include "common/firmware_log.h"
#include "device/network_portal.h"
#include "net/ethernet_mqtt.h"
#include "net/switch_communication.h"
#include "adapters/h3c/device_adapter.h"
#include "application/switch_runtime.h"
#include "application/action_ledger.h"
#if ESP_STORM_ACCEPTANCE
#include "application/storm_acceptance.h"
#endif
#if ESP_PORT_EXERCISE
#include "application/port_exercise.h"
#if ESP_PORT_EXERCISE_BDI
#include "bdi/port_acceptance.h"
#endif
#endif
#include "bdi/strategies/loop_discovery.h"
#include "bdi/strategies/loop_isolation.h"
#include "bdi/strategies/storm_isolation.h"
#include "config/site_policy.h"
#include "net/switch_heartbeat.h"
#include "bdi/strategies/heartbeat.h"
#include "storage/local_journal.h"
#include "ai/tiny_log_service.h"
#include "security/ssh_trust_store.h"
#include "ai/device_analysis.h"

std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
std::function<void(const std::string&)> firmware::output;
namespace {
NetworkPortal networkPortal;
EthernetMqtt ethernetMqtt;
h3c::DeviceAdapter deviceAdapter;
SwitchSsh switchSsh{deviceAdapter};
SwitchCommunication switchCommunication{switchSsh,deviceAdapter};
switchtransport::ICommandTransport& switchConnection=switchCommunication.connection();
SshTrustStore sshTrust;
std::string bootId;
switchbdi::HeartbeatStrategy heartbeatStrategy;
SwitchHeartbeat switchHeartbeat{heartbeatStrategy};
LocalJournal sdJournal;
switchbdi::LoopDiscoveryStrategy loopDiscovery;
switchbdi::LoopIsolationStrategy loopIsolation;
switchbdi::StormIsolationStrategy stormIsolation;
#if ESP_STORM_ACCEPTANCE
switchbdi::StrategySet strategies{&loopDiscovery,&stormIsolation};
switchapp::StormAcceptance stormAcceptance;
bool stormLabAdmitted=false;
#elif ESP_LOOP_AUTO
switchbdi::StrategySet strategies{&loopDiscovery,&loopIsolation};
#else
switchbdi::StrategySet strategies{&loopDiscovery,&loopIsolation,&stormIsolation};
#endif
switchapp::SwitchRuntime switchRuntime{deviceAdapter,strategies,site::switchPolicy()};
switchapp::ActionLedger actionLedger;
NvsTextStore actionStore;
DeviceAnalysis deviceAnalysis;
#if ESP_PORT_EXERCISE
static_assert(ESP_BDI_READ_ONLY==1,"Port exercise requires ordinary BDI writes disabled");
switchapp::PortExercise portExercise{deviceAdapter,switchConnection};
#if ESP_PORT_SHUTDOWN_ONLY
const char* firmwareVersion="native-bdi-20261103-r19-port2-down";
#elif ESP_PORT_EXERCISE_BDI
switchbdi::PortAcceptance portAcceptance;
const char* firmwareVersion="native-bdi-20261103-r17-bdi-port23";
#else
const char* firmwareVersion="native-bdi-20261103-r16-port23-exercise";
#endif
#elif ESP_STORM_ACCEPTANCE
const char* firmwareVersion="native-bdi-20261103-r18-storm2-lab";
#elif ESP_LOOP_AUTO
static_assert(!ESP_BDI_READ_ONLY&&!ESP_PORT_EXERCISE&&!ESP_STORM_ACCEPTANCE&&ESP_SWITCH_CHANNEL==0,
  "Automatic loop isolation requires a dedicated writable SSH build");
const char* firmwareVersion="native-bdi-20261103-r20-loop-auto";
#else
const char* firmwareVersion="native-bdi-20261103-r15-faultrecovery";
#endif
std::string exerciseFields(){
#if ESP_PORT_EXERCISE
#if ESP_PORT_SHUTDOWN_ONLY
  return ",\"port_shutdown\":"+portExercise.json();
#elif ESP_PORT_EXERCISE_BDI
  return ",\"acceptance_bdi\":{\"goal\":\"verify_port_roundtrip\",\"decisions\":"+std::to_string(portAcceptance.decisions())+",\"scope\":\"operator_acceptance_only\"},\"port_exercise\":"+portExercise.json();
#else
  return ",\"port_exercise\":"+portExercise.json();
#endif
#elif ESP_STORM_ACCEPTANCE
  return ",\"storm_acceptance\":"+stormAcceptance.json();
#elif ESP_LOOP_AUTO
  return ",\"loop_automation\":{\"configured\":true,\"scope\":\"all_business_access_ports\",\"allowed_ge_range\":\"1/0/2-48\",\"allowed_xge_range\":\"1/0/49-52\",\"management_port\":\"GigabitEthernet1/0/1\",\"requires_explicit_loop_evidence\":true,\"automatic_restore\":false}";
#else
  return "";
#endif
}
std::string endpoint(){const auto& profile=networkPortal.switchConfig();return SshHostKey::endpoint(profile.host,profile.port);}
std::string deviceExtraStatus(){return "\"management\":{\"policy\":"+journal::quote(switchCommunication.policy())+",\"transport\":"+journal::quote(switchConnection.kind())+",\"state\":"+journal::quote(switchConnection.stateName())+",\"ssh_state\":"+journal::quote(switchSsh.stateName())+",\"console_state\":"+journal::quote(switchCommunication.consoleState())+",\"session_accepted\":"+(switchConnection.sessionAccepted()?"true":"false")+"},"+"\"bdi\":"+switchRuntime.json()+",\"analysis\":"+deviceAnalysis.json()+",\"mqtt\":{\"state\":"+journal::quote(ethernetMqtt.stateName())+",\"enabled\":"+(networkPortal.mqttConfig().enabled?"true":"false")+"}";}
void configureSwitch(){
#if ESP_STORM_ACCEPTANCE
  stormAcceptance.configurationChanged();
#endif
  const auto& profile=networkPortal.switchConfig();actionLedger.disconnected();deviceAnalysis.reset();
  if(profile.host!="192.168.10.2"||profile.port!=22)actionStore.write("site-revoked","1");
  switchRuntime.configure(profile.host,networkPortal.switchRevision(),bootId,std::string(switchConnection.kind())!="ssh"||profile.transport=="shell",switchCommunication.endpointPort(profile.port),switchConnection.kind(),switchCommunication.identity());
#if ESP_PORT_EXERCISE
  portExercise.configure(profile.host,profile.port,profile.protocol,profile.transport);
#endif
}
unsigned mqttRevision=0, switchRevision=0;
uint64_t communicationGeneration=0;
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
  firmware::printf("[BDI][BOOT] boot=%s transport=%s log_source=%s ai_enabled=%u",boot,switchConnection.kind(),"esp_native",ESP_LOG_AI_ENABLED);
  const auto resetReason=esp_reset_reason();
  firmware::printf("[SYSTEM][RESET] reason=%d power_on=%u brownout=%u panic=%u task_watchdog=%u interrupt_watchdog=%u",int(resetReason),resetReason==ESP_RST_POWERON,resetReason==ESP_RST_BROWNOUT,resetReason==ESP_RST_PANIC,resetReason==ESP_RST_TASK_WDT,resetReason==ESP_RST_INT_WDT);
  firmware::printf("[MEMORY] psram_bytes=%u free_psram=%u free_heap=%u",(unsigned)ESP.getPsramSize(),(unsigned)ESP.getFreePsram(),(unsigned)ESP.getFreeHeap());
  networkPortal.begin();
  sshTrust.begin();
  switchSsh.setHostKeyVerifier([](const std::string& host,unsigned port,const std::string& observed,const std::string& legacy){return sshTrust.verify(host,port,observed,legacy);});
  switchCommunication.begin(networkPortal.switchConfig(),0);
  networkPortal.setSwitchKey([](){
    const auto& profile=networkPortal.switchConfig();
    return String(("sw_host_key_state="+std::string(switchSsh.hostKeyState())+"&sw_fingerprint="+switchcli::encode(sshTrust.fingerprint(profile.host,profile.port))).c_str());
  },[](){const auto& profile=networkPortal.switchConfig();return sshTrust.forget(profile.host,profile.port);});
  actionStore.begin("h3c-actions");
#if ESP_PORT_EXERCISE
  portExercise.log=firmware::println;
#if ESP_PORT_SHUTDOWN_ONLY
  portExercise.shutdownPortTwoOnly();
#endif
#if ESP_PORT_EXERCISE_BDI
  portExercise.requireDeliberation=true;
  portAcceptance.log=firmware::println;
  portExercise.deliberateAction=[](const switchmodel::Snapshot& s,uint64_t now,const std::string& port,unsigned phase,bool claimed){return portAcceptance.deliberate(s,now,port,phase,claimed);};
#endif
  portExercise.claimOnce=[](){
#if ESP_PORT_SHUTDOWN_ONLY
    const std::string key="port2-down",claim="20261103-1:started:shutdown_port_2";std::string stored;
#elif ESP_PORT_EXERCISE_BDI
    const std::string key="bdi23-test",claim="20261103-1:started:baseline_ADM_2_3";std::string stored;
#else
    const std::string key="port23-test",claim="20261103-1:started:baseline_ADM_2_3";std::string stored;
#endif
    return actionStore.read(key,stored)&&stored.empty()&&actionStore.write(key,claim)&&actionStore.read(key,stored)&&stored==claim;
  };
  portExercise.finishRun=[](){actionStore.write(ESP_PORT_SHUTDOWN_ONLY?"port2-down":ESP_PORT_EXERCISE_BDI?"bdi23-test":"port23-test",ESP_PORT_SHUTDOWN_ONLY?"20261103-1:passed:shutdown_port_2":"20261103-1:passed:baseline_ADM_2_3");};
#endif
  actionLedger.read=[](const std::string& key,std::string& value){return actionStore.read(key,value);};
  actionLedger.write=[](const std::string& key,const std::string& value){return actionStore.write(key,value);};
  actionLedger.erase=[](const std::string& key){return actionStore.erase(key);};
  switchRuntime.connectionLost=[](){actionLedger.disconnected();};
  switchRuntime.log=firmware::println;
  switchRuntime.submitRead=[](const std::string& id,const std::string& command){return switchConnection.submit(id,command);};
  switchRuntime.submitIsolation=[](const std::string& id,const std::string& port){return switchConnection.submitIsolation(id,port);};
  switchRuntime.submitAction=[](const std::string& id,switchmodel::Action action,const std::string& port){return switchConnection.submitAction(id,action,port);};
  switchRuntime.resultReady=[](const std::string& id){return switchConnection.resultReady(id);};
  switchRuntime.result=[](const std::string& id){return switchConnection.takeResult(id);};
  switchRuntime.claimIntent=[](const switchmodel::ActionPlan& plan,const std::string& incident){return actionLedger.claim(endpoint(),plan,incident);};
  switchRuntime.claimRestoreIntent=[](const switchmodel::ActionPlan& plan,const std::string& incident){return actionLedger.claimRestore(endpoint(),plan,incident);};
  networkPortal.setPortRestore([](const String& port,bool repaired){return String(switchRuntime.requestOwnedRestore(port.c_str(),repaired).c_str());});
  switchRuntime.alreadyClaimed=[](const std::string& port){return actionLedger.alreadyClaimed(endpoint(),port);};
  switchRuntime.observeLedger=[](const switchmodel::Snapshot& snapshot,const switchmodel::Policy& policy,uint64_t now){
    for(const auto& entry:snapshot.interfaces.ports)if(actionLedger.observeHealthy(endpoint(),entry.first,snapshot,policy,now))
      firmware::println("[BDI][LEDGER] port="+entry.first+" state=healthy_incident_cleared");
  };
  deviceAnalysis.save=[](const std::string& json){return sdJournal.appendAnalysis(json);};
  switchRuntime.analyse=[](const std::string& text,uint64_t at,uint64_t cycle){deviceAnalysis.observe(text,at,cycle,"h3c_"+std::string(switchConnection.kind()));};
  std::string bindingRevoked;
  if(actionStore.read("site-revoked",bindingRevoked)&&bindingRevoked.empty())
    switchRuntime.bindManagementProfile("192.168.10.2",22,"GigabitEthernet1/0/1");
  configureSwitch();
#if ESP_STORM_ACCEPTANCE
  static_assert(!ESP_PORT_EXERCISE&&!ESP_BDI_READ_ONLY,"Storm lab must use real fault BDI, separate from port exercise/readonly builds");
  stormAcceptance.log=firmware::println;
  stormAcceptance.outcome=[](const std::string& state){if(stormLabAdmitted)actionStore.write("storm2-test","20261103-1:"+state+":port2:vlan4090:threshold20");};
  const auto& labProfile=networkPortal.switchConfig();
  const std::string labClaim="20261103-1:started:port2:vlan4090:threshold20";std::string labStored;
  stormLabAdmitted=labProfile.host=="192.168.10.2"&&labProfile.port==22&&labProfile.protocol=="ssh"&&labProfile.transport=="shell"&&std::string(switchConnection.kind())=="ssh"&&actionStore.read("storm2-test",labStored)&&labStored.empty()&&actionStore.write("storm2-test",labClaim)&&actionStore.read("storm2-test",labStored)&&labStored==labClaim;
  stormAcceptance.begin(stormLabAdmitted,0);
  if(stormAcceptance.terminal())switchRuntime.revokeAutomation();
#endif
  networkPortal.setDeviceExtraStatus([](){return String(("\"firmware_version\":"+journal::quote(firmwareVersion)+",\"acceptance_read_only\":"+std::string(site::readOnlyAcceptance()&&!ESP_PORT_EXERCISE?"true":"false")+",\"boot\":"+journal::quote(bootId)+",\"ssh_host_key_state\":"+journal::quote(switchSsh.hostKeyState())+","+deviceExtraStatus()+exerciseFields()).c_str());});
  networkPortal.setSshStatus([](){return String(switchSsh.stateName());});
  networkPortal.setConnectionFields([](){return String("communication_mode=")+switchConnection.kind()+"&communication_policy="+switchCommunication.policy()+"&console_baud="+String(ESP_CONSOLE_BAUD)+"&console_tx="+String(ESP_CONSOLE_TX)+"&console_rx="+String(ESP_CONSOLE_RX)+"&bridge_host="+ESP_CONSOLE_BRIDGE_HOST+"&bridge_port="+String(ESP_CONSOLE_BRIDGE_PORT);});
  networkPortal.setSwitchStatus([](){return String(switchConnection.stateName());});
  networkPortal.setSwitchCommands(
    [](const String& id,const String& command){return String(switchConnection.submit(id.c_str(),command.c_str()).c_str());},
    [](const String& id){return String(switchConnection.result(id.c_str()).fields().c_str());});
  ethernetMqtt.configure(networkPortal.mqttConfig());
  networkPortal.setStorageStatus([](){return sdJournal.status();});
  networkPortal.setStorageViewer([](const String& file,uint32_t offset,uint32_t job){return sdJournal.read(file,offset,job);});
  networkPortal.setLogFiles([](uint32_t before){return sdJournal.filesPage(before);});
  networkPortal.setViewerOpened([](){sdJournal.flush();});
#if ESP_LOG_AI_ENABLED
  networkPortal.setLogExplainer(tinyLogReply);
  firmware::printf("[LOG-AI] model=%s inference=ESP32 display_only=1",tinylog::VERSION);
#endif
  switchHeartbeat.configure(networkPortal.switchConfig().host,networkPortal.switchConfig().protocol=="ssh"&&std::string(switchConnection.kind())=="ssh");
  networkPortal.setHeartbeatStatus([](){return switchHeartbeat.status();});
}
void loop() {
  // Record transitions independently of the SSH worker and its retry delay.
  static int previousLink=-1;
  const int link=ETH.linkUp()?1:0;
  if(link!=previousLink){
    firmware::printf("[NETWORK][LINK] state=%s previous=%s uptime_ms=%lu ip=%s free_heap=%u",link?"up":"down",previousLink<0?"unknown":previousLink?"up":"down",(unsigned long)millis(),ETH.localIP().toString().c_str(),(unsigned)ESP.getFreeHeap());
    previousLink=link;
  }
  networkPortal.tick();
  if(switchRevision!=networkPortal.switchRevision()){
    switchRevision=networkPortal.switchRevision();
    if(switchCommunication.configure(networkPortal.switchConfig(),monotonic_ms)){
      configureSwitch();
      switchHeartbeat.configure(networkPortal.switchConfig().host,networkPortal.switchConfig().protocol=="ssh"&&std::string(switchConnection.kind())=="ssh");
    }
  }
  if(mqttRevision!=networkPortal.mqttRevision()){mqttRevision=networkPortal.mqttRevision();ethernetMqtt.configure(networkPortal.mqttConfig());}
  ethernetMqtt.tick();
  uint32_t current = millis();
  monotonic_ms += static_cast<uint32_t>(current - previous_ms);
  previous_ms = current;
  switchCommunication.tick(monotonic_ms);
  if(communicationGeneration!=switchCommunication.generation()){
    communicationGeneration=switchCommunication.generation();
    configureSwitch(); // Discard old evidence and re-read the device on the new path.
  }
  switchHeartbeat.tick(monotonic_ms,switchSsh.connected());
  const bool connected=switchConnection.connected();
#if ESP_PORT_EXERCISE
  portExercise.tick(monotonic_ms);
  if(!portExercise.exclusive())switchRuntime.tick(monotonic_ms,connected,switchConnection.sessionAccepted());
#else
  switchRuntime.tick(monotonic_ms,connected,switchConnection.sessionAccepted());
#endif
#if ESP_STORM_ACCEPTANCE
  const auto* labRecovery=switchRuntime.recoveryFor("GigabitEthernet1/0/2");
  stormAcceptance.tick(switchRuntime.currentSnapshot(),monotonic_ms,connected&&switchConnection.sessionAccepted(),switchRuntime.automationAuthorized(),labRecovery?labRecovery->state:switchmodel::RecoveryState::Idle,labRecovery&&labRecovery->ownsIsolation());
  if(stormAcceptance.terminal())switchRuntime.revokeAutomation();
#endif
  static uint64_t last_status = 0;
  if (monotonic_ms - last_status >= 5000) {
    last_status = monotonic_ms;
    const auto& hb=switchHeartbeat.bdi.state();
    const std::string telemetry="{\"schema_version\":1,\"source\":\"esp_native\",\"boot\":"+journal::quote(bootId)+",\"uptime_ms\":"+std::to_string(monotonic_ms)+",\"ip\":"+journal::quote(ETH.localIP().toString().c_str())+",\"ssh_state\":"+journal::quote(switchSsh.stateName())+",\"heartbeat\":{\"state\":"+journal::quote(switchHeartbeat.stateName())+",\"rtt_ms\":"+std::to_string(hb.rtt)+",\"misses\":"+std::to_string(hb.misses)+"},"+deviceExtraStatus()+"}";
    ethernetMqtt.publishStatus(telemetry);
    firmware::printf("[MANAGEMENT][STATUS] transport=%s state=%s",switchConnection.kind(),switchConnection.stateName());
    firmware::printf("[SSH][STATUS] state=%s ip=%s link=%s target=%s:%u free_heap=%u\n",switchSsh.stateName(),ETH.localIP().toString().c_str(),ETH.linkUp()?"up":"down",networkPortal.switchConfig().host.c_str(),networkPortal.switchConfig().port,(unsigned)ESP.getFreeHeap());
    firmware::printf("[BDI][STATUS] reasoning_cycle=ACTIVE transport=%s uptime_ms=%llu devices=1 free_heap=%u sd_state=%s log_source=esp_native",
        switchConnection.kind(),(unsigned long long)monotonic_ms,(unsigned)ESP.getFreeHeap(),sdJournal.stateName());
  }
  delay(1);
}
