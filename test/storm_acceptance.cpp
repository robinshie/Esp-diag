#include "../src/application/storm_acceptance.h"
#include "../src/config/site_policy.h"
#include <cassert>
#include <iostream>
using namespace switchmodel;
static Snapshot snapshot(){
  Snapshot s;s.managementConnected=s.identityVerified=true;s.targetRevision=1;
  Stamp stamp;stamp.complete=true;stamp.cycle=1;stamp.atMs=1000;stamp.targetRevision=1;
  s.interfaces.valid=s.down.valid=s.loops.valid=s.stp.valid=s.aggregation.valid=s.storm.valid=true;
  s.interfaces.stamp=s.down.stamp=s.loops.stamp=s.stp.stamp=s.aggregation.stamp=s.storm.stamp=stamp;
  for(unsigned n=1;n<=3;++n){Interface port;port.name="GigabitEthernet1/0/"+std::to_string(n);port.link=Link::Up;port.type='A';port.pvid=n==2?4090:10;s.interfaces.ports[port.name]=port;}
  s.storm.ports["GigabitEthernet1/0/2"].measured=true;
  return s;
}
int main(){
  const std::string port="GigabitEthernet1/0/2";auto s=snapshot();auto p=site::switchPolicy();p.topologyConfirmed=true;p.managementPort="GigabitEthernet1/0/1";
  assert(p.allowedPorts.size()==1&&p.allowedPorts.count(port)&&p.requiredPvids.at(port)==4090&&p.stormBroadcastLimits.at(port)==20);
  assert(policyGate(s,p,port,1000).empty());
  assert(policyGate(s,p,"GigabitEthernet1/0/1",1000)=="management_port_protected");
  assert(policyGate(s,p,"GigabitEthernet1/0/3",1000)=="port_not_authorized");
  s.interfaces.ports[port].pvid=10;assert(policyGate(s,p,port,1000)=="required_access_vlan_mismatch");s=snapshot();
  {switchapp::StormAcceptance a;a.begin(false,0);a.tick(s,1000,true,true,RecoveryState::Idle,false);assert(a.state()=="blocked");}
  {switchapp::StormAcceptance a;a.begin(true,0);a.tick(s,1000,true,true,RecoveryState::Idle,false);assert(a.state()=="ready");
   a.tick(s,2000,true,true,RecoveryState::VerifyingIsolation,false);assert(a.state()=="verifying_isolation");
   a.tick(s,3000,true,true,RecoveryState::Contained,true);assert(a.state()=="awaiting_stop_confirmation");
   a.tick(s,600000,true,true,RecoveryState::Contained,true);assert(a.state()=="awaiting_stop_confirmation"&&!a.terminal());
   a.tick(s,600001,true,true,RecoveryState::VerifyingRecovery,true);assert(a.state()=="verifying_recovery");
   a.tick(s,600002,true,true,RecoveryState::Recovered,false);assert(a.state()=="passed"&&a.terminal());}
  {switchapp::StormAcceptance a;a.begin(true,0);a.tick(s,900000,true,true,RecoveryState::Contained,true);assert(a.state()=="needs_review");}
  {switchapp::StormAcceptance a;a.begin(true,0);a.tick(s,1000,true,true,RecoveryState::Idle,false);a.tick(s,1100,false,true,RecoveryState::Idle,false);assert(a.state()=="needs_review");}
  {switchapp::StormAcceptance a;a.begin(true,0);a.tick(s,1000,true,true,RecoveryState::Idle,false);a.configurationChanged();assert(a.state()=="needs_review");}
  {switchapp::StormAcceptance a;a.begin(true,0);a.tick(s,1000,true,true,RecoveryState::Contained,false);assert(a.state()=="needs_review");}
  {switchapp::StormAcceptance a;a.begin(true,0);s.interfaces.ports[port].pvid=10;a.tick(s,1000,true,true,RecoveryState::Idle,false);assert(a.state()=="needs_review");s=snapshot();}
  {switchapp::StormAcceptance a;a.begin(true,0);a.tick(s,1000,true,false,RecoveryState::Idle,false);assert(a.state()=="needs_review");}
  {switchapp::StormAcceptance a;a.begin(true,0);s.interfaces.ports[port].link=Link::Down;a.tick(s,1000,true,true,RecoveryState::Idle,false);assert(a.state()=="waiting_link");s=snapshot();}
  {switchapp::StormAcceptance a;a.begin(true,0);s.storm.ports[port].measured=false;a.tick(s,1000,true,true,RecoveryState::Idle,false);assert(a.state()=="waiting_baseline");s=snapshot();}
  std::cout<<"11 storm-lab admission/observation scenarios and isolated-port policy fences passed\n";
}
