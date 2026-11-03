#define SWITCH_FAULT_EXPERIMENT_ONLY
#include "switch_fault_recovery.cpp"

// Reuse the real H3C parser, EmbeddedBDI, Runtime and durable ledger harness.
// Only the command transport is simulated; no traffic or switch writes occur.
struct LoopExperiment:Experiment {
  std::string target;
  bool truncated=false;
  std::vector<std::string> writtenPorts;
  explicit LoopExperiment(unsigned number):Experiment(site::switchPolicy()),target(std::string(number>=49?"Ten-GigabitEthernet1/0/":"GigabitEthernet1/0/")+std::to_string(number)){
    runtime.submitRead=[this,number](const std::string& id,const std::string& key){
      auto output=fixture(key);
      const std::pair<std::string,std::string> replacements[]={
        {business,target},{"GE1/0/3",std::string(number>=49?"XGE1/0/":"GE1/0/")+std::to_string(number)}};
      for(const auto& pair:replacements){
        size_t at=0;while((at=output.find(pair.first,at))!=std::string::npos){output.replace(at,pair.first.size(),pair.second);at+=pair.second.size();}
      }
      switchcli::Result r;r.id=id;r.command=key;r.target="192.168.10.2";r.output=output;r.state="ok";
      r.truncated=truncated&&key=="loops";replies[id]=r;return "queued";
    };
    runtime.submitAction=[this](const std::string& id,Action action,const std::string& port){
      assert(port==target&&action==Action::Isolate);
      const std::vector<std::string> expected={"system-view",h3ccli::interfaceCommand(target),"shutdown","return"};
      assert(adapter.actionCommands(action,port)==expected);
      writtenPorts.push_back(port);writes.push_back(action);
      switchcli::Result r;r.id=id;r.command=actionCommandKey(action,port);r.target="192.168.10.2";
      r.state=failWrites?"timeout_or_disconnected":"ok";replies[id]=r;
      if(!failWrites)isolated=true;
      return "queued";
    };
  }
};

int main(){
  const auto p=site::switchPolicy();
  assert(p.automationEnabled&&!site::readOnlyAcceptance());
  assert(p.allowedPorts.size()==51&&!p.allowedPorts.count(management));
  for(unsigned n=2;n<=48;++n)assert(p.allowedPorts.count("GigabitEthernet1/0/"+std::to_string(n)));
  for(unsigned n=49;n<=52;++n)assert(p.allowedPorts.count("Ten-GigabitEthernet1/0/"+std::to_string(n)));
  assert(p.stormBroadcastLimits.empty()&&p.requiredPvids.empty());
  for(unsigned number:{2,3,4,24,48,49,52}){
    LoopExperiment e(number);e.run(120000);assert(e.writes.empty()); // High broadcasts alone cannot close a port.
    e.looped=true;e.until([&](){return e.is("contained");});
    assert(e.writes==std::vector<Action>{Action::Isolate});assert(e.writtenPorts==std::vector<std::string>{e.target});
    e.run(180000);assert(e.writes.size()==1&&e.isolated); // No timed restore or repeat shutdown.
  }
  {LoopExperiment e(53);e.looped=true;e.run(180000);assert(e.writes.empty());}
  {LoopExperiment e(2);e.looped=true;e.isolated=true;e.run(180000);assert(e.writes.empty()&&e.isolated);}
  for(unsigned guard=0;guard<3;++guard){
    LoopExperiment e(2);e.looped=true;e.trunk=guard==0;e.aggregation=guard==1;e.root=guard==2;
    e.run(180000);assert(e.writes.empty());
  }
  {LoopExperiment e(2);e.looped=true;e.truncated=true;e.run(180000);assert(e.writes.empty());}
  {LoopExperiment e(2);e.looped=true;e.failStorage=true;e.run(180000);assert(e.writes.empty());}
  {
    LoopExperiment e(2);e.looped=true;e.failWrites=true;e.until([&](){return e.is("needs_review");});
    e.run(120000);e.step(false);e.run(120000);assert(e.writes.size()==1);
    LoopExperiment rebooted(2);rebooted.storage=e.storage;rebooted.looped=true;
    rebooted.run(180000);assert(rebooted.writes.empty());
  }
  {
    LoopExperiment e(3);Stamp stamp;stamp.complete=true;stamp.atMs=1000;stamp.cycle=1;stamp.targetRevision=1;Observation o;
    for(const auto& key:{"interfaces","down","loops","stp","aggregation"})assert(e.adapter.accept(key,e.fixture(key),stamp,o));
    auto policy=p;policy.topologyConfirmed=true;policy.managementPort=management;
    policy.allowedPorts.insert(management);o.snapshot.identityVerified=o.snapshot.managementConnected=true;o.snapshot.targetRevision=1;
    assert(policyGate(o.snapshot,policy,management,1000)=="management_port_protected");
  }
  std::cout<<"17 live-loop authorization and automatic isolation scenarios passed; simulated transport only\n";
}
