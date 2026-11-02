#pragma once
#include "bdi_policy.h"
#include "agent/agent.h"
#include <functional>

namespace h3c {
// This agent is a synchronous deliberation stage on the firmware main loop.
// Hardware execution, durable intentions and post-action observation are owned
// by the caller. Its beliefs/plans are separate from the heartbeat agent.
class H3cBdiAgent {
  enum PropositionId : uint8_t {
    LOOP_CONFIRMED, ACTION_ALLOWED, SSH_READY, ISOLATE_GOAL, DISPATCH_ACTION
  };
  BeliefBase beliefs_{3};
  EventBase events_{8};
  PlanBase plans_{1};
  IntentionBase intentions_{1,1};
  Context isolateContext_{3};
  Body isolateBody_{1};
  Agent agent_{&beliefs_,&events_,&plans_,&intentions_};
  const Snapshot* snapshot_=nullptr;
  const Policy* policy_=nullptr;
  uint64_t now_=0,lastCycle_=0,targetRevision_=0;
  ActionPlan candidate_;
  bool loopConfirmed_=false,dispatched_=false;
  std::set<std::string> attempted_;

  static H3cBdiAgent*& current() { static H3cBdiAgent* instance=nullptr; return instance; }
  struct Scope {
    H3cBdiAgent* previous;
    explicit Scope(H3cBdiAgent* agent):previous(current()) { current()=agent; }
    ~Scope() { current()=previous; }
  };
  void emit(const std::string& message) { if(log) log(message); }
  void belief(PropositionId id,bool value,const char* name) {
    if(beliefs_.get_belief_state(Proposition(id))==value) return;
    beliefs_.change_belief_state(Proposition(id),value);
    emit("[BDI][H3C][BELIEF] belief="+std::string(name)+" value="+(value?"true":"false")+" port="+(candidate_.port.empty()?"none":candidate_.port));
  }
  static bool dispatchAction() {
    H3cBdiAgent* self=current();
    if(!self || !self->snapshot_ || !self->policy_ || !self->dispatch || self->dispatched_) return false;
    const auto freshPlan=isolationPlan(*self->snapshot_,*self->policy_,self->candidate_.port,self->now_);
    if(!freshPlan.allowed || self->attempted_.count(freshPlan.port)) {
      self->emit("[BDI][H3C][BLOCKED] port="+self->candidate_.port+" reason="+(freshPlan.allowed?"incident_already_dispatched":freshPlan.reason));
      return false;
    }
    self->attempted_.insert(freshPlan.port);
    self->dispatched_=true;
    self->emit("[BDI][H3C][INTENTION] goal=isolate_loop port="+freshPlan.port+" cycle="+std::to_string(freshPlan.cycle));
    self->dispatch(freshPlan);
    return true;
  }
public:
  std::function<void(const ActionPlan&)> dispatch;
  std::function<void(const std::string&)> log;

  H3cBdiAgent() {
    for(uint8_t id=LOOP_CONFIRMED;id<=SSH_READY;++id) {
      beliefs_.add_belief(Belief(Proposition(id),nullptr,false));
      isolateContext_.add_context(ContextCondition(Proposition(id)));
    }
    isolateBody_.add_instruction(BodyInstruction(BodyType::ACTION,Proposition(DISPATCH_ACTION),dispatchAction));
    plans_.add_plan(Plan(EventOperator::GOAL_ADDITION,Proposition(ISOLATE_GOAL),&isolateContext_,&isolateBody_));
  }
  H3cBdiAgent(const H3cBdiAgent&)=delete;
  H3cBdiAgent& operator=(const H3cBdiAgent&)=delete;

  void reset() {
    if(current()) return;
    attempted_.clear(); lastCycle_=0; targetRevision_=0; candidate_=ActionPlan();
    snapshot_=nullptr; policy_=nullptr; loopConfirmed_=false; dispatched_=false;
    for(uint8_t id=LOOP_CONFIRMED;id<=SSH_READY;++id) beliefs_.change_belief_state(Proposition(id),false);
  }

  void deliberate(const Snapshot& snapshot,const Policy& policy,uint64_t now) {
    if(current()) { emit("[BDI][H3C][BLOCKED] reason=reentrant_deliberation"); return; }
    if(snapshot.targetRevision!=targetRevision_) {
      targetRevision_=snapshot.targetRevision; lastCycle_=0; attempted_.clear();
    }
    const uint64_t cycle=snapshot.interfaces.stamp.cycle;
    if(!cycle || cycle<=lastCycle_) return;
    lastCycle_=cycle; snapshot_=&snapshot; policy_=&policy; now_=now;
    candidate_=ActionPlan(); loopConfirmed_=false; dispatched_=false;
    for(const auto& finding:diagnose(snapshot,now,policy.maxAgeMs)) {
      if(finding.kind==FindingKind::Up && snapshot.loops.enabled) attempted_.erase(finding.port);
      if(finding.kind!=FindingKind::LoopDetected || attempted_.count(finding.port)) continue;
      auto plan=isolationPlan(snapshot,policy,finding.port,now);
      if(!loopConfirmed_ || plan.allowed) { candidate_=plan; loopConfirmed_=true; }
      if(plan.allowed) break;
    }
    belief(LOOP_CONFIRMED,loopConfirmed_,"loop_confirmed");
    belief(ACTION_ALLOWED,candidate_.allowed,"action_allowed");
    belief(SSH_READY,snapshot.sshConnected && snapshot.identityVerified,"ssh_ready");
    if(loopConfirmed_ && !candidate_.allowed)
      emit("[BDI][H3C][BLOCKED] port="+candidate_.port+" reason="+candidate_.reason);
    Scope scope(this);
    if(events_.add_event(Event(EventOperator::GOAL_ADDITION,Proposition(ISOLATE_GOAL)))) {
      for(unsigned step=0;step<32 && (!events_.is_empty() || !intentions_.is_empty());++step) agent_.run();
    } else emit("[BDI][H3C][BLOCKED] reason=event_capacity");
    snapshot_=nullptr; policy_=nullptr;
  }
};
} // namespace h3c
