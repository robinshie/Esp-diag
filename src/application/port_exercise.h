#pragma once
#include "../adapters/device_adapter.h"
#include "../net/command_transport.h"
#include "../common/log_text.h"
#include <functional>

namespace switchapp {
// Lab-only, explicitly authorized manual exercise. This is NOT fault evidence
// and never enters BDI beliefs or claims automatic physical fault repair.
// Fixed endpoint/ports and ADM baseline; no arbitrary command/port API.
class PortExercise {
  using Action=switchmodel::Action;
  const switchdevice::IDeviceAdapter& adapter;
  switchtransport::ICommandTransport& transport;
  switchmodel::Observation observation;
  std::string pending,key,state_="waiting",reason_="waiting_ssh",target;
  uint64_t cycle=0,started=0,nextAt=0,sequence=0,runStarted=0;
  unsigned query=0,portIndex=0,phase=0,good=0,writes=0;
  bool collecting=false,claimed=false,terminal=false,actionPending=false;
  bool shutdownOnly=false;
  bool carrier[2]={false,false};
  const char* queryAt(unsigned i)const{const char* names[]={"version","interfaces","down","loops","stp","aggregation"};return names[i];}
  std::string port(unsigned i)const{return "GigabitEthernet1/0/"+std::to_string(i+2);}
  const char* phaseName()const{const char* names[]={"prepare_enabled","isolate","restore","restore_baseline"};return names[phase];}
  Action phaseAction()const{return phase%2?Action::Isolate:Action::RestoreOwned;}
  void emit(const std::string& value){if(log)log("[EXERCISE] "+value);}
  void stop(const std::string& why){state_="needs_review";reason_=why;terminal=true;emit("state=needs_review port="+port(portIndex)+" reason="+why+" baseline_restore_required=1");}
  bool adminDown(const switchmodel::Snapshot& s,const std::string& p)const{
    const auto i=s.interfaces.ports.find(p),unused=s.interfaces.ports.end();
    const auto d=s.down.ports.find(p);
    return i!=unused&&i->second.link==switchmodel::Link::AdminDown&&d!=s.down.ports.end()&&d->second.cause==switchmodel::DownCause::Administrative;
  }
  bool guards(uint64_t now){
    auto& s=observation.snapshot;s.identityVerified=observation.identity;
    if(!switchmodel::usableSnapshot(s,now,15000))return false;
    const auto mgmt=s.interfaces.ports.find("GigabitEthernet1/0/1");
    if(mgmt==s.interfaces.ports.end()||mgmt->second.link!=switchmodel::Link::Up)return false;
    switchmodel::Policy p;p.automationEnabled=p.topologyConfirmed=true;p.managementPort="GigabitEthernet1/0/1";p.maxAgeMs=15000;
    p.allowedPorts={port(0),port(1)};
    if(shutdownOnly){p.allowedPorts={port(0)};return switchmodel::policyGate(s,p,port(0),now).empty();}
    for(unsigned i=0;i<2;++i){
      if(!switchmodel::policyGate(s,p,port(i),now).empty()||s.interfaces.ports.at(port(i)).pvid!=10||s.loops.ports.count(port(i)))return false;
    }
    return adminDown(s,port(1-portIndex)); // Never enable both experimental ports.
  }
  void beginCollection(){
    observation=switchmodel::Observation();observation.snapshot.targetRevision=1;observation.snapshot.managementConnected=true;
    ++cycle;query=0;collecting=true;
  }
  void collected(uint64_t now){
    collecting=false;
    if(!guards(now)){stop("identity_topology_or_protection_changed");return;}
    const auto& s=observation.snapshot;
    if(shutdownOnly){
      if(!claimed){
        if(!claimOnce||!claimOnce()){stop("shutdown_claim_missing_failed_or_consumed");return;}
        claimed=true;state_="running";reason_="explicit_operator_shutdown_port_2";
      }
      if(adminDown(s,port(0))){
        emit("state=shutdown_readback port="+port(0)+" sample="+std::to_string(good+1)+" admin_down=1");
        if(++good<2){nextAt=now+2000;beginCollection();return;}
        state_="passed";reason_=writes?"port_2_shutdown_verified":"port_2_already_administratively_down";terminal=true;
        emit("state=passed port="+port(0)+" writes="+std::to_string(writes)+" reason="+reason_);
        if(finishRun)finishRun();return;
      }
      if(writes||good){stop("shutdown_state_not_confirmed_no_replay");return;}
      nextAt=now;return;
    }
    if(!claimed){
      if(!adminDown(s,port(0))||!adminDown(s,port(1))){stop("expected_both_ports_administratively_down");return;}
      if(!claimOnce||!claimOnce()){stop("run_claim_missing_failed_or_already_consumed");return;}
      claimed=true;state_="running";reason_="explicit_user_authorization_ports_2_3";
      emit("state=armed ports=GigabitEthernet1/0/2,GigabitEthernet1/0/3 baseline=both_admin_down persistent_once=1");
      nextAt=now;return;
    }
    const auto& p=port(portIndex);const auto current=s.interfaces.ports.at(p).link;bool expected=false;
    if(phase%2)expected=adminDown(s,p);
    else{
      const auto down=s.down.ports.find(p);
      expected=(current==switchmodel::Link::Up&&down==s.down.ports.end())||
        (current==switchmodel::Link::Down&&down!=s.down.ports.end()&&down->second.cause==switchmodel::DownCause::Physical);
      carrier[portIndex]=carrier[portIndex]||current==switchmodel::Link::Up;
    }
    if(!expected){stop("post_command_state_not_confirmed");return;}
    emit("state=readback port="+p+" phase="+phaseName()+" sample="+std::to_string(good+1)+" admin_down="+(current==switchmodel::Link::AdminDown?"1":"0")+" carrier_up="+(current==switchmodel::Link::Up?"1":"0"));
    if(++good<2){nextAt=now+2000;beginCollection();return;}
    good=0;++phase;
    if(phase==4){
      phase=0;++portIndex;
      if(portIndex==2){
        portIndex=1;terminal=true;state_="passed";reason_="both_ports_returned_to_administrative_down";
        emit("state=passed writes="+std::to_string(writes)+" ports=2,3 baseline_restored=1 carrier_up_2="+(carrier[0]?"1":"0")+" carrier_up_3="+(carrier[1]?"1":"0"));
        if(finishRun)finishRun();return;
      }
    }
    nextAt=now+1000;
  }
public:
  std::function<void(const std::string&)> log;
  // Lab BDI returns a semantic plan; the executor independently checks identity,
  // phase, target and fresh evidence before allowing any SSH write.
  bool requireDeliberation=false;
  std::function<switchmodel::ActionPlan(const switchmodel::Snapshot&,uint64_t,const std::string&,unsigned,bool)> deliberateAction;
  std::function<bool()> claimOnce;
  std::function<void()> finishRun;
  PortExercise(const switchdevice::IDeviceAdapter& device,switchtransport::ICommandTransport& connection):adapter(device),transport(connection){}
  void shutdownPortTwoOnly(){if(!cycle&&!claimed){shutdownOnly=true;phase=1;portIndex=0;}}
  void configure(const std::string& host,unsigned number,const std::string& protocol,const std::string& mode){
    target=host;
    if(host!="192.168.10.2"||number!=22||protocol!="ssh"||mode!="shell"||std::string(transport.kind())!="ssh")stop("fixed_ssh_endpoint_required");
  }
  bool exclusive()const{return !terminal||!pending.empty();}
  const std::string& state()const{return state_;}
  void tick(uint64_t now){
    if(terminal){if(!pending.empty()&&transport.resultReady(pending)){transport.takeResult(pending);pending.clear();}return;}
    if(!transport.connected()||!transport.sessionAccepted()){
      if(claimed||!pending.empty())stop("ssh_lost_no_replay");return;
    }
    if(!runStarted)runStarted=now;
    if(now-runStarted>240000){stop("exercise_deadline_no_replay");return;}
    if(!pending.empty()){
      if(!transport.resultReady(pending)){if(now-started>30000)stop("command_timeout_no_replay");return;}
      const auto result=transport.takeResult(pending);const bool matches=result.id==pending&&result.target==target&&result.command==key;
      pending.clear();
      if(!matches||result.state!="ok"||result.truncated){stop("command_failed_ambiguous_or_identity_mismatch");return;}
      if(actionPending){actionPending=false;good=0;nextAt=now+2000;beginCollection();return;}
      switchmodel::Stamp stamp;stamp.complete=true;stamp.cycle=cycle;stamp.atMs=now;stamp.targetRevision=1;
      if(!adapter.accept(key,result.output,stamp,observation)){stop("readback_parse_rejected");return;}
      if(++query==6){collected(now);return;}
    }
    if(now<nextAt)return;
    if(!claimed&&!collecting)beginCollection();
    const auto id="bdi-exercise-"+std::to_string(++sequence);std::string answer;
    if(collecting){key=queryAt(query);answer=transport.submit(id,key);}
    else{
      if(!guards(now)){stop("evidence_expired_before_write");return;}
      if(requireDeliberation&&!deliberateAction){stop("bdi_planner_missing");return;}
      if(deliberateAction){
        const auto plan=deliberateAction(observation.snapshot,now,port(portIndex),phase,claimed);
        if(!plan.allowed||plan.action!=phaseAction()||plan.port!=port(portIndex)||plan.cycle!=cycle||plan.targetRevision!=1||plan.createdAtMs!=now){stop("bdi_plan_rejected_or_mismatched");return;}
      }
      key=switchmodel::actionCommandKey(phaseAction(),port(portIndex));
      answer=transport.submitAction(id,phaseAction(),port(portIndex));
    }
    if(answer=="busy")return;
    if(answer!="queued"&&answer!="running"){stop("submission_rejected_no_replay");return;}
    pending=id;started=now;actionPending=!collecting;
    if(actionPending){++writes;emit("state=command request_id="+id+" port="+port(portIndex)+" phase="+phaseName()+" action="+(phaseAction()==Action::Isolate?"shutdown":"undo_shutdown"));}
  }
  std::string json()const{return "{\"state\":"+journal::quote(state_)+",\"reason\":"+journal::quote(reason_)+",\"writes\":"+std::to_string(writes)+",\"port\":"+journal::quote(port(portIndex))+",\"phase\":"+journal::quote(phaseName())+",\"carrier_up_2\":"+(carrier[0]?"true":"false")+",\"carrier_up_3\":"+(carrier[1]?"true":"false")+"}";}
};
}
