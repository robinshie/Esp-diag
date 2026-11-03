#pragma once
#include "switch_policy.h"

namespace switchmodel {
// Cumulative counters avoid treating unsupported device rate columns as zero.
class StormMonitor {
  TrafficCounters previous;
  StormEvidence evidence;
public:
  void reset(){previous=TrafficCounters();evidence=StormEvidence();}
  void observe(Snapshot& s,const Policy& p,uint64_t now){
    const auto& current=s.traffic;
    if(!current.valid||!fresh(current.stamp,now,s.targetRevision,p.maxAgeMs)||current.stamp.cycle!=s.interfaces.stamp.cycle){reset();s.storm=StormEvidence();return;}
    if(previous.stamp.targetRevision==current.stamp.targetRevision&&current.stamp.cycle<=previous.stamp.cycle){s.storm=evidence;return;}
    const bool ordered=previous.valid&&previous.stamp.targetRevision==current.stamp.targetRevision&&current.stamp.cycle==previous.stamp.cycle+1&&current.stamp.atMs>previous.stamp.atMs;
    const uint64_t elapsed=ordered?current.stamp.atMs-previous.stamp.atMs:0;
    StormEvidence next;next.stamp=current.stamp;next.valid=true;
    for(const auto& row:current.ports){
      StormPort measured;const auto before=previous.ports.find(row.first);
      const auto limit=p.stormBroadcastLimits.find(row.first);
      if(ordered&&elapsed>=std::max(p.minObservationGapMs,p.stormMinWindowMs)&&elapsed<=p.maxAgeMs&&before!=previous.ports.end()&&row.second.broadcast>=before->second.broadcast&&row.second.total>=before->second.total){
        const auto delta=row.second.broadcast-before->second.broadcast;
        measured.measured=true;measured.broadcastPps=delta/elapsed*1000+(delta%elapsed)*1000/elapsed;
        if(limit!=p.stormBroadcastLimits.end()&&limit->second&&measured.broadcastPps>=limit->second){
          const auto old=evidence.ports.find(row.first);
          measured.rounds=old==evidence.ports.end()?1:std::min(old->second.rounds+1,1000u);
        }
      }
      next.ports[row.first]=measured;
    }
    previous=current;evidence=next;s.storm=next;
  }
};
inline ActionPlan stormIsolationPlan(const Snapshot& s,const Policy& p,const std::string& requested,uint64_t now){
  ActionPlan plan;plan.port=normalizePort(requested);plan.reason=policyGate(s,p,plan.port,now);if(!plan.reason.empty())return plan;
  const auto limit=p.stormBroadcastLimits.find(plan.port);
  if(limit==p.stormBroadcastLimits.end()||!limit->second){plan.reason="storm_threshold_not_configured";return plan;}
  if(s.interfaces.ports.at(plan.port).link!=Link::Up||s.down.ports.count(plan.port)){plan.reason="port_not_confirmed_up";return plan;}
  if(!s.storm.valid||!fresh(s.storm.stamp,now,s.targetRevision,p.maxAgeMs)||s.storm.stamp.cycle!=s.interfaces.stamp.cycle){plan.reason="storm_evidence_missing_or_stale";return plan;}
  const auto measured=s.storm.ports.find(plan.port);
  if(measured==s.storm.ports.end()||!measured->second.measured||measured->second.broadcastPps<limit->second||measured->second.rounds<std::max(2u,p.stormRequiredRounds)){plan.reason="storm_not_sustained";return plan;}
  allowPlan(plan,Action::StormIsolate,s,now);return plan;
}
inline ActionPlan isolationAuthorization(const Snapshot& s,const Policy& p,Action action,const std::string& port,uint64_t now){
  return action==Action::StormIsolate?stormIsolationPlan(s,p,port,now):isolationPlan(s,p,port,now);
}
}
