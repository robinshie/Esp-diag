#include <cassert>
#include <vector>
#include "../src/bdi/switch_heartbeat_bdi.h"
#include "../src/legacy/cluster_runtime.h"
std::string bdi_device="original";
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
int main(){
  std::vector<std::string> lines;
  bdi_output=[&](const std::string& line){lines.push_back(line);};
  auto count=[&](const std::string& word){unsigned n=0;for(const auto& line:lines)if(line.find(word)!=std::string::npos)++n;return n;};
  SwitchHeartbeatBdi heartbeat;unsigned pings=0;heartbeat.requestPing=[&](){++pings;};
  heartbeat.reset("192.168.10.2");
  heartbeat.tick(0,false);heartbeat.tick(5000,false);assert(pings==0);
  assert(heartbeat.evidence.state==HeartbeatState::State::WaitingSsh);
  heartbeat.tick(5001,true);assert(pings==1);assert(count("decision=HEARTBEAT_OK")==0);
  heartbeat.observe(true,5002,4,"ECHO_REPLY");assert(count("decision=HEARTBEAT_OK")==1);
  heartbeat.tick(10001,true);assert(pings==2);
  heartbeat.observe(false,11000,0,"PING_TIMEOUT");assert(!heartbeat.evidence.alarm);
  heartbeat.observe(false,16000,0,"PING_TIMEOUT");assert(!heartbeat.evidence.alarm);
  heartbeat.observe(false,21000,0,"PING_TIMEOUT");assert(heartbeat.evidence.alarm);
  assert(count("decision=HEARTBEAT_LOST")==1);
  heartbeat.observe(false,26000,0,"PING_TIMEOUT");assert(count("decision=HEARTBEAT_LOST")==1);
  heartbeat.observe(true,31000,3,"ECHO_REPLY");assert(heartbeat.evidence.alarm);
  assert(heartbeat.evidence.state==HeartbeatState::State::Recovering);
  heartbeat.observe(false,36000,0,"LOCAL_LINK_DOWN");assert(heartbeat.evidence.alarm);
  heartbeat.observe(true,41000,2,"ECHO_REPLY");assert(heartbeat.evidence.alarm);
  heartbeat.observe(true,46000,2,"ECHO_REPLY");assert(!heartbeat.evidence.alarm);
  assert(count("decision=HEARTBEAT_OK")==2);
  heartbeat.tick(61001,false);assert(heartbeat.evidence.state==HeartbeatState::State::Unknown);
  assert(bdi_device=="original");
  // SSH failure alone cannot assert ping loss; ping still runs after first login.
  auto before=pings;heartbeat.tick(66001,false);assert(pings>before);assert(!heartbeat.evidence.alarm);
  heartbeat.observe(false,66002,0,"PING_TIMEOUT");heartbeat.observe(false,71002,0,"PING_TIMEOUT");heartbeat.observe(false,76002,0,"PING_TIMEOUT");
  heartbeat.tick(91003,false);assert(heartbeat.evidence.alarm);assert(heartbeat.evidence.state==HeartbeatState::State::Unknown);
  heartbeat.probeError();assert(heartbeat.evidence.alarm); // monitor failure never clears known loss
  heartbeat.reset("192.168.10.3");assert(!heartbeat.evidence.alarm&&!heartbeat.evidence.armed);
  before=pings;heartbeat.tick(92000,false);assert(pings==before);
  // Independent heartbeat reasoning cannot recover a component fault in cluster BDI.
  ClusterRuntime cluster("heartbeat-test");cluster.model.output=bdi_output;
  assert(cluster.model.ingest("H3C LOG device=business-switch kind=switch epoch=1 seq=1 event=THERMAL component=cpu level=CRITICAL status=ALARM"));
  heartbeat.tick(93000,true);heartbeat.observe(true,93001,1,"ECHO_REPLY");
  assert(cluster.model.devices.at("business-switch").faults.size()==1);
  assert(cluster::Model::health(cluster.model.devices.at("business-switch"))=="FAULT");
  assert(count("[BDI][CMD]")==0);
}
