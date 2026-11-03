#include "../src/application/switch_runtime.h"
#include "../src/application/action_ledger.h"
#include "../src/adapters/h3c/device_adapter.h"
#include "../src/bdi/strategies/storm_isolation.h"
#include "../src/bdi/strategies/loop_isolation.h"
#include "../src/config/site_policy.h"
#include <cassert>
#include <iostream>
#include <fstream>

std::string bdi_device;
std::function<void(const std::string&)> bdi_output;
std::function<void()> bdi_request_action;
using namespace switchmodel;
const std::string business="GigabitEthernet1/0/3",management="GigabitEthernet1/0/1";
static inline Policy configuredPolicy(){Policy p;p.automationEnabled=true;p.allowedPorts.insert(business);p.stormBroadcastLimits[business]=1000;return p;}
struct Experiment {
  h3c::DeviceAdapter adapter;
  switchbdi::LoopIsolationStrategy loop;
  switchbdi::StormIsolationStrategy storm;
  switchbdi::StrategySet strategies{&loop,&storm};
  switchapp::SwitchRuntime runtime;
  switchapp::ActionLedger ledger;
  std::map<std::string,std::string> storage;
  std::map<std::string,switchcli::Result> replies;
  std::vector<Action> writes;
  std::vector<std::string> logs;
  uint64_t now=1000,lastTraffic=0,broadcast=0,total=0;
  unsigned pps=2000;
  bool isolated=false,repaired=false,looped=false,aggregation=false,root=false,trunk=false,unsupported=false,failWrites=false,failStorage=false;
  explicit Experiment(const Policy& policy=configuredPolicy()):runtime(adapter,strategies,policy){
    ledger.read=[this](const std::string& k,std::string& v){v=storage[k];return true;};
    ledger.write=[this](const std::string& k,const std::string& v){if(failStorage)return false;storage[k]=v;return true;};
    ledger.erase=[this](const std::string& k){storage.erase(k);return true;};
    runtime.log=[this](const std::string& line){logs.push_back(line);};
    runtime.connectionLost=[this](){ledger.disconnected();};
    runtime.claimIntent=[this](const ActionPlan& plan,const std::string& incident){return ledger.claim("192.168.10.2:22",plan,incident);};
    runtime.claimRestoreIntent=[this](const ActionPlan& plan,const std::string& incident){return ledger.claimRestore("192.168.10.2:22",plan,incident);};
    runtime.alreadyClaimed=[this](const std::string& p){return ledger.alreadyClaimed("192.168.10.2:22",p);};
    runtime.observeLedger=[this](const Snapshot& s,const Policy& p,uint64_t at){for(const auto& row:s.interfaces.ports)ledger.observeHealthy("192.168.10.2:22",row.first,s,p,at);};
    runtime.submitRead=[this](const std::string& id,const std::string& key){switchcli::Result r;r.id=id;r.command=key;r.target="192.168.10.2";r.output=fixture(key);r.state="ok";replies[id]=r;return "queued";};
    runtime.submitAction=[this](const std::string& id,Action action,const std::string& port){
      assert(port==business);writes.push_back(action);
      switchcli::Result r;r.id=id;r.command=actionCommandKey(action,port);r.target="192.168.10.2";r.state=failWrites?"timeout_or_disconnected":"ok";replies[id]=r;
      if(!failWrites)isolated=action!=Action::RestoreOwned;
      return "queued";
    };
    runtime.resultReady=[this](const std::string& id){return replies.count(id)&&replies.at(id).done();};
    runtime.result=[this](const std::string& id){return replies.at(id);};
    assert(runtime.bindManagementProfile("192.168.10.2",22,management));runtime.configure("192.168.10.2",0,"storm-experiment",true);
  }
  std::string fixture(const std::string& key){
    if(key=="version")return "H3C Comware Software, Version 7.1.070, Release 6328P03\nH3C S5130S-52S-EI uptime is 1 day\n<H3C>";
    if(key=="interfaces")return std::string("Brief information on interfaces in bridge mode:\nInterface Link Speed Duplex Type PVID Description\nGE1/0/1 UP 100M F A 10\nGE1/0/3 ")+(isolated?"ADM":"UP")+" 1G F "+(trunk?"T":"A")+" 10\n<H3C>";
    if(key=="down")return std::string("Brief information on interfaces in bridge mode:\nInterface Link Cause\n")+(isolated?"GE1/0/3 ADM Administratively\n":"")+"<H3C>";
    if(key=="loops")return looped&&!repaired?"Loop detection is enabled.\nLoop detection interval is 30 second(s).\nLoop is detected on following interfaces:\nInterface Action mode VLANs\nGigabitEthernet1/0/3 None 10\n<H3C>":"Loop detection is enabled.\nLoop detection interval is 30 second(s).\nNo loop is detected.\n<H3C>";
    if(key=="stp")return std::string("MST ID Port Role STP State Protection\n0 GigabitEthernet1/0/1 DESI FORWARDING NONE\n")+(isolated?"":std::string("0 GigabitEthernet1/0/3 ")+(root?"ROOT":"DESI")+" FORWARDING NONE\n")+"<H3C>";
    if(key=="aggregation")return aggregation?"Port Status: S -- Selected, U -- Unselected, I -- Individual\nAggregate Interface: Bridge-Aggregation1\nLocal:\nPort Status Priority Oper-Key Flag\nGE1/0/3 S 32768 1 {ACDEF}\n<H3C>":"display link-aggregation verbose\n<H3C>";
    if(key=="traffic"){
      const auto elapsed=lastTraffic?now-lastTraffic:0;lastTraffic=now;
      const auto packets=(!isolated&&!repaired?pps:0)*elapsed/1000;broadcast+=packets;total+=packets;
      return "Interface Total (pkts) Broadcast (pkts) Multicast (pkts) Err (pkts)\nGE1/0/1 100 0 0 0\nGE1/0/3 "+std::to_string(total)+" "+(unsupported?"--":std::to_string(broadcast))+" 0 0\n<H3C>";
    }
    return "<H3C>";
  }
  void step(bool connected=true){now+=500;runtime.tick(now,connected,connected);}
  template<class P> void until(P predicate){for(unsigned i=0;i<1500&&!predicate();++i)step();if(!predicate())std::cerr<<runtime.json()<<"\n";assert(predicate());}
  void run(uint64_t duration){const auto stop=now+duration;while(now<stop)step();}
  bool is(const std::string& state)const{return runtime.json().find("\"state\":\""+state+"\"")!=std::string::npos;}
};
#ifndef SWITCH_FAULT_EXPERIMENT_ONLY
static void parserCases(){
  Stamp s;s.complete=true;s.cycle=1;s.targetRevision=1;s.atMs=1;
  const std::string header="Interface Total (pkts) Broadcast (pkts) Multicast (pkts) Err (pkts)\n";
  assert(h3c::parseTraffic(header+"GE1/0/3 100 50 0 0\n",s).ports.at(business).broadcast==50);
  for(const auto& value:{"--","Overflow","-1","999999999999999","1;shutdown"})assert(!h3c::parseTraffic(header+"GE1/0/3 100 "+value+" 0 0\n",s).ports.count(business));
  assert(!h3c::parseTraffic(header+"GE1/0/3 1 2 0 0\n",s).valid);
  assert(!h3c::parseTraffic(header+"GE1/0/3 100 0 0 0\nGE1/0/3 100 0 0 0\n",s).valid);
  assert(!h3c::parseTraffic(header+"GE1/0/3 100 -- 0 0\nGE1/0/3 100 0 0 0\n",s).valid);
  assert(!h3c::parseTraffic("Interface Broadcast (pkts) Total (pkts) Multicast (pkts) Err (pkts)\nGE1/0/3 100 50 0 0\n",s).valid);
  s.complete=false;assert(!h3c::parseTraffic(header+"GE1/0/3 100 50 0 0\n",s).valid);
}
static void observationFences(){
  auto p=configuredPolicy();StormMonitor monitor;Snapshot s;
  auto sample=[&](uint64_t cycle,uint64_t at,uint64_t count,uint64_t revision=1){
    s.targetRevision=revision;s.interfaces.stamp.cycle=cycle;s.traffic=TrafficCounters();
    s.traffic.valid=true;s.traffic.stamp.complete=true;s.traffic.stamp.cycle=cycle;s.traffic.stamp.atMs=at;s.traffic.stamp.targetRevision=revision;
    s.traffic.ports[business].total=count;s.traffic.ports[business].broadcast=count;monitor.observe(s,p,at);
  };
  sample(1,1000,100);assert(!s.storm.ports.at(business).measured);
  sample(2,11000,20100);assert(s.storm.ports.at(business).rounds==1);
  sample(2,11000,20100);assert(s.storm.ports.at(business).rounds==1);
  sample(3,21000,40100);assert(s.storm.ports.at(business).rounds==2);
  sample(4,31000,1);assert(!s.storm.ports.at(business).measured); // Counter reset.
  sample(5,41000,20001);assert(s.storm.ports.at(business).rounds==1);
  sample(7,51000,40001);assert(!s.storm.ports.at(business).measured); // Lost collection cycle.
  sample(8,101000,140001);assert(!s.storm.ports.at(business).measured); // Long gap.
  sample(9,102000,142001);assert(!s.storm.ports.at(business).measured); // Window too short.
  sample(10,112000,162001,2);assert(!s.storm.ports.at(business).measured); // Different endpoint revision.
  sample(11,122000,182001,2);assert(s.storm.ports.at(business).rounds==1);
  s.traffic.valid=false;monitor.observe(s,p,122000);assert(!s.storm.valid);
  sample(12,132000,202001,2);assert(!s.storm.ports.at(business).measured);
  Experiment e;Stamp st;st.complete=true;st.cycle=1;st.atMs=1000;st.targetRevision=1;Observation o;
  for(const auto& key:{"interfaces","down","loops","stp","aggregation"})assert(e.adapter.accept(key,e.fixture(key),st,o));
  o.snapshot.managementConnected=true;o.snapshot.identityVerified=true;o.snapshot.targetRevision=1;
  p.managementPort=management;p.topologyConfirmed=true;p.allowedPorts.insert(management);p.stormBroadcastLimits[management]=1;
  assert(stormIsolationPlan(o.snapshot,p,management,1000).reason=="management_port_protected");
  e.aggregation=true;assert(e.adapter.accept("aggregation",e.fixture("aggregation"),st,o));assert(o.snapshot.aggregation.members.count(business));
  assert(stormIsolationPlan(o.snapshot,p,business,1000).reason=="aggregation_member_protected");
}
int main(int argc,char** argv){
#if ESP_BDI_READ_ONLY
  const auto policy=site::switchPolicy();assert(!policy.automationEnabled);
  auto stormPolicy=policy;stormPolicy.stormBroadcastLimits[business]=1000;
  Experiment readonly(stormPolicy);readonly.looped=true;readonly.run(200000);
  assert(readonly.writes.empty());assert(readonly.runtime.requestOwnedRestore(business,true)=="isolation_not_owned");
  assert(readonly.runtime.json().find("\"enabled\":false")!=std::string::npos);
  std::cout<<"{\"case\":\"readonly_firmware_blocks_loop_and_storm_writes\",\"passed\":true}\n";
#endif
  assert(argc==3);
  for(int index=1;index<argc;++index){
    std::ifstream file(argv[index]);assert(file);
    const std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    Stamp stamp;stamp.complete=true;stamp.cycle=1;stamp.targetRevision=1;stamp.atMs=1000;
    const auto traffic=h3c::parseTraffic(text,stamp);assert(traffic.valid&&traffic.ports.size()==1);
    const auto port=index==1?management:business;
    assert(traffic.ports.at(port).broadcast==(index==1?2584u:492766905u));
    assert(traffic.ports.at(port).total==(index==1?227717u:492767061u));
  }
  std::cout<<"{\"case\":\"captured_hardware_counter_format_replay\",\"passed\":true}\n";
  parserCases();observationFences();
  std::cout<<"{\"case\":\"counter_parse_reset_gap_revision_and_management_fences\",\"passed\":true}\n";
  {
    Experiment e;e.run(40000);assert(e.writes.empty());e.until([&](){return e.is("contained");});assert(e.writes==std::vector<Action>{Action::StormIsolate});
    assert(e.runtime.requestOwnedRestore(business,false)=="physical_repair_not_confirmed");
    assert(e.runtime.requestOwnedRestore(management,true)=="isolation_not_owned");
    e.run(30000);assert(e.writes.size()==1);e.repaired=true;
    assert(e.runtime.requestOwnedRestore(business,true)=="queued");e.until([&](){return e.is("recovered");});assert(e.writes.size()==2&&e.writes[1]==Action::RestoreOwned);
    assert(e.runtime.requestOwnedRestore(business,true)=="isolation_not_owned");
    std::cout<<"{\"case\":\"storm_isolate_confirm_restore\",\"passed\":true,\"switch_commands\":[\"shutdown\",\"undo shutdown\"],\"status\":"<<e.runtime.json()<<"}\n";
  }
  {
    Experiment e;e.run(25000);e.pps=0;e.run(100000);assert(e.writes.empty());std::cout<<"{\"case\":\"single_burst_no_action\",\"passed\":true}\n";
  }
  for(unsigned condition=0;condition<6;++condition){
    auto p=configuredPolicy();if(condition==0)p.stormBroadcastLimits.clear();if(condition==1)p.stormBroadcastLimits[management]=1;
    if(condition==1)p.protectedPorts.insert(business);
    Experiment e(p);e.trunk=condition==2;e.aggregation=condition==3;e.root=condition==4;e.unsupported=condition==5;e.run(160000);assert(e.writes.empty());
  }
  std::cout<<"{\"case\":\"threshold_management_trunk_lag_root_unknown_guards\",\"passed\":true}\n";
  {
    Experiment e;e.failWrites=true;e.run(200000);assert(e.writes.size()==1);assert(e.runtime.requestOwnedRestore(business,true)=="isolation_not_owned");
    e.runtime.configure("192.168.10.2",0,"reboot",true);e.run(150000);assert(e.writes.size()==1);
    std::cout<<"{\"case\":\"ambiguous_write_and_reboot_no_replay\",\"passed\":true}\n";
  }
  {
    Experiment e;e.until([&](){return e.is("contained");});e.failStorage=true;assert(e.runtime.requestOwnedRestore(business,true)=="queued");e.run(80000);assert(e.writes.size()==1);
    // A failed persistence attempt is ambiguous: clearing the I/O failure must
    // not allow replay of the same restore intent within this boot.
    e.failStorage=false;assert(e.runtime.requestOwnedRestore(business,true)=="queued");e.run(80000);assert(e.writes.size()==1);
    std::cout<<"{\"case\":\"restore_persistence_failure_no_command\",\"passed\":true}\n";
  }
  {
    Experiment e;e.until([&](){return e.is("contained");});e.failWrites=true;
    assert(e.runtime.requestOwnedRestore(business,true)=="queued");e.run(160000);assert(e.writes.size()==2);
    assert(e.runtime.requestOwnedRestore(business,true)=="isolation_not_owned");
    e.runtime.configure("192.168.10.2",0,"reboot-after-restore",true);assert(e.runtime.requestOwnedRestore(business,true)=="isolation_not_owned");e.run(160000);assert(e.writes.size()==2);
    std::cout<<"{\"case\":\"ambiguous_restore_no_replay_or_boot_ownership\",\"passed\":true}\n";
  }
  {
    Experiment e;e.until([&](){return e.is("contained");});assert(e.runtime.requestOwnedRestore(business,true)=="queued");e.until([&](){return e.is("needs_review");});assert(e.writes.size()==2);assert(e.runtime.requestOwnedRestore(business,true)=="isolation_not_owned");
    std::cout<<"{\"case\":\"storm_returns_after_restore_no_toggle\",\"passed\":true}\n";
  }
  {
    Experiment e;e.pps=0;e.looped=true;e.until([&](){return e.is("contained");});assert(e.writes[0]==Action::Isolate);e.repaired=true;assert(e.runtime.requestOwnedRestore(business,true)=="queued");e.until([&](){return e.is("recovered");});assert(e.writes.size()==2&&e.writes[1]==Action::RestoreOwned);
    std::cout<<"{\"case\":\"loop_owned_restore_full_runtime\",\"passed\":true}\n";
  }
}
#endif
