#ifndef BDI_FUNCTIONS_H_
#define BDI_FUNCTIONS_H_

#include <Arduino.h>

extern uint32_t bdi_unlock_request_id;
extern uint32_t bdi_recover_request_id;
extern bool bdi_agent_ready;

inline bool action_announce_ready() {
  bdi_agent_ready = true;
  Serial.println("[BDI][READY] H3C monitor agent transport=USB gateway=PYTHON_MQTT");
  return true;
}

inline bool action_report_healthy() {
  Serial.println("[BDI][ACTION] server health accepted state=HEALTHY");
  return true;
}

inline bool action_report_critical() {
  Serial.println("[BDI][ACTION] alert severity=CRITICAL decision=MONITOR_AND_NOTIFY");
  return true;
}

inline bool action_request_unlock() {
  ++bdi_unlock_request_id;
  Serial.printf(
      "[BDI][CMD] UNLOCK request_id=%lu target=SIMULATED_H3C_CONSOLE "
      "reason=SERVER_LOCKED\r\n",
      static_cast<unsigned long>(bdi_unlock_request_id));
  return true;
}

inline bool action_recover_interface() {
  ++bdi_recover_request_id;
  Serial.printf(
      "[BDI][CMD] RECOVER_INTERFACE request_id=%lu "
      "target=GigabitEthernet1/0/1 reason=LINK_DOWN\r\n",
      static_cast<unsigned long>(bdi_recover_request_id));
  return true;
}

#endif
