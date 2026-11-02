#pragma once
#include "bdi_agent.h"
#include "../net/ssh_commands.h"
#include "../common/log_text.h"
#include <functional>

namespace h3c {
// Single-owner application loop. The SSH worker, UI and MQTT never run policy.
class Runtime {
  H3cBdiAgent agent;
  Snapshot snapshot;
  Policy policy;
  std::map<std::string,RecoveryTracker> trackers;
  std::map<std::string,std::string> lastFindings,incidents,requests;
  std::string host,boot,request,actionId,actionIncident,actionPort;
  std::string configuredEndpoint,boundEndpoint;
  std::string state="waiting_ssh",reason="waiting_for_authenticated_ssh",port,action="none";
  ActionPlan pendingAction;
  uint64_t cycle=0,now=0,nextCycle=0,requestStarted=0,sequence=0;
  unsigned stage=0;
  bool wasConnected=false,actionQueued=false,shell=true,identity=false;
  bool bindingRevoked=true;
  static const char* commandAt(unsigned n){
    const char* commands[]={"version","interfaces","down","loops","stp","aggregation","logs","cpu","memory"};
    return n<9?commands[n]:"";
  }
  std::string device()const{return "ethernet-switch-"+host;}
  void emit(const std::string& line){if(log)log(line);}
  void setState(const std::string& next,const std::string& why,const std::string& p="",const std::string& a="none"){
    state=next;reason=why;port=p;action=a;
  }
  void dispatch(const ActionPlan& plan){
    if(!actionId.empty()||!shell)return;
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
    Stamp stamp;stamp.cycle=cycle;stamp.atMs=now;stamp.targetRevision=snapshot.targetRevision;stamp.complete=result.state=="ok"&&!result.truncated;
    switch(stage){
      case 0:identity=stamp.complete&&supportedIdentity(result.output);break;
      case 1:snapshot.interfaces=parseInterfaces(result.output,stamp);break;
      case 2:snapshot.down=parseDownCauses(result.output,stamp);break;
      case 3:snapshot.loops=parseLoops(result.output,stamp);break;
      case 4:snapshot.stp=parseStp(result.output,stamp);break;
      case 5:snapshot.aggregation=parseAggregation(result.output,stamp);break;
      case 6:if(analyse&&(result.state=="ok"||result.state=="truncated"))analyse(result.output,now,cycle);break;
      default:break;
    }
    const bool parsed=stage==1?snapshot.interfaces.valid:stage==2?snapshot.down.valid:stage==3?snapshot.loops.valid:stage==4?snapshot.stp.valid:stage==5?snapshot.aggregation.valid:true;
    if(!stamp.complete||!parsed)emit("[BDI][COLLECT] device="+device()+" command="+commandAt(stage)+" state="+(!stamp.complete?result.state:"parse_rejected")+" cycle="+std::to_string(cycle)+" truncated="+(result.truncated?"1":"0"));
  }
  void evaluate(){
    snapshot.identityVerified=identity;
    if(observeLedger)observeLedger(snapshot,policy,now);
    const bool valid=usableSnapshot(snapshot,now,policy.maxAgeMs);
    if(!identity)setState("unsupported_device","device_model_or_firmware_unverified");
    else if(!valid)setState("unknown","incomplete_stale_or_mixed_observation");
    else setState("monitoring",!snapshot.loops.enabled?"loop_detection_disabled":!policy.topologyConfirmed?"management_profile_unbound":"fresh_h3c_observation");
    // A ledger releases a claim only after its independent healthy interval.
    // A terminal tracker then belongs to the old incident and must not reject a
    // later, separately persisted incident on the same physical port.
    for(auto it=trackers.begin();it!=trackers.end();){
      const auto trackerState=it->second.state;
      const bool terminal=trackerState==RecoveryState::Contained||trackerState==RecoveryState::NeedsReview||trackerState==RecoveryState::Recovered;
      if(terminal&&alreadyClaimed&&!alreadyClaimed(it->first)&&valid&&snapshot.loops.enabled&&
         diagnosePort(snapshot,it->first,now,policy.maxAgeMs).kind==FindingKind::Up){
        incidents.erase(it->first);requests.erase(it->first);it=trackers.erase(it);
      }else ++it;
    }
    // Record only changes, retaining the actual port cause without treating unused
    // disconnected ports as faults that a configuration command could repair.
    for(const auto& finding:diagnose(snapshot,now,policy.maxAgeMs)){
      const auto signature=std::string(findingName(finding.kind))+":"+finding.reason;
      if(lastFindings[finding.port]!=signature){
        lastFindings[finding.port]=signature;
        emit("[BDI][OBSERVE] device="+device()+" port="+finding.port+" state="+findingName(finding.kind)+" reason="+finding.reason+" cycle="+std::to_string(cycle)+" incident="+boot+"-"+std::to_string(cycle)+(finding.kind==FindingKind::LoopDetected||finding.kind==FindingKind::LoopContained?" fault=LOOP":""));
      }
      if(finding.kind==FindingKind::LoopDetected)setState("loop_detected",alreadyClaimed&&alreadyClaimed(finding.port)?"persistent_incident_claim_protected":isolationPlan(snapshot,policy,finding.port,now).reason,finding.port);
    }
    bool verifying=false;
    for(auto& item:trackers){
      auto before=item.second.state;item.second.observe(snapshot,policy,now);
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
    cyclePolicy.automationEnabled=policy.automationEnabled&&shell&&!verifying&&actionId.empty();
    for(const auto& item:snapshot.interfaces.ports)if(alreadyClaimed&&alreadyClaimed(item.first))cyclePolicy.protectedPorts.insert(item.first);
    agent.deliberate(snapshot,cyclePolicy,now);
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
  Runtime(){
    agent.dispatch=[this](const ActionPlan& plan){dispatch(plan);};
    agent.log=[this](const std::string& line){emit(line);};
    policy.automationEnabled=true;policy.topologyConfirmed=false;
    for(unsigned p=2;p<=48;++p)policy.allowedPorts.insert("GigabitEthernet1/0/"+std::to_string(p));
    for(unsigned p=49;p<=52;++p)policy.allowedPorts.insert("Ten-GigabitEthernet1/0/"+std::to_string(p));
  }
  // Caller invokes this only for an explicitly established management path, not
  // on every settings change. Changing endpoints revokes the old path even if
  // settings are later changed back. Read-only collection remains available.
  bool bindManagementProfile(const std::string& target,unsigned sshPort,const std::string& managementPort){
    const auto canonical=normalizePort(managementPort);
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
    snapshot=Snapshot();snapshot.targetRevision=uint64_t(revision)+1;
    request.clear();actionId.clear();actionPort.clear();actionIncident.clear();pendingAction=ActionPlan();actionQueued=false;
    trackers.clear();lastFindings.clear();incidents.clear();requests.clear();agent.reset();stage=0;nextCycle=0;wasConnected=false;identity=false;
    setState("waiting_ssh","waiting_for_authenticated_ssh");
  }
  void tick(uint64_t at,bool connected,bool trusted){
    now=at;snapshot.sshConnected=connected&&trusted;
    snapshot.identityVerified=identity&&snapshot.sshConnected;
    if(!snapshot.sshConnected){
      if(wasConnected&&connectionLost)connectionLost();
      if(!actionId.empty())finishAction(false,"disconnected");
      request.clear();stage=0;nextCycle=now;wasConnected=false;
      setState("waiting_ssh",trusted?"ssh_disconnected":"host_key_not_verified");return;
    }
    if(!wasConnected){stage=0;request.clear();nextCycle=now;identity=false;wasConnected=true;}
    if(!actionId.empty()){
      if(!actionQueued){
        const auto gate=isolationPlan(snapshot,policy,actionPort,now);
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
    if(stage==9){evaluate();stage=0;nextCycle=now+10000;return;}
    if(now<nextCycle)return;
    const auto id="bdi-read-"+std::to_string(++sequence);
    auto queued=submitRead?submitRead(id,commandAt(stage)):"unavailable";
    if(queued=="queued"||queued=="running"){if(stage==0)++cycle;request=id;requestStarted=now;}
    else if(queued!="busy")nextCycle=now+1000;
  }
  std::string json()const{
    std::string tables="{";
    auto table=[&](const char* name,const Stamp& stamp,bool valid){
      if(tables.size()>1)tables+=',';
      tables+=journal::quote(name)+":{\"valid\":"+(valid?"true":"false")+",\"complete\":"+(stamp.complete?"true":"false")+",\"cycle\":"+std::to_string(stamp.cycle)+",\"fresh\":"+(fresh(stamp,now,snapshot.targetRevision,policy.maxAgeMs)?"true":"false")+"}";
    };
    table("interfaces",snapshot.interfaces.stamp,snapshot.interfaces.valid);
    table("down",snapshot.down.stamp,snapshot.down.valid);
    table("loops",snapshot.loops.stamp,snapshot.loops.valid);
    table("stp",snapshot.stp.stamp,snapshot.stp.valid);
    table("aggregation",snapshot.aggregation.stamp,snapshot.aggregation.valid);
    tables+='}';
    return "{\"state\":"+journal::quote(state)+",\"reason\":"+journal::quote(reason)+",\"port\":"+journal::quote(port)+",\"action\":"+journal::quote(action)+",\"enabled\":"+(policy.automationEnabled&&policy.topologyConfirmed&&shell?"true":"false")+",\"topology_confirmed\":"+(policy.topologyConfirmed?"true":"false")+",\"management_binding\":"+journal::quote(boundEndpoint)+",\"cycle\":"+std::to_string(cycle)+",\"observation_valid\":"+(usableSnapshot(snapshot,now,policy.maxAgeMs)?"true":"false")+",\"loop_detection_enabled\":"+(!snapshot.loops.valid?"null":snapshot.loops.enabled?"true":"false")+",\"management_port\":"+journal::quote(policy.managementPort)+",\"tables\":"+tables+"}";
  }
};
} // namespace h3c
