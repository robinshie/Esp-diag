#pragma once
#include "../strategy.h"
#include "../../common/log_text.h"
#include "agent/agent.h"
#include <functional>

namespace switchbdi {
using namespace switchmodel;
// Independent, read-only BDI goal. Statistical suspicion never authorizes CLI
// writes and is not consumed by isolationPlan or the existing isolation agent.
class LoopDiscoveryStrategy final:public IBdiStrategy {
  enum Id:uint8_t {READY,DIRECT,INDIRECT,MULTI,PERSISTENT,WATCH,QUIET,UNREADY,EXPLORE,REPORT};
  BeliefBase beliefs{8};EventBase events{4};PlanBase plans{5};IntentionBase intentions{1,1};
  Context contexts[5];Body bodies[5];Agent agent{&beliefs,&events,&plans,&intentions};
  MacMoves previousMac;Stp previousStp;
  std::map<std::string,uint64_t> stpChangedAt;
  uint64_t revision=0,lastCycle=0,lastAt=0;
  unsigned streak=0,vlan=0,forward=0,reverse=0,cpuPercent=0;
  bool cpuKnown=false,cpuHigh=false,stpChange=false,macKnown=false,baseline=false;
  std::string state="unknown",reason="waiting_for_observation",port,peer,evidence,previousPair,lastEmission;
  static LoopDiscoveryStrategy*& active(){static LoopDiscoveryStrategy* value=nullptr;return value;}
  static bool confirm(){return active()->publish("confirmed");}
  static bool suspect(){return active()->publish("suspected");}
  static bool watch(){return active()->publish("watch");}
  static bool monitor(){return active()->publish("monitoring");}
  static bool unknown(){return active()->publish("unknown");}
  bool publish(const char* value){
    state=value;
    const auto signature=state+":"+reason+":"+port+":"+peer+":"+std::to_string(streak);
    if(signature!=lastEmission){lastEmission=signature;if(log)log("[BDI][DISCOVERY] state="+state+" reason="+reason+" port="+(port.empty()?"none":port)+" peer="+(peer.empty()?"none":peer)+" vlan="+std::to_string(vlan)+" rounds="+std::to_string(streak)+" evidence="+(evidence.empty()?"none":evidence)+" cycle="+std::to_string(lastCycle)+" action=observe_only");}
    return true;
  }
  void plan(unsigned index,std::initializer_list<Id> conditions,bool(*callback)()){
    contexts[index]=Context(conditions.size());bodies[index]=Body(1);
    for(auto id:conditions)contexts[index].add_context(ContextCondition(Proposition(id)));
    bodies[index].add_instruction(BodyInstruction(BodyType::ACTION,Proposition(REPORT),callback));
    plans.add_plan(Plan(EventOperator::GOAL_ADDITION,Proposition(EXPLORE),&contexts[index],&bodies[index]));
  }
  void decide(bool ready,bool direct,bool multi,bool watching){
    const bool values[]={ready,direct,!direct,multi,streak>=RequiredRounds,watching,!watching&&!multi,!ready};
    const char* names[]={"evidence_ready","direct_loop","indirect_only","multi_signal","persistent","watch_needed","quiet","evidence_missing"};
    for(uint8_t i=0;i<8;++i)if(beliefs.get_belief_state(Proposition(i))!=values[i]){
      beliefs.change_belief_state(Proposition(i),values[i]);
      if(log)log("[BDI][DISCOVERY][BELIEF] belief="+std::string(names[i])+" value="+(values[i]?"true":"false"));
    }
    if(active())return;
    active()=this;
    if(events.add_event(Event(EventOperator::GOAL_ADDITION,Proposition(EXPLORE))))
      for(unsigned i=0;i<32&&(!events.is_empty()||!intentions.is_empty());++i)agent.run();
    active()=nullptr;
  }
  static std::string stpSignature(const std::vector<StpPort>& rows){
    std::string out;for(const auto& row:rows)out+=row.instance+":"+row.role+":"+row.state+":"+row.protection+";";return out;
  }
  bool edge(const Snapshot& s,const Policy& p,const std::string& name,unsigned tag)const{
    const auto it=s.interfaces.ports.find(name);
    if(it==s.interfaces.ports.end()||it->second.link!=Link::Up||it->second.type!='A'||it->second.pvid!=tag||s.down.ports.count(name)||
       name==normalizePort(p.managementPort)||containsPort(p.protectedPorts,name)||!containsPort(p.allowedPorts,name)||s.aggregation.members.count(name))return false;
    const auto stp=s.stp.ports.find(name);
    if(stp!=s.stp.ports.end())for(const auto& row:stp->second)if(row.role=="ROOT"||row.role=="MAST"||row.state!="FORWARDING"||row.protection!="NONE")return false;
    return true;
  }
  void clearHistory(){previousMac=MacMoves();previousStp=Stp();stpChangedAt.clear();previousPair.clear();streak=0;baseline=false;}
public:
  static constexpr unsigned MoveThreshold=3,CpuThreshold=80,RequiredRounds=2;
  LoopDiscoveryStrategy(){
    for(uint8_t i=0;i<8;++i)beliefs.add_belief(Belief(Proposition(i),nullptr,false));
    plan(0,{READY,DIRECT},confirm);
    plan(1,{READY,INDIRECT,MULTI,PERSISTENT},suspect);
    plan(2,{READY,INDIRECT,WATCH},watch);
    plan(3,{READY,INDIRECT,QUIET},monitor);
    plan(4,{UNREADY},unknown);
  }
  LoopDiscoveryStrategy(const LoopDiscoveryStrategy&)=delete;
  LoopDiscoveryStrategy& operator=(const LoopDiscoveryStrategy&)=delete;
  const char* id()const override{return "loop_discovery";}
  void evaluate(const Observation& o,const Policy& p,uint64_t now)override{observe(o.snapshot,o.macMoves,o.cpu,p,now);}
  void reset()override{
    clearHistory();revision=lastCycle=lastAt=0;state="unknown";reason="waiting_for_observation";port.clear();peer.clear();evidence.clear();lastEmission.clear();vlan=forward=reverse=0;macKnown=cpuKnown=cpuHigh=stpChange=false;
    for(uint8_t i=0;i<8;++i)beliefs.change_belief_state(Proposition(i),false);
  }
  void observe(const Snapshot& s,const MacMoves& mac,const CpuUsage& cpu,const Policy& policy,uint64_t now){
    if(revision!=s.targetRevision){reset();revision=s.targetRevision;}
    const auto cycle=s.interfaces.stamp.cycle;
    if(cycle&&cycle<=lastCycle)return;
    const bool consecutive=lastCycle&&cycle==lastCycle+1&&now>=lastAt&&now-lastAt>=policy.minObservationGapMs&&now-lastAt<=policy.maxAgeMs;
    lastCycle=cycle;lastAt=now;port.clear();peer.clear();evidence.clear();vlan=forward=reverse=0;cpuHigh=stpChange=false;
    cpuKnown=cpu.valid&&cpu.stamp.cycle==cycle&&fresh(cpu.stamp,now,s.targetRevision,policy.maxAgeMs);cpuPercent=cpuKnown?cpu.percent:0;
    macKnown=mac.valid&&mac.stamp.cycle==cycle&&fresh(mac.stamp,now,s.targetRevision,policy.maxAgeMs);
    if(!usableSnapshot(s,now,policy.maxAgeMs)){
      clearHistory();reason="incomplete_stale_or_mixed_observation";decide(false,false,false,false);return;
    }
    // Direct evidence still requires a consistent current link/down-cause pair.
    for(const auto& item:s.loops.ports){
      const auto link=s.interfaces.ports.find(item.first),end=s.interfaces.ports.end();const auto down=s.down.ports.find(item.first);
      if(link==end||!s.loops.enabled||(link->second.link==Link::Up&&down!=s.down.ports.end())||
         (link->second.link==Link::Down&&(down==s.down.ports.end()||down->second.cause!=DownCause::Loopback))||
         (link->second.link==Link::AdminDown&&(down==s.down.ports.end()||down->second.cause!=DownCause::Administrative))||
         (link->second.link!=Link::Up&&link->second.link!=Link::Down&&link->second.link!=Link::AdminDown)){
        clearHistory();reason="contradictory_loop_and_port_evidence";decide(false,false,false,false);return;
      }
      clearHistory();port=item.first;evidence="lpdt_current_port,consistent_link_cause";reason="explicit_switch_loop_evidence";decide(true,true,false,false);return;
    }
    for(const auto& item:s.down.ports)if(item.second.cause==DownCause::Loopback){
      const auto link=s.interfaces.ports.find(item.first);
      if(link!=s.interfaces.ports.end()&&link->second.link==Link::Down){clearHistory();port=item.first;reason="switch_loopback_protection_active";evidence="down_loopback_cause,link_down";decide(true,true,false,false);return;}
    }
    if(!macKnown){clearHistory();reason="mac_move_evidence_unavailable";decide(false,false,false,false);return;}
    if(!consecutive||!baseline){
      clearHistory();previousMac=mac;previousStp=s.stp;baseline=true;reason="baseline_established_history_not_replayed";decide(true,false,false,false);return;
    }
    for(const auto& item:s.stp.ports){
      const auto previous=previousStp.ports.find(item.first);
      if(previous!=previousStp.ports.end()&&stpSignature(previous->second)!=stpSignature(item.second))stpChangedAt[item.first]=now;
    }
    for(auto it=stpChangedAt.begin();it!=stpChangedAt.end();)if(now-it->second>policy.maxAgeMs)it=stpChangedAt.erase(it);else ++it;
    struct Pair {std::string a,b;unsigned vlan=0,ab=0,ba=0;};
    std::map<std::string,Pair> pairs;
    bool counterReset=false;
    for(const auto& item:mac.records){
      const auto old=previousMac.records.find(item.first);const auto& r=item.second;
      if(old==previousMac.records.end())continue; // Baseline a new row before using its cumulative count.
      if(r.times<old->second.times||r.time<old->second.time){counterReset=true;break;}
      const auto delta=r.times-old->second.times;if(!delta)continue;
      if(!edge(s,policy,r.current,r.vlan)||!edge(s,policy,r.source,r.vlan))continue;
      const auto a=std::min(r.current,r.source),b=std::max(r.current,r.source);
      auto& pair=pairs[r.mac+":"+std::to_string(r.vlan)+":"+a+":"+b];pair.a=a;pair.b=b;pair.vlan=r.vlan;
      if(r.current==a)pair.ba=delta;else pair.ab=delta;
    }
    previousMac=mac;previousStp=s.stp;
    if(counterReset){streak=0;previousPair.clear();stpChangedAt.clear();reason="mac_counter_reset_rebaseline";decide(true,false,false,false);return;}
    std::string selected;bool multi=false;unsigned strength=0;
    for(const auto& item:pairs){const auto& p=item.second;if(p.ab<MoveThreshold||p.ba<MoveThreshold)continue;
      const bool corroborated=(cpuKnown&&cpu.percent>=CpuThreshold)||stpChangedAt.count(p.a)||stpChangedAt.count(p.b);
      const unsigned score=std::min(p.ab,p.ba);
      if(selected.empty()||(corroborated&&!multi)||(corroborated==multi&&score>strength)){
        selected=item.first;multi=corroborated;strength=score;port=p.a;peer=p.b;vlan=p.vlan;forward=p.ab;reverse=p.ba;
      }
    }
    if(selected.empty()){streak=0;previousPair.clear();reason=s.loops.enabled?"no_current_multi_signal_loop_evidence":"lpdt_disabled_no_current_multi_signal_evidence";decide(true,false,false,false);return;}
    cpuHigh=cpuKnown&&cpu.percent>=CpuThreshold;stpChange=stpChangedAt.count(port)||stpChangedAt.count(peer);
    evidence="bidirectional_mac_deltas,ports_up_same_access_vlan";if(cpuHigh)evidence+=",cpu_high";if(stpChange)evidence+=",recent_stp_change";
    if(multi){streak=selected==previousPair?(streak<RequiredRounds?streak+1:streak):1;previousPair=selected;}else{streak=0;previousPair.clear();}
    reason=!multi?"mac_moves_need_independent_corroboration":streak<RequiredRounds?"multi_signal_waiting_second_round":"persistent_mac_moves_with_corroboration";
    decide(true,false,multi,!multi||streak<RequiredRounds);
  }
  std::string json(uint64_t now,uint64_t maxAge=45000)const override{
    const bool stale=lastAt&&now>=lastAt&&now-lastAt>maxAge;
    return "{\"state\":"+journal::quote(stale?"unknown":state)+",\"reason\":"+journal::quote(stale?"discovery_observation_stale":reason)+",\"goal\":\"explore_loop\",\"action\":\"observe_only\",\"port\":"+journal::quote(port)+",\"peer\":"+journal::quote(peer)+",\"vlan\":"+std::to_string(vlan)+",\"rounds\":"+std::to_string(streak)+",\"cycle\":"+std::to_string(lastCycle)+",\"observed_at_ms\":"+std::to_string(lastAt)+",\"mac_moves_valid\":"+(macKnown?"true":"false")+",\"cpu_valid\":"+(cpuKnown?"true":"false")+",\"cpu_percent\":"+(cpuKnown?std::to_string(cpuPercent):"null")+",\"forward_delta\":"+std::to_string(forward)+",\"reverse_delta\":"+std::to_string(reverse)+",\"evidence\":"+journal::quote(evidence)+"}";
  }
};
} // namespace switchbdi
