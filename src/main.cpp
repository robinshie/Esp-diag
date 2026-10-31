#include <Arduino.h>
#include <esp_system.h>
#include "cluster_runtime.h"

std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
namespace {
ClusterRuntime* runtime;
std::string usb_line;
bool discard_frame = false;
uint64_t monotonic_ms = 0;
uint32_t previous_ms = 0;
}
void setup() {
  Serial.setRxBufferSize(4096);
  Serial.begin(115200);
  delay(3000);
  char boot[33];
  snprintf(boot, sizeof(boot), "%08lx%08lx%08lx%08lx",
           (unsigned long)esp_random(), (unsigned long)esp_random(),
           (unsigned long)esp_random(), (unsigned long)esp_random());
  bdi_output = [](const std::string& s) { Serial.println(s.c_str()); };
  runtime = new ClusterRuntime(boot);
  runtime->model.output = bdi_output;
  bdi_output("[BDI][BOOT] boot=" + std::string(boot) + " protocol=2 max_devices=32");
}
void loop() {
  uint32_t current = millis();
  monotonic_ms += static_cast<uint32_t>(current - previous_ms);
  previous_ms = current;
  runtime->model.tick(monotonic_ms);
  // Bound bytes per iteration so a continuous event stream cannot starve timers.
  for (unsigned count = 0; count < 1024 && Serial.available(); ++count) {
    char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      if (!discard_frame) runtime->model.receive(usb_line);
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
    Serial.printf("[BDI][STATUS] reasoning_cycle=ACTIVE transport=USB uptime_ms=%llu devices=%u rejected=%u free_heap=%u\r\n",
        (unsigned long long)monotonic_ms, (unsigned)runtime->model.devices.size(),
        runtime->model.rejected, (unsigned)ESP.getFreeHeap());
  }
  delay(1);
}
