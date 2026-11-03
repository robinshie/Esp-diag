#pragma once
#include "agent/agent.h"
#include "../domain/switch_policy.h"
#include <functional>

namespace switchbdi {
// Explicit operator acceptance goal, separate from production fault beliefs.
// The executor owns durable once-only admission, command acknowledgements and
// repeated post-action observations. This agent chooses each semantic action.
class PortAcceptance {
  enum Id:uint8_t {GUARDED,CLAIMED,STATE_MATCHES,PREPARE,ISOLATE,RESTORE,BASELINE,VERIFY_PORT,DISPATCH};
  BeliefBase beliefs{7};EventBase events{4};PlanBase plans{4};IntentionBase intentions{1,1};
  Context contexts[4]{{4},{4},{4},{4}};
  Body bodies[4]{{1},{1},{1},{1}};
  Agent agent{&beliefs,&events,&plans,&intentions};
  switchmodel::ActionPlan candidate;
  bool proposed=false;
  unsigned phase_=0,decisions_=0;
  static PortAcceptance*& current(){static PortAcceptance* value=nullptr;return value;}
  static bool dispatch(){
    auto* self=current();if(!self||self->proposed)return false;
    self->candidate.action=self->phase_%2?switchmodel::Action::Isolate:switchmodel::Action::RestoreOwned;
    self->candidate.allowed=true;self->proposed=true;++self->decisions_;
    if(self->log)self->log("[BDI][ACCEPTANCE][INTENTION] goal=verify_port_roundtrip port="+self->candidate.port+" phase="+std::to_string(self->phase_)+" action="+(self->phase_%2?"shutdown":"undo_shutdown")+" cycle="+std::to_string(self->candidate.cycle));
    return true;
  }
public:
  std::function<void(const std::string&)> log;
  PortAcceptance(){
    for(uint8_t i=0;i<7;++i)beliefs.add_belief(Belief(Proposition(i),nullptr,false));
    for(uint8_t i=0;i<4;++i){
      for(uint8_t common=GUARDED;common<=STATE_MATCHES;++common)contexts[i].add_context(ContextCondition(Proposition(common)));
      contexts[i].add_context(ContextCondition(Proposition(PREPARE+i)));
      bodies[i].add_instruction(BodyInstruction(BodyType::ACTION,Proposition(DISPATCH),dispatch));
      plans.add_plan(Plan(EventOperator::GOAL_ADDITION,Proposition(VERIFY_PORT),&contexts[i],&bodies[i]));
    }
  }
  PortAcceptance(const PortAcceptance&)=delete;
  PortAcceptance& operator=(const PortAcceptance&)=delete;
  unsigned decisions()const{return decisions_;}
  switchmodel::ActionPlan deliberate(const switchmodel::Snapshot& s,uint64_t now,const std::string& port,unsigned phase,bool claimed){
    using namespace switchmodel;
    candidate=ActionPlan();candidate.port=port;candidate.reason="explicit_operator_port_acceptance";
    candidate.cycle=s.interfaces.stamp.cycle;candidate.targetRevision=s.targetRevision;candidate.createdAtMs=now;
    proposed=false;phase_=phase;
    if(current()||phase>3)return candidate;
    Policy p;p.automationEnabled=p.topologyConfirmed=true;p.managementPort="GigabitEthernet1/0/1";p.maxAgeMs=15000;
    p.allowedPorts={"GigabitEthernet1/0/2","GigabitEthernet1/0/3"};
    bool guarded=containsPort(p.allowedPorts,port)&&usableSnapshot(s,now,p.maxAgeMs);
    const auto mgmt=s.interfaces.ports.find(p.managementPort);
    guarded=guarded&&mgmt!=s.interfaces.ports.end()&&mgmt->second.link==Link::Up;
    for(const auto& name:p.allowedPorts){
      const auto i=s.interfaces.ports.find(name);const auto d=s.down.ports.find(name);
      guarded=guarded&&policyGate(s,p,name,now).empty()&&i!=s.interfaces.ports.end()&&i->second.pvid==10&&!s.loops.ports.count(name);
      if(name!=port)guarded=guarded&&i!=s.interfaces.ports.end()&&i->second.link==Link::AdminDown&&d!=s.down.ports.end()&&d->second.cause==DownCause::Administrative;
    }
    bool matches=false;const auto i=s.interfaces.ports.find(port);const auto d=s.down.ports.find(port);
    if(i!=s.interfaces.ports.end()){
      if(phase%2==0)matches=i->second.link==Link::AdminDown&&d!=s.down.ports.end()&&d->second.cause==DownCause::Administrative;
      else matches=(i->second.link==Link::Up&&d==s.down.ports.end())||(i->second.link==Link::Down&&d!=s.down.ports.end()&&d->second.cause==DownCause::Physical);
    }
    const bool values[]={guarded,claimed,matches,phase==0,phase==1,phase==2,phase==3};
    for(uint8_t id=0;id<7;++id)beliefs.change_belief_state(Proposition(id),values[id]);
    if(log)log("[BDI][ACCEPTANCE][BELIEFS] port="+port+" guarded="+(guarded?"1":"0")+" durable_claim="+(claimed?"1":"0")+" state_matches="+(matches?"1":"0")+" phase="+std::to_string(phase));
    current()=this;
    if(events.add_event(Event(EventOperator::GOAL_ADDITION,Proposition(VERIFY_PORT))))
      for(unsigned step=0;step<32&&(!events.is_empty()||!intentions.is_empty());++step)agent.run();
    current()=nullptr;
    return candidate;
  }
};
}
