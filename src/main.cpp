#include <Arduino.h>

#include "agent/agent.h"
#include "bdi_configuration.h"

namespace {
Agent* bdi_agent = nullptr;
AgentSettings* bdi_settings = nullptr;
String usb_line;

bool has_field(const String& line, const char* field) {
  return line.indexOf(field) >= 0;
}

String field_value(const String& line, const char* key) {
  String prefix = String(key) + "=";
  int start = line.indexOf(prefix);
  if (start < 0) return "";
  start += prefix.length();
  int end = line.indexOf(' ', start);
  if (end < 0) end = line.length();
  return line.substring(start, end);
}

void publish_belief(BdiProposition proposition, bool state,
                    const char* name) {
  if (bdi_settings->set_belief(proposition, state)) {
    Serial.printf("[BDI][EVENT] belief=%s value=%s\r\n", name,
                  state ? "true" : "false");
  }
}

void handle_h3c_line(String line) {
  line.trim();
  if (line.isEmpty()) return;

  if (line.startsWith("H3C PING")) {
    Serial.printf("H3C PONG nonce=%s transport=USB\r\n",
                  field_value(line, "nonce").c_str());
    return;
  }

  if (line.startsWith("H3C ACK") && has_field(line, "action=UNLOCK")) {
    String request_id = field_value(line, "request_id");
    if (has_field(line, "status=OK")) {
      publish_belief(SERVER_LOCKED, false, "server_locked");
      Serial.printf("[BDI][ACK] unlock request_id=%s result=SUCCESS\r\n",
                    request_id.c_str());
    } else {
      Serial.printf("[BDI][ACK] unlock request_id=%s result=FAILED\r\n",
                    request_id.c_str());
    }
    return;
  }

  if (!line.startsWith("H3C LOG")) {
    Serial.printf("[BDI][WARN] unsupported_input=%s\r\n", line.c_str());
    return;
  }

  Serial.printf("[BDI][RX][USB] %s\r\n", line.c_str());
  if (has_field(line, "event=HEARTBEAT") && has_field(line, "status=OK")) {
    publish_belief(SERVER_HEALTHY, true, "server_healthy");
  }
  if (has_field(line, "status=ALARM") || has_field(line, "level=CRITICAL")) {
    publish_belief(SERVER_HEALTHY, false, "server_healthy");
    publish_belief(SERVER_CRITICAL, true, "server_critical");
  }
  if (has_field(line, "event=RECOVERY") && has_field(line, "status=OK")) {
    publish_belief(SERVER_CRITICAL, false, "server_critical");
    publish_belief(SERVER_HEALTHY, true, "server_healthy");
  }
  if (has_field(line, "event=AUTH_LOCKED") ||
      has_field(line, "status=LOCKED")) {
    publish_belief(SERVER_LOCKED, true, "server_locked");
  }
}

void poll_usb_transport() {
  while (Serial.available()) {
    char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      handle_h3c_line(usb_line);
      usb_line = "";
    } else if (value != '\r' && usb_line.length() < 512) {
      usb_line += value;
    }
  }
}
}  // namespace

uint32_t bdi_unlock_request_id = 0;
bool bdi_agent_ready = false;

void setup() {
  Serial.begin(115200);
  delay(3000);

  static AgentSettings settings;
  static Agent agent(settings.get_belief_base(), settings.get_event_base(),
                     settings.get_plan_base(), settings.get_intention_base());
  bdi_settings = &settings;
  bdi_agent = &agent;
}

void loop() {
  poll_usb_transport();
  bdi_agent->run();

  static unsigned long last_status_ms = 0;
  if (bdi_agent_ready && millis() - last_status_ms >= 5000) {
    last_status_ms = millis();
    Serial.printf(
        "[BDI][STATUS] reasoning_cycle=ACTIVE transport=USB uptime_ms=%lu\r\n",
        static_cast<unsigned long>(millis()));
  }

  delay(1);
}
