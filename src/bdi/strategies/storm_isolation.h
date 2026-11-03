#pragma once
#include "../strategy.h"
#include "../../domain/storm_policy.h"
#include "agent/agent.h"
#include "../../common/log_text.h"

namespace switchbdi {
// Traffic history is reduced by Runtime; this BDI agent proposes a semantic
// action. Runtime checks the same policy again immediately before execution.
class StormIsolationStrategy final:public IBdiStrategy {
  enum Id:uint8_t {CONFIGURED,SUSTAINED,AUTHORIZED,CONTAIN,DISPATCH};
  BeliefBase beliefs{3};EventBase events{4};PlanBase plans{1};IntentionBase intentions{1,1};
  Context context{3};Body body{1};Agent agent{&beliefs,&events,&plans,&intentions};
  switchmodel::ActionPlan candidate;
  bool proposed=false;
  std::string state="disabled",port,samples="[]";
  static StormIsolationStrategy*& current(){static StormIsolationStrategy* value=nullptr;return value;}
  static bool dispatch(){current()->proposed=true;return true;}
public:
  StormIsolationStrategy(){
    for(uint8_t id=CONFIGURED;id<=AUTHORIZED;++id){beliefs.add_belief(Belief(Proposition(id),nullptr,false));context.add_context(ContextCondition(Proposition(id)));}
    body.add_instruction(BodyInstruction(BodyType::ACTION,Proposition(DISPATCH),dispatch));
    plans.add_plan(Plan(EventOperator::GOAL_ADDITION,Proposition(CONTAIN),&context,&body));
  }
  const char* id()const override{return "storm_isolation";}
  StrategyPhase phase()const override{return StrategyPhase::ProposeAction;}
  void reset()override{candidate=switchmodel::ActionPlan();proposed=false;state="disabled";port.clear();samples="[]";for(uint8_t i=0;i<3;++i)beliefs.change_belief_state(Proposition(i),false);}
  const switchmodel::ActionPlan* proposal()const override{return proposed?&candidate:nullptr;}
  void evaluate(const switchmodel::Observation& o,const switchmodel::Policy& p,uint64_t now)override{
    proposed=false;candidate=switchmodel::ActionPlan();port.clear();
    state=p.stormBroadcastLimits.empty()?"disabled":"monitoring";
    samples="[";
    for(const auto& limit:p.stormBroadcastLimits){
      const auto measured=o.snapshot.storm.ports.find(limit.first);
      const auto value=measured==o.snapshot.storm.ports.end()?switchmodel::StormPort():measured->second;
      if(samples.size()>1)samples+=',';
      samples+="{\"port\":"+journal::quote(limit.first)+",\"measured\":"+(value.measured?"true":"false")+",\"broadcast_pps\":"+std::to_string(value.broadcastPps)+",\"limit_pps\":"+std::to_string(limit.second)+",\"consecutive_windows\":"+std::to_string(value.rounds)+"}";
    }
    samples+=']';
    bool sustained=false;
    for(const auto& row:o.snapshot.storm.ports){
      const auto limit=p.stormBroadcastLimits.find(row.first);
      if(limit==p.stormBroadcastLimits.end()||!limit->second||!row.second.measured||row.second.broadcastPps<limit->second)continue;
      state="watch";port=row.first;
      auto plan=switchmodel::stormIsolationPlan(o.snapshot,p,row.first,now);
      if(!plan.allowed)continue;
      candidate=plan;sustained=true;state="storm_confirmed";break;
    }
    const bool values[]={!p.stormBroadcastLimits.empty(),sustained,candidate.allowed};
    for(uint8_t i=0;i<3;++i)beliefs.change_belief_state(Proposition(i),values[i]);
    if(current())return;
    current()=this;
    if(events.add_event(Event(EventOperator::GOAL_ADDITION,Proposition(CONTAIN))))for(unsigned i=0;i<32&&(!events.is_empty()||!intentions.is_empty());++i)agent.run();
    current()=nullptr;
    if(proposed&&log){
      const auto& measured=o.snapshot.storm.ports.at(candidate.port);
      log("[BDI][STORM][INTENTION] port="+candidate.port+" action=shutdown reason="+candidate.reason+" broadcast_pps="+std::to_string(measured.broadcastPps)+" limit_pps="+std::to_string(p.stormBroadcastLimits.at(candidate.port))+" consecutive_windows="+std::to_string(measured.rounds)+" cycle="+std::to_string(candidate.cycle));
    }
  }
  std::string json(uint64_t,uint64_t)const override{return "{\"state\":"+journal::quote(state)+",\"port\":"+journal::quote(port)+",\"ports\":"+samples+"}";}
};
}
