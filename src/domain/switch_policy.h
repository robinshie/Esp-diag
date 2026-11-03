#pragma once
#include "switch_state.h"

namespace switchmodel {
enum class FindingKind { Unknown, Up, PhysicalDown, AdministrativeDown, LoopDetected, LoopContained, StpDiscarding, StpProtected, OtherProtection, Standby };
inline const char* findingName(FindingKind kind) {
  switch(kind) {
    case FindingKind::Up: return "up";
    case FindingKind::PhysicalDown: return "physical_down";
    case FindingKind::AdministrativeDown: return "administrative_down";
    case FindingKind::LoopDetected: return "loop_detected";
    case FindingKind::LoopContained: return "loop_contained";
    case FindingKind::StpDiscarding: return "stp_discarding";
    case FindingKind::StpProtected: return "stp_protected";
    case FindingKind::OtherProtection: return "other_protection";
    case FindingKind::Standby: return "standby";
    default: return "unknown";
  }
}
struct Finding { std::string port,reason; FindingKind kind=FindingKind::Unknown; };
inline bool usableSnapshot(const Snapshot& s,uint64_t now,uint64_t maxAgeMs) {
  if(!s.managementConnected || !s.identityVerified || !s.interfaces.valid || !s.down.valid || !s.loops.valid || !s.stp.valid || !s.aggregation.valid) return false;
  const Stamp* stamps[]={&s.interfaces.stamp,&s.down.stamp,&s.loops.stamp,&s.stp.stamp,&s.aggregation.stamp};
  for(const auto* stamp:stamps)
    if(!fresh(*stamp,now,s.targetRevision,maxAgeMs) || stamp->cycle!=s.interfaces.stamp.cycle) return false;
  return true;
}
inline Finding diagnosePort(const Snapshot& s,const std::string& requested,uint64_t now,uint64_t maxAgeMs=45000) {
  Finding f; f.port=normalizePort(requested);
  if(f.port.empty()) { f.reason="invalid_port"; return f; }
  if(!usableSnapshot(s,now,maxAgeMs)) { f.reason="incomplete_stale_or_mixed_observation"; return f; }
  auto it=s.interfaces.ports.find(f.port);
  if(it==s.interfaces.ports.end()) { f.reason="port_missing"; return f; }
  const auto loop=s.loops.ports.find(f.port); const auto down=s.down.ports.find(f.port);
  const auto link=it->second.link;
  if(loop!=s.loops.ports.end()) {
    // Action mode names the configured response. It does not prove the port's
    // current forwarding state (shutdown can automatically recover on a timer).
    const bool contained=down!=s.down.ports.end() &&
      ((link==Link::AdminDown && down->second.cause==DownCause::Administrative) ||
       (link==Link::Down && down->second.cause==DownCause::Loopback));
    f.kind=contained?FindingKind::LoopContained:FindingKind::LoopDetected;
    f.reason=contained?"loop_evidence_with_down_state_confirmed":
      loop->second.action==LoopAction::Block||loop->second.action==LoopAction::Shutdown?
      "loop_report_configured_protection_not_verified":"loopback_detection_reports_loop";
    return f;
  }
  auto stp=s.stp.ports.find(f.port);
  if(stp!=s.stp.ports.end()) for(const auto& row:stp->second) {
    if(row.protection!="NONE") { f.kind=FindingKind::StpProtected; f.reason="stp_guard_"+row.protection; return f; }
    if(row.state=="DISCARDING") { f.kind=FindingKind::StpDiscarding; f.reason="stp_topology_discarding"; return f; }
  }
  if(down!=s.down.ports.end() && down->second.cause==DownCause::StpGuard) {
    f.kind=FindingKind::StpProtected; f.reason="bpdu_guard_shutdown"; return f;
  }
  if(link==Link::AdminDown || (down!=s.down.ports.end() && down->second.cause==DownCause::Administrative)) {
    f.kind=FindingKind::AdministrativeDown; f.reason="administrative_intent_not_owned"; return f;
  }
  if(link==Link::Down) {
    if(down==s.down.ports.end() || down->second.cause==DownCause::Unknown) f.reason="down_cause_unconfirmed";
    else if(down->second.cause==DownCause::Physical) { f.kind=FindingKind::PhysicalDown; f.reason="not_connected_or_physical_failure"; }
    else if(down->second.cause==DownCause::Loopback) { f.kind=FindingKind::LoopContained; f.reason="loopback_shutdown"; }
    else { f.kind=FindingKind::OtherProtection; f.reason="switch_protection_shutdown"; }
    return f;
  }
  if(link==Link::Standby) { f.kind=FindingKind::Standby; f.reason="standby_interface"; return f; }
  if(link==Link::Up) { f.kind=FindingKind::Up; f.reason=s.loops.enabled?"no_current_loop_report":"up_loop_detection_disabled"; }
  return f;
}
inline std::vector<Finding> diagnose(const Snapshot& s,uint64_t now,uint64_t maxAgeMs=45000) {
  std::vector<Finding> findings;
  for(const auto& item:s.interfaces.ports) findings.push_back(diagnosePort(s,item.first,now,maxAgeMs));
  return findings;
}

struct Policy {
  bool automationEnabled=false, topologyConfirmed=false;
  std::string managementPort;
  std::set<std::string> allowedPorts,protectedPorts;
  std::map<std::string,unsigned> requiredPvids;
  uint64_t maxAgeMs=45000, verificationTimeoutMs=120000;
  uint64_t minObservationGapMs=1000;
  // No global guess: only explicitly calibrated per-port thresholds authorize writes.
  std::map<std::string,unsigned> stormBroadcastLimits;
  unsigned stormRequiredRounds=3;
  uint64_t stormMinWindowMs=10000,stormRecoveryHoldMs=60000;
};
inline bool containsPort(const std::set<std::string>& ports,const std::string& name) {
  for(const auto& port:ports) if(normalizePort(port)==name) return true;
  return false;
}
inline std::string policyGate(const Snapshot& s,const Policy& p,const std::string& name,uint64_t now) {
  if(name.empty()) return "invalid_port";
  if(!p.automationEnabled) return "automation_disabled";
  if(!p.topologyConfirmed || normalizePort(p.managementPort).empty()) return "topology_unconfirmed";
  if(name==normalizePort(p.managementPort)) return "management_port_protected";
  if(containsPort(p.protectedPorts,name)) return "configured_port_protected";
  if(!containsPort(p.allowedPorts,name)) return "port_not_authorized";
  if(!usableSnapshot(s,now,p.maxAgeMs)) return "incomplete_stale_or_mixed_observation";
  auto port=s.interfaces.ports.find(name);
  if(port==s.interfaces.ports.end()) return "port_missing";
  if(port->second.type!='A') return "trunk_or_hybrid_protected";
  const auto requiredPvid=p.requiredPvids.find(name);
  if(requiredPvid!=p.requiredPvids.end()&&port->second.pvid!=requiredPvid->second)return "required_access_vlan_mismatch";
  if(s.aggregation.members.count(name)) return "aggregation_member_protected";
  auto stp=s.stp.ports.find(name);
  if(stp!=s.stp.ports.end()) for(const auto& row:stp->second) {
    if(row.role=="ROOT" || row.role=="MAST") return "stp_uplink_protected";
    if(row.protection!="NONE") return "stp_guard_active";
    if(row.state!="FORWARDING") return "stp_already_limits_forwarding";
  }
  return "";
}
enum class Action { None, Isolate, RestoreOwned, StormIsolate };
inline bool isolationAction(Action a){return a==Action::Isolate||a==Action::StormIsolate;}
inline std::string actionCommandKey(Action a,const std::string& port){return (a==Action::RestoreOwned?"restore:":a==Action::StormIsolate?"storm-isolate:":"isolate:")+port;}
struct ActionPlan {
  bool allowed=false; Action action=Action::None;
  std::string port,reason;
  uint64_t cycle=0,targetRevision=0,createdAtMs=0;
};
inline void allowPlan(ActionPlan& plan,Action action,const Snapshot& s,uint64_t now) {
  plan.allowed=true; plan.action=action; plan.reason=action==Action::Isolate?"confirmed_loop_on_authorized_edge":action==Action::StormIsolate?"sustained_broadcast_storm_on_authorized_edge":"restore_owned_isolation_after_repair";
  plan.cycle=s.interfaces.stamp.cycle; plan.targetRevision=s.targetRevision; plan.createdAtMs=now;
}
inline ActionPlan isolationPlan(const Snapshot& s,const Policy& p,const std::string& requested,uint64_t now) {
  ActionPlan plan; plan.port=normalizePort(requested); plan.reason=policyGate(s,p,plan.port,now);
  if(!plan.reason.empty()) return plan;
  if(s.interfaces.ports.at(plan.port).link!=Link::Up || s.down.ports.count(plan.port)) { plan.reason="port_not_confirmed_up"; return plan; }
  if(diagnosePort(s,plan.port,now,p.maxAgeMs).kind!=FindingKind::LoopDetected) { plan.reason="no_uncontained_loop_evidence"; return plan; }
  const auto loop=s.loops.ports.find(plan.port);
  if(loop!=s.loops.ports.end() && (loop->second.action==LoopAction::Block || loop->second.action==LoopAction::Shutdown)) {
    plan.reason="switch_loop_protection_requires_readback"; return plan;
  }
  allowPlan(plan,Action::Isolate,s,now); return plan;
}

enum class RecoveryState { Idle, AwaitingExecution, VerifyingIsolation, Contained, AwaitingRestoreExecution, VerifyingRecovery, Recovered, NeedsReview };
inline const char* recoveryName(RecoveryState state) {
  switch(state) {
    case RecoveryState::AwaitingExecution:return "awaiting_execution";
    case RecoveryState::VerifyingIsolation:return "verifying_isolation";
    case RecoveryState::Contained:return "contained";
    case RecoveryState::AwaitingRestoreExecution:return "awaiting_restore_execution";
    case RecoveryState::VerifyingRecovery:return "verifying_recovery";
    case RecoveryState::Recovered:return "recovered";
    case RecoveryState::NeedsReview:return "needs_review";
    default:return "idle";
  }
}
// Owns one incident. Caller maintains one tracker per port. No automatic replay
// after an ambiguous SSH result; a reboot loses ownership and never opens ports.
class RecoveryTracker {
  ActionPlan plan_;
  Action isolatedAction_=Action::None;
  uint64_t completedAt_=0,lastCycle_=0,lastAt_=0,firstGoodAt_=0;
  unsigned good_=0;
  bool ownsIsolation_=false;
public:
  RecoveryState state=RecoveryState::Idle;
  std::string reason="idle";
  const std::string& port() const { return plan_.port; }
  bool ownsIsolation() const { return ownsIsolation_; }
  Action isolatedAction() const {return isolatedAction_;}
  Action lastAction() const {return plan_.action;}
  bool begin(const ActionPlan& plan) {
    if(!plan.allowed || !isolationAction(plan.action) || normalizePort(plan.port)!=plan.port || !plan.cycle || !plan.targetRevision ||
       (state!=RecoveryState::Idle && state!=RecoveryState::Recovered)) return false;
    plan_=plan; isolatedAction_=plan.action; state=RecoveryState::AwaitingExecution; reason="awaiting_command_result"; ownsIsolation_=false; good_=0; return true;
  }
  void executionResult(bool success,uint64_t now) {
    const bool restore=state==RecoveryState::AwaitingRestoreExecution;
    if(!restore && state!=RecoveryState::AwaitingExecution) return;
    if(!success || now<plan_.createdAtMs) { state=RecoveryState::NeedsReview; reason="execution_failed_or_ambiguous_no_replay"; return; }
    completedAt_=now; lastCycle_=plan_.cycle; lastAt_=now; firstGoodAt_=0; good_=0;
    state=restore?RecoveryState::VerifyingRecovery:RecoveryState::VerifyingIsolation;
    reason="command_completed_waiting_for_observation";
  }
  void observe(const Snapshot& s,const Policy& p,uint64_t now) {
    if(state!=RecoveryState::VerifyingIsolation && state!=RecoveryState::VerifyingRecovery) return;
    if(s.targetRevision!=plan_.targetRevision) { state=RecoveryState::NeedsReview; reason="target_changed"; return; }
    if(now<completedAt_ || now-completedAt_>p.verificationTimeoutMs) { state=RecoveryState::NeedsReview; reason="verification_timeout"; return; }
    if(!usableSnapshot(s,now,p.maxAgeMs)) { good_=0; firstGoodAt_=0; reason="waiting_for_fresh_complete_observation"; return; }
    const auto& stamp=s.interfaces.stamp;
    const Stamp* stamps[]={&s.interfaces.stamp,&s.down.stamp,&s.loops.stamp,&s.stp.stamp,&s.aggregation.stamp};
    for(const auto* collected:stamps) if(collected->atMs<=completedAt_) { reason="pre_action_observation_ignored"; return; }
    if(stamp.cycle<=lastCycle_ || stamp.atMs<=lastAt_ || stamp.atMs-lastAt_<p.minObservationGapMs) return;
    if(stamp.atMs-lastAt_>p.maxAgeMs) { good_=0; firstGoodAt_=0; }
    lastCycle_=stamp.cycle; lastAt_=stamp.atMs;
    const auto it=s.interfaces.ports.find(plan_.port);
    const auto down=s.down.ports.find(plan_.port);
    if(it==s.interfaces.ports.end()) { good_=0; reason="port_missing"; return; }
    bool good=false;
    if(state==RecoveryState::VerifyingIsolation) {
      good=it->second.link==Link::AdminDown && down!=s.down.ports.end() && down->second.cause==DownCause::Administrative;
      reason=good?"administrative_isolation_observed":"isolation_not_observed";
    } else {
      const auto kind=diagnosePort(s,plan_.port,now,p.maxAgeMs).kind;
      good=it->second.link==Link::Up && down==s.down.ports.end() &&
        !s.loops.ports.count(plan_.port) && kind==FindingKind::Up;
      if(isolatedAction_==Action::StormIsolate){
        const auto limit=p.stormBroadcastLimits.find(plan_.port);const auto measured=s.storm.ports.find(plan_.port);
        const bool measuredNow=s.storm.valid&&fresh(s.storm.stamp,now,s.targetRevision,p.maxAgeMs)&&s.storm.stamp.cycle==stamp.cycle&&measured!=s.storm.ports.end()&&measured->second.measured;
        good=good&&measuredNow&&limit!=p.stormBroadcastLimits.end()&&limit->second&&measured->second.broadcastPps<limit->second;
        if(measuredNow&&limit!=p.stormBroadcastLimits.end()&&limit->second&&measured->second.broadcastPps>=limit->second){state=RecoveryState::NeedsReview;reason="storm_recurred_after_restore";return;}
      }else good=good&&s.loops.enabled&&s.loops.intervalSeconds;
      reason=good?(isolatedAction_==Action::StormIsolate?"low_broadcast_observed_waiting_hold_interval":"clear_link_observed_waiting_detection_interval"):"recovery_not_observed";
      if(s.loops.ports.count(plan_.port)) { state=RecoveryState::NeedsReview; reason="loop_recurred_after_restore"; return; }
    }
    if(!good) { good_=0; firstGoodAt_=0; return; }
    if(!good_) firstGoodAt_=stamp.atMs;
    ++good_;
    if(good_<2) return;
    if(state==RecoveryState::VerifyingIsolation) {
      state=RecoveryState::Contained; ownsIsolation_=true; reason=isolatedAction_==Action::StormIsolate?"storm_isolation_verified_source_not_declared_repaired":"isolation_verified_physical_loop_not_declared_repaired";
    } else if(stamp.atMs-firstGoodAt_>=(isolatedAction_==Action::StormIsolate?p.stormRecoveryHoldMs:uint64_t(s.loops.intervalSeconds)*2000)) {
      state=RecoveryState::Recovered; ownsIsolation_=false; reason=isolatedAction_==Action::StormIsolate?"storm_recovery_verified_across_hold_interval":"recovery_verified_across_two_detection_intervals";
    }
  }
  ActionPlan ownedRestorePlan(const Snapshot& s,const Policy& p,uint64_t now,bool repairConfirmed) const {
    ActionPlan plan; plan.port=plan_.port;
    if(state!=RecoveryState::Contained || !ownsIsolation_) { plan.reason="isolation_not_owned"; return plan; }
    if(s.targetRevision!=plan_.targetRevision) { plan.reason="target_changed"; return plan; }
    if(!repairConfirmed) { plan.reason="physical_repair_not_confirmed"; return plan; }
    plan.reason=policyGate(s,p,plan.port,now); if(!plan.reason.empty()) return plan;
    const auto it=s.interfaces.ports.find(plan.port);
    const auto down=s.down.ports.find(plan.port);
    if(it==s.interfaces.ports.end() || it->second.link!=Link::AdminDown || down==s.down.ports.end() || down->second.cause!=DownCause::Administrative) { plan.reason="owned_isolation_state_changed"; return plan; }
    allowPlan(plan,Action::RestoreOwned,s,now); return plan;
  }
  bool beginRestore(const ActionPlan& plan) {
    if(state!=RecoveryState::Contained || !ownsIsolation_ || !plan.allowed || plan.action!=Action::RestoreOwned ||
       plan.port!=plan_.port || plan.targetRevision!=plan_.targetRevision || plan.cycle<plan_.cycle) return false;
    plan_=plan; state=RecoveryState::AwaitingRestoreExecution; reason="awaiting_restore_ssh_result"; return true;
  }
};
} // namespace switchmodel
