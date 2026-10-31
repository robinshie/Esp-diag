// Generated-style Embedded-BDI representation of bdi/agentspeak.asl.
#ifndef BDI_CONFIGURATION_H_
#define BDI_CONFIGURATION_H_

#include "bdi/belief_base.h"
#include "bdi/event_base.h"
#include "bdi/intention_base.h"
#include "bdi/plan_base.h"
#include "bdi_functions.h"

enum BdiProposition : uint8_t {
  SERVER_LOCKED = 0,
  SERVER_CRITICAL = 1,
  SERVER_HEALTHY = 2,
  SWITCH_LINK_FAULT = 3,
  START_GOAL = 4,
  ANNOUNCE_READY_ACTION = 5,
  REQUEST_UNLOCK_ACTION = 6,
  REPORT_CRITICAL_ACTION = 7,
  REPORT_HEALTHY_ACTION = 8,
  RECOVER_INTERFACE_ACTION = 9,
};

class AgentSettings {
 private:
  Body bodies_[5];
  Context contexts_[5];
  BeliefBase belief_base_;
  EventBase event_base_;
  PlanBase plan_base_;
  IntentionBase intention_base_;

  void add_action_plan(uint8_t index, EventOperator event_operator,
                       uint8_t event_proposition, uint8_t action_proposition,
                       bool (*action)()) {
    contexts_[index] = Context(0);
    bodies_[index] = Body(1);
    Proposition action_prop(action_proposition);
    BodyInstruction instruction(BodyType::ACTION, action_prop, action);
    bodies_[index].add_instruction(instruction);
    Proposition event_prop(event_proposition);
    Plan plan(event_operator, event_prop, &contexts_[index], &bodies_[index]);
    plan_base_.add_plan(plan);
  }

 public:
  AgentSettings() {
    belief_base_ = BeliefBase(4);
    event_base_ = EventBase(16);
    plan_base_ = PlanBase(5);
    intention_base_ = IntentionBase(10, 4);

    belief_base_.add_belief(Belief(SERVER_LOCKED, nullptr, false));
    belief_base_.add_belief(Belief(SERVER_CRITICAL, nullptr, false));
    belief_base_.add_belief(Belief(SERVER_HEALTHY, nullptr, false));
    belief_base_.add_belief(Belief(SWITCH_LINK_FAULT, nullptr, false));

    event_base_.add_event(Event(EventOperator::GOAL_ADDITION, START_GOAL));

    add_action_plan(0, EventOperator::GOAL_ADDITION, START_GOAL,
                    ANNOUNCE_READY_ACTION, action_announce_ready);
    add_action_plan(1, EventOperator::BELIEF_ADDITION, SERVER_LOCKED,
                    REQUEST_UNLOCK_ACTION, action_request_unlock);
    add_action_plan(2, EventOperator::BELIEF_ADDITION, SERVER_CRITICAL,
                    REPORT_CRITICAL_ACTION, action_report_critical);
    add_action_plan(3, EventOperator::BELIEF_ADDITION, SERVER_HEALTHY,
                    REPORT_HEALTHY_ACTION, action_report_healthy);
    add_action_plan(4, EventOperator::BELIEF_ADDITION, SWITCH_LINK_FAULT,
                    RECOVER_INTERFACE_ACTION, action_recover_interface);
  }

  bool set_belief(BdiProposition proposition, bool state) {
    Proposition prop(proposition);
    if (belief_base_.get_belief_state(prop) == state) return false;

    EventOperator op = state ? EventOperator::BELIEF_ADDITION
                             : EventOperator::BELIEF_DELETION;
    if (!event_base_.add_event(Event(op, prop))) {
      bdi_output("[BDI][WARN] device=" + bdi_device + " reason=BDI_EVENT_CAPACITY");
      return false;
    }
    belief_base_.change_belief_state(prop, state);
    return true;
  }

  BeliefBase* get_belief_base() { return &belief_base_; }
  EventBase* get_event_base() { return &event_base_; }
  PlanBase* get_plan_base() { return &plan_base_; }
  IntentionBase* get_intention_base() { return &intention_base_; }
};

#endif
