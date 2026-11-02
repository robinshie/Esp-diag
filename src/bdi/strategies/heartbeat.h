#pragma once
#include "../heartbeat_strategy.h"
#include "../bdi_configuration.h"
#include "agent/agent.h"

// Dedicated BDI context: ping evidence cannot set server_healthy or clear faults.
namespace switchbdi {
class HeartbeatStrategy final:public IHeartbeatStrategy {
  AgentSettings settings{false};
  Agent agent{settings.get_belief_base(),settings.get_event_base(),settings.get_plan_base(),settings.get_intention_base()};
  uint64_t nextProbe=0;
  bool sshWasConnected=false;
  std::string device="ethernet-switch";
  void drain(){
    auto savedDevice=bdi_device;auto savedAction=bdi_request_action;
    bdi_device=device;bdi_request_action=requestPing;
    for(unsigned i=0;i<32&&(!settings.get_event_base()->is_empty()||!settings.get_intention_base()->is_empty());++i)agent.run();
    bdi_device=savedDevice;bdi_request_action=savedAction;
  }
  void belief(BdiProposition proposition,bool value,const char* name){
    if(settings.set_belief(proposition,value))
      bdi_output("[BDI][EVENT] device="+device+" belief="+name+" value="+(value?"true":"false"));
  }
  void sync(){
    belief(SWITCH_HEARTBEAT_OK,evidence.state==HeartbeatState::State::Healthy,"switch_heartbeat_ok");
    belief(SWITCH_HEARTBEAT_LOST,evidence.alarm,"switch_heartbeat_lost");
    belief(SWITCH_HEARTBEAT_UNKNOWN,evidence.state==HeartbeatState::State::Unknown||evidence.state==HeartbeatState::State::WaitingSsh,"switch_heartbeat_unknown");
    drain();
  }
public:
  HeartbeatState evidence;
  const HeartbeatState& state()const override{return evidence;}
  void reset(const std::string& host)override{
    evidence.reset();nextProbe=0;sshWasConnected=false;device="ethernet-switch-"+host;
    // Clear old-target evidence before publishing unknown for the new target.
    settings.set_belief(SSH_CONNECTED,false);settings.set_belief(SWITCH_HEARTBEAT_OK,false);
    settings.set_belief(SWITCH_HEARTBEAT_LOST,false);settings.set_belief(SWITCH_HEARTBEAT_UNKNOWN,false);
    drain();sync();
  }
  void tick(uint64_t now,bool sshConnected)override{
    evidence.ssh(sshConnected);
    belief(SSH_CONNECTED,sshConnected,"ssh_connected");
    if(sshConnected&&!sshWasConnected)nextProbe=now+HeartbeatState::IntervalMs;
    else if(evidence.armed&&now>=nextProbe){
      if(settings.get_event_base()->add_event(Event(EventOperator::GOAL_ADDITION,VERIFY_HEARTBEAT_GOAL)))nextProbe=now+HeartbeatState::IntervalMs;
    }
    sshWasConnected=sshConnected;evidence.tick(now);sync();
  }
  void probeError()override{evidence.state=HeartbeatState::State::Unknown;evidence.successes=0;evidence.reason="PROBE_ERROR";sync();}
  void observe(bool ok,uint64_t now,uint32_t rtt,const char* reason)override{evidence.observe(ok,now,rtt,reason);sync();}
};

} // namespace switchbdi
