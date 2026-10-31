#include <Arduino.h>
#include <esp_system.h>
#include "cluster_runtime.h"
#include "network_portal.h"
#include "ethernet_mqtt.h"
#include "sd_journal.h"
#include "tiny_log_service.h"

std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
namespace {
ClusterRuntime* runtime;
NetworkPortal networkPortal;
EthernetMqtt ethernetMqtt;
SdJournal sdJournal;
unsigned mqttRevision=0;
std::string usb_line;
bool discard_frame = false;
uint64_t monotonic_ms = 0;
uint32_t previous_ms = 0;
}
void setup() {
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  delay(3000);
  networkPortal.begin();
  ethernetMqtt.configure(networkPortal.mqttConfig());
  char boot[33];
  snprintf(boot, sizeof(boot), "%08lx%08lx%08lx%08lx",
           (unsigned long)esp_random(), (unsigned long)esp_random(),
           (unsigned long)esp_random(), (unsigned long)esp_random());
  sdJournal.begin(boot);
  networkPortal.setStorageStatus([](){return sdJournal.status();});
  networkPortal.setStorageViewer([](){return sdJournal.list();},[](const String& file,uint32_t offset,uint32_t job){return sdJournal.read(file,offset,job);});
#if ESP_LOG_AI_EXPERIMENT
  networkPortal.setLogExplainer(tinyLogReply);
  Serial.printf("[LOG-AI] model=%s official_derived_and_synthetic_training=1 display_only=1 mqtt_unchanged=1\n",tinylog::VERSION);
#endif
  bdi_output = [](const std::string& s) { Serial.println(s.c_str()); ethernetMqtt.output(s);sdJournal.core.output(s); };
  runtime = new ClusterRuntime(boot);
  runtime->model.output = bdi_output;
  bdi_output("[BDI][BOOT] boot=" + std::string(boot) + " protocol=2 max_devices=32");
}
void loop() {
  networkPortal.tick();
  if(mqttRevision!=networkPortal.mqttRevision()){mqttRevision=networkPortal.mqttRevision();ethernetMqtt.configure(networkPortal.mqttConfig());}
  ethernetMqtt.tick();
  uint32_t current = millis();
  monotonic_ms += static_cast<uint32_t>(current - previous_ms);
  previous_ms = current;
  sdJournal.core.clock(monotonic_ms);
  runtime->model.tick(monotonic_ms);
  static uint64_t last_journal_observation=0;
  if(monotonic_ms-last_journal_observation>=1000){
    last_journal_observation=monotonic_ms;
    sdJournal.core.observe(runtime->model);
#ifdef ESP_ETH_W5500
    sdJournal.core.link(ETH.linkUp());
#endif
  }
  // Bound bytes per iteration so a continuous event stream cannot starve timers.
  for (unsigned count = 0; count < 1024 && Serial.available(); ++count) {
    char value = static_cast<char>(Serial.read());
    if (value == '\n') {
#if ESP_LOG_AI_EXPERIMENT
      // Lab diagnostics bypass BDI entirely: inference cannot dispatch an action.
      if(!discard_frame&&usb_line.compare(0,7,"H3C AI ")==0){
        Serial.print("[LOG-AI] ");Serial.println(tinyLogReply(String(usb_line.c_str()+7)));
        usb_line.clear();discard_frame=false;continue;
      }
#endif
      if (!discard_frame&&runtime->model.receive(usb_line)){
        sdJournal.core.input(usb_line);sdJournal.core.observe(runtime->model);
      }
      usb_line.clear(); discard_frame = false;
    } else if (value != '\r' && !discard_frame) {
      if (usb_line.size() >= 511) {
        usb_line.clear(); discard_frame = true;
        runtime->model.reject("FRAME_TOO_LONG");
      } else usb_line += value;
    }
  }
  static uint64_t last_status = 0;
  if (monotonic_ms - last_status >= 5000) {
    last_status = monotonic_ms;
    Serial.printf("[BDI][STATUS] reasoning_cycle=ACTIVE transport=USB uptime_ms=%llu devices=%u rejected=%u free_heap=%u sd_state=%s\r\n",
        (unsigned long long)monotonic_ms, (unsigned)runtime->model.devices.size(),
        runtime->model.rejected, (unsigned)ESP.getFreeHeap(),sdJournal.stateName());
  }
  delay(1);
}
