#pragma once
#include "../domain/switch_policy.h"
#ifndef ESP_BDI_READ_ONLY
#define ESP_BDI_READ_ONLY 0
#endif
#ifndef ESP_STORM_ACCEPTANCE
#define ESP_STORM_ACCEPTANCE 0
#endif
namespace site {
inline bool readOnlyAcceptance(){return ESP_BDI_READ_ONLY!=0;}
// Existing user-authorized business-port range. Device adapters never grant
// automation permission. The confirmed management binding is applied separately.
inline switchmodel::Policy switchPolicy(){
  switchmodel::Policy p;p.automationEnabled=!readOnlyAcceptance();
#if ESP_STORM_ACCEPTANCE
  // Test values only: one isolated access port, never the management VLAN.
  p.allowedPorts={"GigabitEthernet1/0/2"};
  p.requiredPvids["GigabitEthernet1/0/2"]=4090;
  p.stormBroadcastLimits["GigabitEthernet1/0/2"]=20;
#else
  for(unsigned n=2;n<=48;++n)p.allowedPorts.insert("GigabitEthernet1/0/"+std::to_string(n));
  for(unsigned n=49;n<=52;++n)p.allowedPorts.insert("Ten-GigabitEthernet1/0/"+std::to_string(n));
#endif
  return p;
}
}
