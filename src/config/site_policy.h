#pragma once
#include "../domain/switch_policy.h"
namespace site {
// Existing user-authorized business-port range. Device adapters never grant
// automation permission. The confirmed management binding is applied separately.
inline switchmodel::Policy switchPolicy(){
  switchmodel::Policy p;p.automationEnabled=true;
  for(unsigned n=2;n<=48;++n)p.allowedPorts.insert("GigabitEthernet1/0/"+std::to_string(n));
  for(unsigned n=49;n<=52;++n)p.allowedPorts.insert("Ten-GigabitEthernet1/0/"+std::to_string(n));
  return p;
}
}
