#pragma once
#include "../domain/switch_policy.h"
#include "../common/log_text.h"
#include <functional>

namespace switchapp {
// Observes the real fault Runtime. It neither invents counters nor requests
// restore. With a single wired sender, stop confirmation remains an operator act.
class StormAcceptance {
  std::string state_="not_started",reason_="waiting_persistent_admission";
  bool begun=false,terminal_=false,seenConnection=false;
  uint64_t started=0;
  void finish(const std::string& state,const std::string& reason){
    state_=state;reason_=reason;terminal_=true;
    if(log)log("[BDI][STORM-ACCEPTANCE] state="+state_+" reason="+reason_+" port=GigabitEthernet1/0/2");
    if(outcome)outcome(state_);
  }
public:
  std::function<void(const std::string&)> log,outcome;
  void begin(bool durableAdmission,uint64_t now){
    if(begun)return;begun=true;started=now;
    if(!durableAdmission){finish("blocked","one_shot_claim_missing_failed_or_consumed");return;}
    state_="waiting_observation";reason_="prepare_isolated_vlan_and_link";
  }
  bool terminal()const{return terminal_;}
  const std::string& state()const{return state_;}
  void configurationChanged(){if(begun&&seenConnection&&!terminal_)finish("needs_review","management_configuration_changed_no_replay");}
  void tick(const switchmodel::Snapshot& s,uint64_t now,bool connected,bool authorized,switchmodel::RecoveryState recovery,bool owned){
    using namespace switchmodel;
    if(!begun||terminal_)return;
    if(now<started||now-started>=900000){finish("needs_review","fifteen_minute_window_expired");return;}
    if(!connected){if(seenConnection)finish("needs_review","management_lost_no_replay");return;}
    seenConnection=true;
    if(recovery==RecoveryState::NeedsReview){finish("needs_review","fault_runtime_requires_review");return;}
    if(recovery==RecoveryState::Recovered){finish("passed","real_storm_isolation_and_recovery_verified");return;}
    if(recovery==RecoveryState::Contained){
      if(!owned){finish("needs_review","isolation_ownership_missing");return;}
      state_="awaiting_stop_confirmation";reason_="operator_must_confirm_windows_sender_stopped";return;
    }
    if(recovery==RecoveryState::AwaitingRestoreExecution||recovery==RecoveryState::VerifyingRecovery){state_="verifying_recovery";reason_="waiting_up_and_low_broadcast_for_sixty_seconds";return;}
    if(recovery==RecoveryState::AwaitingExecution||recovery==RecoveryState::VerifyingIsolation){state_="verifying_isolation";reason_="waiting_fresh_administrative_down_readbacks";return;}
    if(!usableSnapshot(s,now,45000))return;
    if(!authorized){finish("needs_review","automation_not_authorized");return;}
    Policy p;p.automationEnabled=p.topologyConfirmed=true;p.managementPort="GigabitEthernet1/0/1";
    p.allowedPorts={"GigabitEthernet1/0/2"};p.requiredPvids["GigabitEthernet1/0/2"]=4090;
    const auto mgmt=s.interfaces.ports.find(p.managementPort),port=s.interfaces.ports.find("GigabitEthernet1/0/2");
    if(!policyGate(s,p,"GigabitEthernet1/0/2",now).empty()||mgmt==s.interfaces.ports.end()||mgmt->second.link!=Link::Up||s.loops.ports.count("GigabitEthernet1/0/2")){
      finish("needs_review","identity_vlan_topology_or_management_guard_failed");return;
    }
    if(port->second.link!=Link::Up||s.down.ports.count(port->first)){state_="waiting_link";reason_="port_2_must_have_real_carrier_up";return;}
    const auto measured=s.storm.ports.find(port->first);
    if(!s.storm.valid||!fresh(s.storm.stamp,now,s.targetRevision,45000)||s.storm.stamp.cycle!=s.interfaces.stamp.cycle||measured==s.storm.ports.end()||!measured->second.measured){state_="waiting_baseline";reason_="waiting_measured_broadcast_baseline";return;}
    state_=measured->second.broadcastPps>=20?"observing_high_broadcast":"ready";
    reason_=measured->second.broadcastPps>=20?"fault_strategy_evaluates_real_counter_windows":"bounded_windows_sender_may_start";
  }
  std::string json()const{return "{\"state\":"+journal::quote(state_)+",\"reason\":"+journal::quote(reason_)+",\"port\":\"GigabitEthernet1/0/2\",\"required_vlan\":4090,\"test_threshold_pps\":20,\"window_seconds\":900,\"manual_stop_confirmation_required\":true}";}
};
}
