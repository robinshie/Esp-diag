#pragma once
#include "bdi_policy.h"
#include <functional>
#include <limits>

namespace h3c {
// A durable claim precedes every switch write. Storage callbacks must operate on
// one atomic string value; read=false means I/O failure, read=true + empty means
// no entry. Single main-loop owner; no concurrent calls or remote clear API.
class ActionLedger {
  struct Progress {
    uint64_t revision=0,cycle=0,firstAt=0,lastAt=0;
    unsigned samples=0,interval=0;
    std::string claim;
  };
  std::map<std::string,Progress> progress_;
  std::set<std::string> blocked_;
  static bool endpointValid(const std::string& endpoint) {
    if(endpoint.empty() || endpoint.size()>192) return false;
    for(unsigned char c:endpoint) if(c<=32 || c>=127) return false;
    return true;
  }
  static bool incidentValid(const std::string& incident) {
    if(incident.empty() || incident.size()>64) return false;
    for(unsigned char c:incident) if(!std::isalnum(c) && c!='_' && c!='-' && c!='.' && c!=':') return false;
    return true;
  }
  static bool uint64(const std::string& text,uint64_t& value) {
    if(text.empty() || text.size()>20) return false;
    value=0;
    for(unsigned char c:text) {
      if(c<'0' || c>'9' || value>(std::numeric_limits<uint64_t>::max()-(c-'0'))/10) return false;
      value=value*10+(c-'0');
    }
    return true;
  }
  static std::string record(const std::string& endpoint,const ActionPlan& plan,const std::string& incident) {
    return "ESP-H3C-CLAIM-1\n"+endpoint+"\n"+plan.port+"\n"+incident+"\n"+
      std::to_string(plan.cycle)+"\n"+std::to_string(plan.targetRevision)+"\n"+std::to_string(plan.createdAtMs)+"\n";
  }
  static bool matchingRecord(const std::string& value,const std::string& endpoint,const std::string& port) {
    if(value.size()>512 || value.empty() || value.back()!='\n') return false;
    std::istringstream in(value); std::vector<std::string> lines; std::string line;
    while(std::getline(in,line)) lines.push_back(line);
    uint64_t cycle,revision,created;
    return lines.size()==7 && lines[0]=="ESP-H3C-CLAIM-1" && lines[1]==endpoint && lines[2]==port &&
      incidentValid(lines[3]) && uint64(lines[4],cycle) && cycle && uint64(lines[5],revision) && revision && uint64(lines[6],created);
  }
  bool block(const std::string& key) {
    if(blocked_.count(key)) return true;
    if(blocked_.size()>=128) { reason="ledger_memory_capacity"; return false; }
    blocked_.insert(key); return true;
  }
  void resetProgress(const std::string& key,const char* why) { progress_.erase(key); reason=why; }
public:
  std::function<bool(const std::string&,std::string&)> read;
  std::function<bool(const std::string&,const std::string&)> write;
  std::function<bool(const std::string&)> erase;
  std::string reason="idle";

  // 15 ASCII bytes, compatible with ESP Preferences keys. A collision cannot
  // overwrite or erase a different endpoint/port: full identity stays in value.
  static std::string storageKey(const std::string& endpoint,const std::string& requested) {
    const auto port=normalizePort(requested);
    if(!endpointValid(endpoint) || port.empty()) return "";
    uint64_t hash=14695981039346656037ULL;
    for(unsigned char c:endpoint+"\n"+port) { hash^=c; hash*=1099511628211ULL; }
    const char* hex="0123456789abcdef"; std::string key(15,'0'); key[0]='a';
    for(int i=14;i>0;--i) { key[size_t(i)]=hex[hash&15]; hash>>=4; }
    return key;
  }
  bool alreadyClaimed(const std::string& endpoint,const std::string& requested) {
    const auto key=storageKey(endpoint,requested);
    if(key.empty()) { reason="invalid_ledger_identity"; return true; }
    if(blocked_.count(key)) { reason="claimed_or_ambiguous"; return true; }
    std::string stored;
    if(!read || !read(key,stored)) { reason="ledger_read_failed"; return true; }
    reason=stored.empty()?"unclaimed":"already_claimed";
    return !stored.empty(); // Corrupt and foreign records also fail closed.
  }
  bool claim(const std::string& endpoint,const ActionPlan& plan,const std::string& incident) {
    const auto key=storageKey(endpoint,plan.port);
    if(key.empty() || !incidentValid(incident) || !plan.allowed || plan.action!=Action::Isolate ||
       normalizePort(plan.port)!=plan.port || !plan.cycle || !plan.targetRevision) {
      reason="invalid_claim"; return false;
    }
    if(alreadyClaimed(endpoint,plan.port)) return false;
    if(!write || !read) { reason="ledger_storage_unavailable"; return false; }
    if(!block(key)) return false;
    const auto value=record(endpoint,plan,incident);
    if(!write(key,value)) { reason="ledger_write_failed"; return false; }
    std::string verified;
    if(!read(key,verified) || verified!=value) { reason="ledger_readback_failed"; return false; }
    progress_.erase(key); reason="claim_durable"; return true;
  }
  void disconnected() { progress_.clear(); reason="observation_progress_reset"; }

  // Clears only a completed incident's replay lock. It never generates a port
  // command or infers that an administratively down port should be reopened.
  bool observeHealthy(const std::string& endpoint,const std::string& requested,const Snapshot& s,const Policy& p,uint64_t now) {
    const auto port=normalizePort(requested), key=storageKey(endpoint,requested);
    if(key.empty()) { reason="invalid_ledger_identity"; return false; }
    std::string stored;
    if(!read || !read(key,stored)) { resetProgress(key,"ledger_read_failed"); return false; }
    if(stored.empty()) { resetProgress(key,blocked_.count(key)?"claim_not_durable_or_clear_ambiguous":"unclaimed"); return false; }
    if(!matchingRecord(stored,endpoint,port)) { resetProgress(key,"ledger_record_invalid_or_collision"); return false; }
    if(!usableSnapshot(s,now,p.maxAgeMs)) { resetProgress(key,"waiting_for_complete_fresh_observation"); return false; }
    const auto found=s.interfaces.ports.find(port);
    bool healthy=found!=s.interfaces.ports.end() && found->second.link==Link::Up && !s.down.ports.count(port) &&
      s.loops.enabled && s.loops.intervalSeconds && !s.loops.ports.count(port) && diagnosePort(s,port,now,p.maxAgeMs).kind==FindingKind::Up;
    const auto stp=s.stp.ports.find(port);
    if(stp!=s.stp.ports.end()) for(const auto& row:stp->second)
      if(row.state!="FORWARDING" || row.protection!="NONE") healthy=false;
    if(!healthy) { resetProgress(key,"incident_not_observed_healthy"); return false; }
    uint64_t at=s.interfaces.stamp.atMs;
    const Stamp* stamps[]={&s.down.stamp,&s.loops.stamp,&s.stp.stamp,&s.aggregation.stamp};
    for(const auto* stamp:stamps) at=std::min(at,stamp->atMs);
    if(!progress_.count(key) && progress_.size()>=128) { reason="ledger_memory_capacity"; return false; }
    auto& progress=progress_[key];
    const auto cycle=s.interfaces.stamp.cycle;
    if(progress.revision!=s.targetRevision || progress.claim!=stored || progress.interval!=s.loops.intervalSeconds ||
       cycle<progress.cycle || at<progress.lastAt || (progress.samples && at-progress.lastAt>p.maxAgeMs)) progress=Progress();
    if(progress.samples && (cycle==progress.cycle || at==progress.lastAt || at-progress.lastAt<p.minObservationGapMs)) {
      reason="duplicate_or_close_observation"; return false;
    }
    if(!progress.samples) { progress.firstAt=at; progress.revision=s.targetRevision; progress.claim=stored; progress.interval=s.loops.intervalSeconds; }
    progress.cycle=cycle; progress.lastAt=at; ++progress.samples;
    if(progress.samples<2 || at-progress.firstAt<uint64_t(progress.interval)*2000) { reason="waiting_for_two_detection_intervals"; return false; }
    if(!erase) { reason="ledger_erase_unavailable"; return false; }
    // Fence a changed record immediately before destructive storage mutation.
    std::string current;
    if(!read(key,current) || current!=stored) { resetProgress(key,"ledger_changed_before_clear"); return false; }
    if(!block(key)) return false;
    if(!erase(key)) { reason="ledger_erase_failed"; return false; }
    std::string cleared;
    if(!read(key,cleared) || !cleared.empty()) { reason="ledger_clear_readback_failed"; return false; }
    progress_.erase(key); blocked_.erase(key); reason="healthy_incident_cleared"; return true;
  }
};
} // namespace h3c
