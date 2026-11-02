#include "../src/application/switch_runtime.h"
#include "../src/bdi/strategies/loop_discovery.h"
#include "../src/bdi/strategies/loop_isolation.h"
#include "fixture_device_adapter.h"
#include <cassert>
#include <iostream>

using namespace switchmodel;
using namespace switchbdi;
class CountingStrategy final:public IBdiStrategy {
public:
  unsigned evaluations=0,resets=0;
  std::string identifier="test_counter";
  const char* id()const override{return identifier.c_str();}
  void reset()override{evaluations=0;++resets;}
  void evaluate(const Observation& o,const Policy&,uint64_t)override{assert(o.snapshot.identityVerified);++evaluations;}
};
class UngatedProposal final:public IBdiStrategy {
  ActionPlan action;
public:
  bool observer=false;
  const char* id()const override{return "test_untrusted_proposal";}
  StrategyPhase phase()const override{return observer?StrategyPhase::Observe:StrategyPhase::ProposeAction;}
  void reset()override{action=ActionPlan();}
  void evaluate(const Observation& o,const Policy&,uint64_t now)override{
    action.allowed=true;action.action=Action::Isolate;action.port="GigabitEthernet1/0/2";
    action.cycle=o.snapshot.interfaces.stamp.cycle;action.targetRevision=o.snapshot.targetRevision;action.createdAtMs=now;
  }
  const ActionPlan* proposal()const override{return &action;}
};
static Policy policy(){Policy p;p.automationEnabled=true;p.allowedPorts.insert("GigabitEthernet1/0/2");return p;}
struct Harness {
  FixtureDeviceAdapter adapter;
  switchapp::SwitchRuntime runtime;
  switchcli::Result reply;
  unsigned reads=0,writes=0,claims=0;
  uint64_t now=1000;
  std::string mode="normal",identity="FixtureSwitch";
  explicit Harness(StrategySet& strategies):runtime(adapter,strategies,policy()){
    assert(runtime.bindManagementProfile("192.168.10.2",22,"Ethernet1"));
    runtime.configure("192.168.10.2",0,"strategy-test",true);
    runtime.submitRead=[&](const std::string& id,const std::string& key){
      assert(adapter.readCommand(key));++reads;reply=switchcli::Result();reply.id=id;reply.command=key;reply.target="192.168.10.2";
      reply.state="ok";reply.output=key=="identity"?identity:mode;return "queued";
    };
    runtime.resultReady=[&](const std::string& id){return reply.id==id&&reply.done();};
    runtime.result=[&](const std::string&){return reply;};
    runtime.claimIntent=[&](const ActionPlan&,const std::string&){++claims;return true;};
    runtime.submitIsolation=[&](const std::string&,const std::string&){++writes;return "queued";};
  }
  void step(bool connected=true){now+=100;runtime.tick(now,connected,true);}
  void cycles(unsigned count=1){for(unsigned c=0;c<count;++c){const unsigned target=reads+2;for(unsigned i=0;i<300&&reads<target;++i)step();assert(reads==target);step();step();}}
};
int main(){
  {CountingStrategy counters[9];StrategySet set;
   for(unsigned n=0;n<9;++n){counters[n].identifier="counter_"+std::to_string(n);assert(set.add(counters[n])==(n<8));}
   assert(set.size()==8);}
  {CountingStrategy counter,duplicate;StrategySet set;assert(set.add(counter));assert(!set.add(counter));assert(!set.add(duplicate));
   Harness h(set);assert(!set.add(duplicate));h.cycles(3);assert(counter.evaluations==3&&h.writes==0);
   assert(h.runtime.json().find("test-fixture-switch")!=std::string::npos);assert(h.runtime.json().find("test_counter")!=std::string::npos);
   h.step(false);assert(counter.evaluations==0&&counter.resets==2);h.cycles();assert(counter.evaluations==1);}
  {LoopDiscoveryStrategy discovery;LoopIsolationStrategy isolation;StrategySet set{&discovery,&isolation};Harness h(set);
   h.cycles(3);const auto json=h.runtime.json();assert(json.find("lpdt_disabled_no_current_multi_signal_evidence")!=std::string::npos);
   assert(json.find("\"mac_moves_valid\":true")!=std::string::npos);assert(h.writes==0&&h.claims==0);
   std::cout<<json<<'\n';}
  {LoopDiscoveryStrategy discovery;LoopIsolationStrategy isolation;StrategySet set{&discovery,&isolation};Harness h(set);h.mode="loop";
   h.cycles();assert(h.claims==1);h.step();assert(h.writes==1);}
  {UngatedProposal proposal;StrategySet set{&proposal};Harness h(set);h.cycles(3);assert(h.writes==0&&h.claims==0);}
  {UngatedProposal proposal;proposal.observer=true;StrategySet set{&proposal};Harness h(set);h.mode="loop";h.cycles(2);assert(h.writes==0&&h.claims==0);}
  {UngatedProposal proposal;StrategySet set{&proposal};Harness h(set);h.cycles();h.mode="omitted";h.cycles();
   assert(h.runtime.json().find("\"observation_valid\":false")!=std::string::npos&&h.writes==0&&h.claims==0);}
  {UngatedProposal proposal;StrategySet invalid{&proposal,&proposal};assert(!invalid.valid());Harness h(invalid);h.mode="loop";h.cycles(2);assert(h.writes==0&&h.claims==0);}
  {LoopIsolationStrategy isolation;StrategySet set{&isolation};Harness h(set);h.mode="loop";h.identity="UnknownFixture";h.cycles(2);
   assert(h.runtime.json().find("unsupported_device")!=std::string::npos&&h.writes==0&&h.claims==0);}
  FixtureDeviceAdapter adapter;
  assert(!adapter.readCommand("show state;reboot"));assert(adapter.actionCommands(Action::Isolate,"Ethernet2;reboot").empty());
  std::cout<<"Strategy replacement, device replacement, independent authorization and lifecycle passed\n";
}
