#ifndef BDI_FUNCTIONS_H_
#define BDI_FUNCTIONS_H_
#include <functional>
#include <string>
extern std::string bdi_device;
extern std::function<void(const std::string&)> bdi_output;
extern std::function<void()> bdi_request_action;
inline bool action_announce_ready() {
  bdi_output("[BDI][READY] device=" + bdi_device + " transport=USB");
  return true;
}
inline bool action_report_healthy() {
  bdi_output("[BDI][ACTION] device=" + bdi_device + " decision=HEALTHY");
  return true;
}
inline bool action_report_critical() {
  bdi_output("[BDI][ACTION] device=" + bdi_device + " severity=CRITICAL decision=MONITOR_AND_NOTIFY");
  return true;
}
inline bool action_request_unlock() { if (bdi_request_action) bdi_request_action(); return true; }
inline bool action_recover_interface() { if (bdi_request_action) bdi_request_action(); return true; }
#endif
