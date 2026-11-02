#include "../src/ai/device_analysis.h"
#include <cassert>
#include <iostream>
int main(){
  DeviceAnalysis analysis;std::vector<std::string> saved;
  analysis.save=[&](const std::string& s){saved.push_back(s);return true;};
  const std::string evidence="%Nov 2 H3C LBDT/4/LOOP_DETECTED: Loop detected on GigabitEthernet1/0/2";
  analysis.observe("display logbuffer reverse\nLog buffer: enabled\n"+evidence+"\n<H3C>",12000,1);
  assert(saved.size()==1&&saved[0].size()<=1000);
  analysis.observe(evidence,25000,2);assert(saved.size()==1);
  analysis.reset();analysis.observe(evidence,30000,3);assert(saved.size()==2);
  DeviceAnalysis failure;unsigned calls=0;
  failure.save=[&](const std::string&){++calls;return false;};
  failure.observe(evidence,1000,1);failure.observe(evidence,2000,2);assert(calls==2);
  analysis.observe("%Nov 2 H3C SHELL/5/LOGIN: password=private-password",40000,4);
  assert(saved.back().find("private-password")==std::string::npos);
  for(const auto& item:saved)std::cout<<item<<"\n";
}
