#pragma once
#include "../bdi/strategy.h"
#include "../adapters/device_adapter.h"
#include "../net/command_result.h"
#include "../common/log_text.h"
#include <functional>

namespace switchapp {
using namespace switchmodel;
// Single-owner application loop. The SSH worker, UI and MQTT never run policy.
class SwitchRuntime {
  const switchdevice::IDeviceAdapter& adapter;
  switchbdi::StrategySet& strategies;
  Observation observation;
  Policy policy;
  std::map<std::string,RecoveryTracker> trackers;
  std::map<std::string,std::string> lastFindings,incidents,requests;
  std::string host,boot,request,actionId,actionIncident,actionPort;
  std::string configuredEndpoint,boundEndpoint;
  std::string state="waiting_ssh",reason="waiting_for_authenticated_ssh",port,action="none";
  ActionPlan pendingAction;
  uint64_t cycle=0,now=0,nextCycle=0,requestStarted=0,sequence=0;
  unsigned stage=0;
  bool wasConnected=false,actionQueued=false,shell=true;
  bool bindingRevoked=true;
  const char* commandAt(unsigned n)const{return adapter.queryAt(n);}
  std::string device()const{return "ethernet-switch-"+host;}
  void emit(const std::string& line){if(log)log(line);}
  void setState(const std::string& next,const std::string& why,const std::string& p="",const std::string& a="none"){
    state=next;reason=why;port=p;action=a;
  }
  void evaluateStrategies(switchbdi::StrategyPhase phase,const Policy& activePolicy){
    for(size_t i=0;i<strategies.size();++i){
      auto* strategy=strategies.at(i);if(strategy->phase()!=phase)continue;
      strategy->evaluate(observation,activePolicy,now);
      if(phase==switchbdi::StrategyPhase::ProposeAction&&activePolicy.automationEnabled&&strategies.valid()){
        const auto* plan=strategy->proposal();if(plan)dispatch(*plan);
      }
    }
  }
  void dispatch(const ActionPlan& plan){
    if(!actionId.empty()||!shell)return;
    const auto checked=isolationPlan(observation.snapshot,policy,plan.port,now);
    if(plan.action!=Action::Isolate||!checked.allowed||plan.cycle!=cycle||plan.cycle!=checked.cycle||plan.targetRevision!=checked.targetRevision||adapter.actionCommands(plan.action,plan.port).empty()){
      emit("[BDI][BLOCKED] device="+device()+" reason=strategy_proposal_rejected");return;
    }
    const auto id="bdi-act-"+std::to_string(++sequence);
    const auto incident=boot+"-"+std::to_string(cycle);
    if(!claimIntent||!claimIntent(plan,incident)){
      setState("needs_review","intent_persistence_failed_or_already_claimed",plan.port);
      emit("[BDI][BLOCKED] device="+device()+" port="+plan.port+" fault=LOOP incident="+incident+" reason="+reason);return;
    }
    if(!trackers[plan.port].begin(plan)){setState("needs_review","tracker_rejected",plan.port);return;}
    pendingAction=plan;actionId=id;actionIncident=incident;actionPort=plan.port;actionQueued=false;
    incidents[plan.port]=incident;requests[plan.port]=id;
    emit("[BDI][CMD] device="+device()+" port="+plan.port+" fault=LOOP incident="+incident+" request_id="+id+" action=shutdown phase=intent_persisted");
    setState("awaiting_execution","intent_persisted",plan.port,"shutdown");
  }
  void finishAction(bool success,const std::string& result){
    trackers[actionPort].executionResult(success,now);
    emit("[BDI][ACK] device="+device()+" port="+actionPort+" fault=LOOP incident="+actionIncident+" request_id="+actionId+" action=shutdown state="+result);
    if(recordOutcome)recordOutcome(actionPort,result);
    setState(success?"verifying_isolation":"needs_review",success?"waiting_for_fresh_readback":"execution_failed_or_ambiguous_no_replay",actionPort,"shutdown");
    actionId.clear();actionPort.clear();request.clear();stage=0;nextCycle=now;
  }
  void accept(const switchcli::Result& result){
    Stamp stamp;stamp.cycle=cycle;stamp.atMs=now;stamp.targetRevision=observation.snapshot.targetRevision;stamp.complete=result.state=="ok"&&!result.truncated;
    const std::string key=commandAt(stage);
    const bool parsed=adapter.accept(key,result.output,stamp,observation);
    if(key=="logs"&&analyse&&(result.state=="ok"||result.state=="truncated"))analyse(result.output,now,cycle);
    if(!stamp.complete||!parsed)emit("[BDI][COLLECT] device="+device()+" command="+commandAt(stage)+" state="+(!stamp.complete?result.state:"parse_rejected")+" cycle="+std::to_string(cycle)+" truncated="+(result.truncated?"1":"0"));
  }
  void evaluate(){
    observation.snapshot.identityVerified=observation.identity;
    evaluateStrategies(switchbdi::StrategyPhase::Observe,policy);
    if(observeLedger)observeLedger(observation.snapshot,policy,now);
    const bool valid=usableSnapshot(observation.snapshot,now,policy.maxAgeMs);
    if(!observation.identity)setState("unsupported_device","device_model_or_firmware_unverified");
    else if(!valid)setState("unknown","incomplete_stale_or_mixed_observation");
    else setState("monitoring",!observation.snapshot.loops.enabled?"loop_detection_disabled":!policy.topologyConfirmed?"management_profile_unbound":adapter.freshObservationReason());
    // A ledger releases a claim only after its independent healthy interval.
    // A terminal tracker then belongs to the old incident and must not reject a
    // later, separately persisted incident on the same physical port.
    for(auto it=trackers.begin();it!=trackers.end();){
      const auto trackerState=it->second.state;
      const bool terminal=trackerState==RecoveryState::Contained||trackerState==RecoveryState::NeedsReview||trackerState==RecoveryState::Recovered;
      if(terminal&&alreadyClaimed&&!alreadyClaimed(it->first)&&valid&&observation.snapshot.loops.enabled&&
         diagnosePort(observation.snapshot,it->first,now,policy.maxAgeMs).kind==FindingKind::Up){
        incidents.erase(it->first);requests.erase(it->first);it=trackers.erase(it);
      }else ++it;
    }
    // Record only changes, retaining the actual port cause without treating unused
    // disconnected ports as faults that a configuration command could repair.
    for(const auto& finding:diagnose(observation.snapshot,now,policy.maxAgeMs)){
      const auto signature=std::string(findingName(finding.kind))+":"+finding.reason;
      if(lastFindings[finding.port]!=signature){
        lastFindings[finding.port]=signature;
        emit("[BDI][OBSERVE] device="+device()+" port="+finding.port+" state="+findingName(finding.kind)+" reason="+finding.reason+" cycle="+std::to_string(cycle)+" incident="+boot+"-"+std::to_string(cycle)+(finding.kind==FindingKind::LoopDetected||finding.kind==FindingKind::LoopContained?" fault=LOOP":""));
      }
      if(finding.kind==FindingKind::LoopDetected)setState("loop_detected",alreadyClaimed&&alreadyClaimed(finding.port)?"persistent_incident_claim_protected":isolationPlan(observation.snapshot,policy,finding.port,now).reason,finding.port);
    }
    bool verifying=false;
    for(auto& item:trackers){
      auto before=item.second.state;item.second.observe(observation.snapshot,policy,now);
      const auto current=item.second.state;
      if(current!=before){
        emit("[BDI][VERIFY] device="+device()+" port="+item.first+" fault=LOOP incident="+incidents[item.first]+" request_id="+requests[item.first]+" state="+recoveryName(current)+" reason="+item.second.reason);
        if(recordOutcome)recordOutcome(item.first,recoveryName(current));
      }
      if(current==RecoveryState::VerifyingIsolation){verifying=true;setState("verifying_isolation",item.second.reason,item.first,"shutdown");}
      else if(current==RecoveryState::Contained&&state=="monitoring")setState("contained",item.second.reason,item.first,"shutdown");
      else if(current==RecoveryState::NeedsReview)setState("needs_review",item.second.reason,item.first,"shutdown");
    }
    Policy cyclePolicy=policy;
    cyclePolicy.automationEnabled=policy.automationEnabled&&shell&&!verifying&&actionId.empty()&&strategies.valid();
    for(const auto& item:observation.snapshot.interfaces.ports)if(alreadyClaimed&&alreadyClaimed(item.first))cyclePolicy.protectedPorts.insert(item.first);
    evaluateStrategies(switchbdi::StrategyPhase::ProposeAction,cyclePolicy);
  }
public:
  std::function<void(const std::string&)> log;
  std::function<std::string(const std::string&,const std::string&)> submitRead,submitIsolation;
  std::function<bool(const std::string&)> resultReady,alreadyClaimed;
  std::function<switchcli::Result(const std::string&)> result;
  std::function<bool(const ActionPlan&,const std::string&)> claimIntent;
  std::function<void(const std::string&,const std::string&)> recordOutcome;
  std::function<void(const Snapshot&,const Policy&,uint64_t)> observeLedger;
  std::function<void(const std::string&,uint64_t,uint64_t)> analyse;
  std::function<void()> connectionLost;
  SwitchRuntime(const switchdevice::IDeviceAdapter& device,switchbdi::StrategySet& set,const Policy& sitePolicy)
    :adapter(device),strategies(set),policy(sitePolicy){
    policy.topologyConfirmed=false;
    strategies.freeze();
    for(size_t i=0;i<strategies.size();++i)strategies.at(i)->log=[this](const std::string& line){emit(line);};
  }
  SwitchRuntime(const SwitchRuntime&)=delete;
  SwitchRuntime& operator=(const SwitchRuntime&)=delete;
  // Caller invokes this only for an explicitly established management path, not
  // on every settings change. Changing endpoints revokes the old path even if
  // settings are later changed back. Read-only collection remains available.
  bool bindManagementProfile(const std::string& target,unsigned sshPort,const std::string& managementPort){
    const auto canonical=adapter.canonicalPort(managementPort);
    if(target.empty()||target.size()>192||target.find_first_of(" \t\r\n")!=std::string::npos||!sshPort||sshPort>65535||canonical.empty()){
      bindingRevoked=true;policy.topologyConfirmed=false;return false;
    }
    boundEndpoint=target+":"+std::to_string(sshPort);policy.managementPort=canonical;bindingRevoked=false;
    policy.topologyConfirmed=configuredEndpoint==boundEndpoint;
    return true;
  }
  void configure(const std::string& target,unsigned revision,const std::string& bootId,bool useShell,unsigned sshPort=22){
    if(connectionLost)connectionLost();
    host=target;boot=bootId.substr(0,16);shell=useShell;
    configuredEndpoint=host+":"+std::to_string(sshPort);
    if(configuredEndpoint!=boundEndpoint)bindingRevoked=true;
    policy.topologyConfirmed=!bindingRevoked&&configuredEndpoint==boundEndpoint;
    observation.snapshot=Snapshot();observation.snapshot.targetRevision=uint64_t(revision)+1;
    strategies.reset();observation.macMoves=MacMoves();observation.cpu=CpuUsage();
    request.clear();actionId.clear();actionPort.clear();actionIncident.clear();pendingAction=ActionPlan();actionQueued=false;
    trackers.clear();lastFindings.clear();incidents.clear();requests.clear();stage=0;nextCycle=0;wasConnected=false;observation.identity=false;
    setState("waiting_ssh","waiting_for_authenticated_ssh");
  }
  void tick(uint64_t at,bool connected,bool trusted){
    now=at;observation.snapshot.sshConnected=connected&&trusted;
    observation.snapshot.identityVerified=observation.identity&&observation.snapshot.sshConnected;
    if(!observation.snapshot.sshConnected){
      if(wasConnected)strategies.disconnected();
      if(wasConnected&&connectionLost)connectionLost();
      if(!actionId.empty())finishAction(false,"disconnected");
      request.clear();stage=0;nextCycle=now;wasConnected=false;
      setState("waiting_ssh",trusted?"ssh_disconnected":"host_key_not_verified");return;
    }
    if(!wasConnected){stage=0;request.clear();nextCycle=now;observation.identity=false;wasConnected=true;}
    if(!actionId.empty()){
      if(!actionQueued){
        const auto gate=isolationPlan(observation.snapshot,policy,actionPort,now);
        if(!gate.allowed){finishAction(false,"evidence_expired_or_changed");return;}
        auto queued=submitIsolation?submitIsolation(actionId,actionPort):"unavailable";
        if(queued=="busy")return;
        if(queued!="queued"&&queued!="running"){finishAction(false,queued);return;}
        actionQueued=true;requestStarted=now;
      }
      if(resultReady&&result&&resultReady(actionId)){
        const auto reply=result(actionId);
        const bool matches=reply.id==actionId&&reply.command=="isolate:"+actionPort&&reply.target==host;
        finishAction(matches&&reply.state=="ok"&&!reply.truncated,matches?reply.state:"result_identity_mismatch");
      }
      else if(now-requestStarted>110000)finishAction(false,"result_timeout");
      return;
    }
    if(!request.empty()){
      if(resultReady&&result&&resultReady(request)){
        auto reply=result(request);
        if(reply.id!=request||reply.command!=commandAt(stage)||reply.target!=host){reply.state="result_identity_mismatch";reply.output.clear();reply.truncated=false;}
        accept(reply);request.clear();++stage;
      }
      else if(now-requestStarted>110000){switchcli::Result failed;failed.state="result_timeout";accept(failed);request.clear();++stage;}
      else return;
    }
    if(stage==adapter.queryCount()){evaluate();stage=0;nextCycle=now+10000;return;}
    if(now<nextCycle)return;
    const auto id="bdi-read-"+std::to_string(++sequence);
    auto queued=submitRead?submitRead(id,commandAt(stage)):"unavailable";
    if(queued=="queued"||queued=="running"){
      if(stage==0){
        ++cycle;const auto revision=observation.snapshot.targetRevision;
        observation=Observation();observation.snapshot.targetRevision=revision;observation.snapshot.sshConnected=connected&&trusted;
      }
      request=id;requestStarted=now;
    }
    else if(queued!="busy")nextCycle=now+1000;
  }
  std::string json()const{
    std::string ids="[";
    for(size_t i=0;i<strategies.size();++i){if(i)ids+=',';ids+=journal::quote(strategies.at(i)->id());}
    ids+=']';
    std::string tables="{";
    auto table=[&](const char* name,const Stamp& stamp,bool valid){
      if(tables.size()>1)tables+=',';
      tables+=journal::quote(name)+":{\"valid\":"+(valid?"true":"false")+",\"complete\":"+(stamp.complete?"true":"false")+",\"cycle\":"+std::to_string(stamp.cycle)+",\"fresh\":"+(fresh(stamp,now,observation.snapshot.targetRevision,policy.maxAgeMs)?"true":"false")+"}";
    };
    table("interfaces",observation.snapshot.interfaces.stamp,observation.snapshot.interfaces.valid);
    table("down",observation.snapshot.down.stamp,observation.snapshot.down.valid);
    table("loops",observation.snapshot.loops.stamp,observation.snapshot.loops.valid);
    table("stp",observation.snapshot.stp.stamp,observation.snapshot.stp.valid);
    table("aggregation",observation.snapshot.aggregation.stamp,observation.snapshot.aggregation.valid);
    tables+='}';
    return "{\"device_profile\":"+journal::quote(adapter.id())+",\"strategy_ids\":"+ids+",\"state\":"+journal::quote(state)+",\"reason\":"+journal::quote(reason)+",\"port\":"+journal::quote(port)+",\"action\":"+journal::quote(action)+",\"enabled\":"+(policy.automationEnabled&&policy.topologyConfirmed&&shell&&strategies.valid()?"true":"false")+",\"topology_confirmed\":"+(policy.topologyConfirmed?"true":"false")+",\"management_binding\":"+journal::quote(boundEndpoint)+",\"cycle\":"+std::to_string(cycle)+",\"observation_valid\":"+(usableSnapshot(observation.snapshot,now,policy.maxAgeMs)?"true":"false")+",\"loop_detection_enabled\":"+(!observation.snapshot.loops.valid?"null":observation.snapshot.loops.enabled?"true":"false")+",\"management_port\":"+journal::quote(policy.managementPort)+",\"tables\":"+tables+",\"discovery\":"+strategies.status("loop_discovery",now,policy.maxAgeMs)+"}";
  }
};
} // namespace switchapp
