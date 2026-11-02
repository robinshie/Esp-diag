#include "../src/h3c/bdi_policy.h"
#include "../src/h3c/bdi_agent.h"
#include "../src/h3c/action_ledger.h"
#include "../src/h3c/runtime.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <iterator>

using namespace h3c;
static const std::string port="GigabitEthernet1/0/3";
static Stamp stamp(uint64_t cycle=1,uint64_t at=1000) {
  Stamp s; s.cycle=cycle; s.atMs=at; s.targetRevision=7; s.complete=true; return s;
}
static const std::string briefHeader=
  "Brief information on interfaces in route mode:\nInterface Link Protocol Primary IP Description\nVlan10 UP UP 192.168.10.2\n"
  "Brief information on interfaces in bridge mode:\nLink: ADM - administratively down; Stby - standby\n"
  "Interface Link Speed Duplex Type PVID Description\nGE1/0/1 UP 100M(a) F(a) A 10\n";
static const std::string downHeader=
  "Brief information on interfaces in route mode:\nInterface Link Cause\nVlan1 DOWN Not connected\n"
  "Brief information on interfaces in bridge mode:\nInterface Link Cause\n";
static const std::string stpHeader="MST ID Port Role STP State Protection\n";
static const std::string loopHeader="Loop detection is enabled.\nLoop detection interval is 30 second(s).\n";
static Snapshot snapshot(uint64_t cycle=1,uint64_t at=1000,const std::string& link="UP",const std::string& cause="",bool loop=true) {
  Snapshot s; auto st=stamp(cycle,at); s.targetRevision=7; s.sshConnected=true; s.identityVerified=true;
  s.interfaces=parseInterfaces(briefHeader+"GE1/0/3 "+link+" 1G(a) F(a) A 10\n<H3C>\n",st);
  s.down=parseDownCauses(downHeader+(cause.empty()?"":"GE1/0/3 "+link+" "+cause+"\n")+"<H3C>\n",st);
  s.loops=parseLoops(loopHeader+(loop?"Loop is detected on following interfaces:\nInterface Action mode VLANs\nGigabitEthernet1/0/3 None 10\n":"No loop is detected.\n"),st);
  s.stp=parseStp(stpHeader+(link=="UP"?"0 GigabitEthernet1/0/3 DESI FORWARDING NONE\n":""),st);
  s.aggregation=parseAggregation("display link-aggregation verbose\n<H3C>\n",st);
  assert(s.interfaces.valid && s.down.valid && s.loops.valid && s.stp.valid && s.aggregation.valid);
  return s;
}
static Policy policy() {
  Policy p; p.automationEnabled=true; p.topologyConfirmed=true; p.managementPort="GE1/0/1";
  p.allowedPorts.insert("GE1/0/1"); p.allowedPorts.insert("GE1/0/3"); return p;
}
static void parsing() {
  assert(normalizePort("GE1/0/3")==port);
  assert(normalizePort("GigabitEthernet1/0/3")==port);
  assert(normalizePort("XGE1/0/49")=="Ten-GigabitEthernet1/0/49");
  assert(normalizePort("Ten-GigabitEthernet1/0/49")=="Ten-GigabitEthernet1/0/49");
  for(const char* invalid:{"GE1/0/3\nshutdown","GE1/0/3;shutdown","GE1/0/3-4","GE1/0/0","GE1/0/3/1","Vlan10","Bridge-Aggregation1","GE0/0/1","GE1/0/99999"}) assert(normalizePort(invalid).empty());
  assert(h3ccli::interfaceCommand(port)=="interface GigabitEthernet 1/0/3");
  assert(supportedIdentity("H3C Comware Software, Version 7.1.070, Release 6328P03\nH3C S5130S-52S-EI uptime"));
  assert(!supportedIdentity("H3C S5120V2-LI"));
  auto s=snapshot(); assert(usableSnapshot(s,1000,45000));
  auto incomplete=stamp(); incomplete.complete=false;
  assert(!parseInterfaces(briefHeader+"GE1/0/3 UP 1G F A 10\n",incomplete).valid);
  assert(!parseLoops(loopHeader,stamp()).valid); // Missing actual loop status.
  assert(!parseLoops("No loop is detected.\n",stamp()).valid);
  assert(!parseLoops(loopHeader+"No loop is detected.\nInterface Action mode VLANs\nGE1/0/3 None 10\n",stamp()).valid);
  assert(!parseLoops(loopHeader+"Interface Action mode VLANs\nGE1/0/3 Unexpected 10\n",stamp()).valid);
  assert(!parseLoops(loopHeader+"No loop is detected.\n% Unrecognized command",stamp()).valid);
  assert(!parseInterfaces(briefHeader+"GE1/0/3 nonsense 1G F A 10\n",stamp()).valid);
  assert(!parseInterfaces(briefHeader+"GE1/0/3 UP 1G F A 10\nGE1/0/3 ADM auto A A 10\n",stamp()).valid);
  assert(parseDownCauses(downHeader,stamp()).valid);
  assert(!parseDownCauses("display interface brief down\n<H3C>\n",stamp()).valid);
  assert(!parseStp(stpHeader+"0 GigabitEthernet1/0/3 ROOT mystery NONE\n",stamp()).valid);
  assert(parseStp("STP is disabled.\n",stamp()).valid);
  assert(!parseStp("\n<H3C>\n",stamp()).valid);
  auto agg=parseAggregation("Loadsharing Type: Shar -- Loadsharing, NonS -- Non-Loadsharing\nPort Status: S -- Selected, U -- Unselected, I -- Individual\nAggregate Interface: Bridge-Aggregation10\nLocal:\nPort Status Priority Index Oper-Key Flag\nGE1/0/3(R) S 32768 61 2 {ACDEF}\nRemote:\nGE1/0/3 32768 111 2 0x8000, 000f-e267-57ad {ACDEF}\n",stamp());
  assert(agg.valid && agg.members.count(port));
  assert(parseAggregation("Loadsharing Type: Shar -- Loadsharing, NonS -- Non-Loadsharing\nPort Status: S -- Selected, U -- Unselected, I -- Individual\n",stamp()).valid);
  assert(!parseAggregation("New unrecognized output format\n",stamp()).valid);
  assert(!parseAggregation("% Unrecognized command\n",stamp()).valid);
  const auto unconfigured=parseLoops("display loopback-detection\nLPDT is not configured.\n<H3C>\n",stamp());
  assert(unconfigured.valid&&!unconfigured.enabled&&unconfigured.ports.empty());
  assert(!parseLoops("LPDT is not configured.\nLoop detection is enabled.\nNo loop is detected.\n",stamp()).valid);
  assert(!parseLoops("LPDT is not configured.\nInterface Action mode VLANs\nGE1/0/3 None 10\n",stamp()).valid);
  assert(!parseLoops("LPDT is not configured.\n",incomplete).valid);
  const auto empty=parseAggregation("display link-aggregation verbose\nNo link aggregation group exists.\n<H3C>\n",stamp());
  assert(empty.valid&&empty.members.empty());
  assert(!parseAggregation("No link aggregation group exists.\nAggregate Interface: Bridge-Aggregation10\nPort Status: S -- Selected\nGE1/0/3 S\n",stamp()).valid);
  assert(!parseAggregation("No link aggregation group exists.\nUnknown output\n",stamp()).valid);
  assert(!parseAggregation("No link aggregation group exists.\n",incomplete).valid);
}
static void diagnosisAndGates() {
  auto s=snapshot(); auto p=policy();
  auto plan=isolationPlan(s,p,port,1000); assert(plan.allowed && h3c::DeviceAdapter().actionCommands(plan.action,plan.port).size()==4 && h3c::DeviceAdapter().actionCommands(plan.action,plan.port)[2]=="shutdown");
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopDetected);
  assert(isolationPlan(s,p,"GE1/0/1",1000).reason=="management_port_protected");
  assert(!isolationPlan(s,p,"GE1/0/3\nreturn",1000).allowed);
  p.automationEnabled=false; assert(!isolationPlan(s,p,port,1000).allowed); p=policy();
  p.topologyConfirmed=false; assert(!isolationPlan(s,p,port,1000).allowed); p=policy();
  p.allowedPorts.clear(); assert(isolationPlan(s,p,port,1000).reason=="port_not_authorized"); p=policy();
  p.protectedPorts.insert("GE1/0/3"); assert(!isolationPlan(s,p,port,1000).allowed); p=policy();
  s.interfaces.ports[port].type='T'; assert(isolationPlan(s,p,port,1000).reason=="trunk_or_hybrid_protected"); s=snapshot();
  s.aggregation.members.insert(port); assert(isolationPlan(s,p,port,1000).reason=="aggregation_member_protected"); s=snapshot();
  s.stp.ports[port][0].role="ROOT"; assert(isolationPlan(s,p,port,1000).reason=="stp_uplink_protected"); s=snapshot();
  s.stp.ports[port][0].state="DISCARDING"; assert(!isolationPlan(s,p,port,1000).allowed); s=snapshot();
  s.stp.ports[port][0].protection="LOOP"; assert(!isolationPlan(s,p,port,1000).allowed); s=snapshot();
  s.stp.stamp.cycle=2; assert(!isolationPlan(s,p,port,1000).allowed); s=snapshot();
  s.down.stamp.targetRevision=6; assert(!isolationPlan(s,p,port,1000).allowed); s=snapshot();
  s.loops.stamp.atMs=1001; assert(!isolationPlan(s,p,port,1000).allowed); s=snapshot();
  assert(!isolationPlan(s,p,port,46001).allowed);
  s.sshConnected=false; assert(diagnosePort(s,port,1000).kind==FindingKind::Unknown); s=snapshot();
  s.identityVerified=false; assert(!isolationPlan(s,p,port,1000).allowed); s=snapshot();
  s.loops.ports[port].action=LoopAction::Block;
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopDetected && isolationPlan(s,p,port,1000).reason=="switch_loop_protection_requires_readback");
  s.loops.ports[port].action=LoopAction::Shutdown;
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopDetected && isolationPlan(s,p,port,1000).reason=="switch_loop_protection_requires_readback");
  s.down.ports[port].cause=DownCause::Loopback;
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopDetected); // UP conflicts with a down cause.
  s.interfaces.ports[port].link=Link::Down;
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopContained);
  s=snapshot(1,1000,"ADM","",true);
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopDetected); // Missing independent down-cause readback.
  s=snapshot(1,1000,"DOWN","Not connected",false);
  assert(diagnosePort(s,port,1000).kind==FindingKind::PhysicalDown && !isolationPlan(s,p,port,1000).allowed);
  s=snapshot(1,1000,"ADM","Administratively",false);
  assert(diagnosePort(s,port,1000).kind==FindingKind::AdministrativeDown && !isolationPlan(s,p,port,1000).allowed);
  s=snapshot(1,1000,"DOWN","STP DOWN",false);
  assert(diagnosePort(s,port,1000).kind==FindingKind::StpProtected && !isolationPlan(s,p,port,1000).allowed);
  s=snapshot(1,1000,"DOWN","DOWN (Loopback detection down)",false);
  assert(diagnosePort(s,port,1000).kind==FindingKind::LoopContained);
  s=snapshot(1,1000,"DOWN","Storm-Constrain",false);
  assert(diagnosePort(s,port,1000).kind==FindingKind::OtherProtection);
  s=snapshot(1,1000,"UP","",false); s.stp.ports[port][0].state="DISCARDING"; s.stp.ports[port][0].role="ALTE";
  assert(diagnosePort(s,port,1000).kind==FindingKind::StpDiscarding);
  s.stp.ports[port][0].protection="LOOP";
  assert(diagnosePort(s,port,1000).kind==FindingKind::StpProtected);
}
static void verifiedLifecycle() {
  auto s=snapshot(); const auto p=policy(); RecoveryTracker tracker;
  assert(!tracker.ownedRestorePlan(s,p,1000,true).allowed);
  assert(tracker.begin(isolationPlan(s,p,port,1000)));
  tracker.executionResult(true,1500);
  assert(tracker.state==RecoveryState::VerifyingIsolation && !tracker.ownsIsolation()); // ACK is not recovery.
  tracker.observe(s,p,2000); assert(tracker.state==RecoveryState::VerifyingIsolation);
  auto isolated=snapshot(2,3000,"ADM","Administratively",true);
  tracker.observe(isolated,p,3000); assert(!tracker.ownsIsolation());
  tracker.observe(isolated,p,4500); assert(!tracker.ownsIsolation()); // Replayed cycle is not new evidence.
  isolated=snapshot(3,5000,"ADM","Administratively",true); tracker.observe(isolated,p,5000);
  assert(tracker.state==RecoveryState::Contained && tracker.ownsIsolation());
  assert(tracker.reason=="isolation_verified_physical_loop_not_declared_repaired");
  assert(!tracker.ownedRestorePlan(isolated,p,5000,false).allowed);
  auto restore=tracker.ownedRestorePlan(isolated,p,5000,true); assert(restore.allowed && h3c::DeviceAdapter().actionCommands(restore.action,restore.port)[2]=="undo shutdown");
  assert(tracker.beginRestore(restore)); tracker.executionResult(true,5500);
  assert(tracker.state==RecoveryState::VerifyingRecovery);
  tracker.observe(snapshot(4,7000,"UP","",false),p,7000);
  tracker.observe(snapshot(5,9000,"UP","",false),p,9000);
  assert(tracker.state==RecoveryState::VerifyingRecovery); // Wait two detector periods.
  tracker.observe(snapshot(6,37000,"UP","",false),p,37000);
  assert(tracker.state==RecoveryState::VerifyingRecovery);
  tracker.observe(snapshot(7,67000,"UP","",false),p,67000);
  assert(tracker.state==RecoveryState::Recovered && !tracker.ownsIsolation());
}
static void failuresDoNotReplayOrRecover() {
  auto s=snapshot(); auto p=policy(); auto plan=isolationPlan(s,p,port,1000);
  RecoveryTracker failed; assert(failed.begin(plan)); failed.executionResult(false,1500);
  assert(failed.state==RecoveryState::NeedsReview && !failed.begin(plan));
  RecoveryTracker timeout; assert(timeout.begin(plan)); timeout.executionResult(true,1500);
  timeout.observe(snapshot(2,122000),p,122000); assert(timeout.state==RecoveryState::NeedsReview);
  RecoveryTracker target; assert(target.begin(plan)); target.executionResult(true,1500);
  s.targetRevision=8; target.observe(s,p,2500); assert(target.state==RecoveryState::NeedsReview);
  RecoveryTracker mixed; s=snapshot(); assert(mixed.begin(plan)); mixed.executionResult(true,1500);
  auto isolated=snapshot(2,3000,"ADM","Administratively"); isolated.loops.stamp.atMs=1400;
  mixed.observe(isolated,p,3000); assert(!mixed.ownsIsolation());
  isolated=snapshot(3,5000,"ADM","Administratively"); mixed.observe(isolated,p,5000); assert(!mixed.ownsIsolation());
  isolated=snapshot(4,7000,"ADM","Administratively"); mixed.observe(isolated,p,7000); assert(mixed.ownsIsolation());
  auto restore=mixed.ownedRestorePlan(isolated,p,7000,true); assert(mixed.beginRestore(restore)); mixed.executionResult(true,7500);
  mixed.observe(snapshot(5,9000),p,9000); assert(mixed.state==RecoveryState::NeedsReview && mixed.reason=="loop_recurred_after_restore");
  RecoveryTracker rebooted; assert(!rebooted.ownedRestorePlan(isolated,p,7000,true).allowed);
}
static void actualEmbeddedBdi() {
  H3cBdiAgent agent; auto p=policy(); unsigned dispatched=0; std::vector<std::string> logs;
  agent.log=[&](const std::string& message) { logs.push_back(message); };
  agent.dispatch=[&](const ActionPlan& plan) { ++dispatched; assert(plan.allowed && plan.action==Action::Isolate && plan.port==port); };
  agent.deliberate(snapshot(1,1000),p,1000); assert(dispatched==1);
  assert(std::any_of(logs.begin(),logs.end(),[](const std::string& line){ return line.find("[INTENTION] goal=isolate_loop")!=std::string::npos; }));
  agent.deliberate(snapshot(1,1000),p,1000); assert(dispatched==1);
  agent.deliberate(snapshot(2,3000),p,3000); assert(dispatched==1); // Persistent loop cannot replay writes.
  agent.deliberate(snapshot(3,5000,"ADM","Administratively"),p,5000); assert(dispatched==1);
  agent.deliberate(snapshot(4,7000,"UP","",false),p,7000); assert(dispatched==1); // New healthy observation ends incident.
  agent.deliberate(snapshot(5,9000),p,9000); assert(dispatched==2);

  H3cBdiAgent gated; unsigned blockedCalls=0; gated.dispatch=[&](const ActionPlan&) { ++blockedCalls; };
  p.automationEnabled=false; gated.deliberate(snapshot(1,1000),p,1000); assert(blockedCalls==0);
  p=policy(); auto s=snapshot(2,3000); s.interfaces.ports[port].type='T'; gated.deliberate(s,p,3000); assert(blockedCalls==0);
  s=snapshot(3,5000); s.sshConnected=false; gated.deliberate(s,p,5000); assert(blockedCalls==0);
  gated.deliberate(snapshot(4,7000,"DOWN","Not connected",false),p,7000); assert(blockedCalls==0);
  // The context was eligible, but the callback must recheck a changed policy.
  gated.log=[&](const std::string& line) { if(line.find("belief=action_allowed value=true")!=std::string::npos) p.automationEnabled=false; };
  gated.deliberate(snapshot(5,9000),p,9000); assert(blockedCalls==0);
  gated.log=nullptr; p=policy(); gated.deliberate(snapshot(6,11000),p,11000); assert(blockedCalls==1);
  H3cBdiAgent oneAtATime; unsigned calls=0;
  oneAtATime.dispatch=[&](const ActionPlan&){++calls;}; p.allowedPorts.insert("GE1/0/4");
  s=snapshot(1,1000); Interface another=s.interfaces.ports.at(port); another.name="GigabitEthernet1/0/4";
  s.interfaces.ports[another.name]=another; s.loops.ports[another.name]=s.loops.ports.at(port);
  oneAtATime.deliberate(s,p,1000); assert(calls==1);
}
struct LedgerStore {
  std::map<std::string,std::string> values;
  bool readOk=true,writeOk=true,eraseOk=true,corruptWrite=false,eraseNoop=false;
  unsigned writes=0,erases=0;
  void attach(ActionLedger& ledger) {
    ledger.read=[this](const std::string& key,std::string& value) { if(!readOk)return false; auto it=values.find(key); value=it==values.end()?"":it->second; return true; };
    ledger.write=[this](const std::string& key,const std::string& value) { ++writes; if(!writeOk)return false; values[key]=corruptWrite?"corrupt":value; return true; };
    ledger.erase=[this](const std::string& key) { ++erases; if(!eraseOk)return false; if(!eraseNoop)values.erase(key); return true; };
  }
};
static const std::string endpoint="192.168.10.2:22";
static Snapshot healthy(uint64_t cycle,uint64_t at) { return snapshot(cycle,at,"UP","",false); }
static bool observe(ActionLedger& ledger,uint64_t cycle,uint64_t at) { return ledger.observeHealthy(endpoint,port,healthy(cycle,at),policy(),at); }
static void durableLedger() {
  LedgerStore store; ActionLedger ledger; store.attach(ledger);
  const auto plan=isolationPlan(snapshot(),policy(),port,1000);
  const auto key=ActionLedger::storageKey(endpoint,"GE1/0/3");
  assert(key.size()==15 && key==ActionLedger::storageKey(endpoint,port));
  assert(key!=ActionLedger::storageKey("192.168.10.3:22",port));
  assert(!ledger.alreadyClaimed(endpoint,port));
  assert(ledger.claim(endpoint,plan,"boot-123:incident-1") && store.writes==1);
  assert(ledger.alreadyClaimed(endpoint,port) && !ledger.claim(endpoint,plan,"retry") && store.writes==1);
  ActionLedger rebooted; store.attach(rebooted); assert(rebooted.alreadyClaimed(endpoint,port));
  assert(!rebooted.claim(endpoint,plan,"after-reboot"));
  assert(!observe(rebooted,2,3000) && !observe(rebooted,2,3000));
  assert(!rebooted.observeHealthy(endpoint,port,snapshot(3,33000,"DOWN","Not connected",false),policy(),33000));
  assert(!observe(rebooted,4,63000) && store.erases==0); // Down/one UP cannot clear.
  assert(!observe(rebooted,5,93000));
  assert(observe(rebooted,6,123000));
  assert(store.erases==1 && !rebooted.alreadyClaimed(endpoint,port));
  // A later independent incident can be claimed once the old one was proven clear.
  assert(rebooted.claim(endpoint,plan,"new-incident"));

  LedgerStore failed; ActionLedger noWrite; failed.attach(noWrite); failed.writeOk=false;
  assert(!noWrite.claim(endpoint,plan,"write-fails") && noWrite.reason=="ledger_write_failed");
  assert(noWrite.alreadyClaimed(endpoint,port));
  failed.writeOk=true; assert(!noWrite.claim(endpoint,plan,"no-blind-retry"));
  ActionLedger ioFailure; failed.attach(ioFailure); failed.readOk=false;
  assert(ioFailure.alreadyClaimed(endpoint,port) && !ioFailure.claim(endpoint,plan,"no-read"));
  LedgerStore corrupt; ActionLedger wrongReadback; corrupt.attach(wrongReadback); corrupt.corruptWrite=true;
  assert(!wrongReadback.claim(endpoint,plan,"bad-readback"));
  ActionLedger corruptReboot; corrupt.attach(corruptReboot); assert(corruptReboot.alreadyClaimed(endpoint,port));
  assert(!observe(corruptReboot,2,3000) && !observe(corruptReboot,3,33000) && !observe(corruptReboot,4,63000));
  assert(corrupt.erases==0);
  LedgerStore cannotErase; ActionLedger protectedClaim; cannotErase.attach(protectedClaim);
  assert(protectedClaim.claim(endpoint,plan,"cannot-erase")); cannotErase.eraseOk=false;
  assert(!observe(protectedClaim,2,3000) && !observe(protectedClaim,3,33000) && !observe(protectedClaim,4,63000));
  assert(protectedClaim.reason=="ledger_erase_failed" && protectedClaim.alreadyClaimed(endpoint,port));
  cannotErase.eraseOk=true; cannotErase.eraseNoop=true;
  assert(!observe(protectedClaim,5,93000) && protectedClaim.reason=="ledger_clear_readback_failed");
  assert(protectedClaim.alreadyClaimed(endpoint,port));
}
static void ledgerObservationFences() {
  const auto plan=isolationPlan(snapshot(),policy(),port,1000); LedgerStore store; ActionLedger ledger; store.attach(ledger);
  assert(ledger.claim(endpoint,plan,"fenced"));
  assert(!observe(ledger,2,3000) && !observe(ledger,3,33000)); ledger.disconnected();
  assert(!observe(ledger,4,63000)); // Reconnect discards pre-disconnect evidence.
  auto changed=healthy(5,93000); changed.targetRevision=8;
  changed.interfaces.stamp.targetRevision=changed.down.stamp.targetRevision=changed.loops.stamp.targetRevision=8;
  changed.stp.stamp.targetRevision=changed.aggregation.stamp.targetRevision=8;
  assert(!ledger.observeHealthy(endpoint,port,changed,policy(),93000));
  assert(!observe(ledger,6,123000)); // Another revision resets again.
  auto missing=healthy(7,153000); missing.loops.valid=false;
  assert(!ledger.observeHealthy(endpoint,port,missing,policy(),153000));
  assert(!observe(ledger,8,183000));
  auto disabled=healthy(9,213000); disabled.loops.enabled=false;
  assert(!ledger.observeHealthy(endpoint,port,disabled,policy(),213000));
  assert(!observe(ledger,10,243000));
  assert(!observe(ledger,11,303000)); // Missing collection interval cannot count as continuous health.
  assert(!observe(ledger,12,333000));
  assert(observe(ledger,13,363000));

  // A key collision/foreign or malformed value can never clear another claim.
  const auto key=ActionLedger::storageKey(endpoint,port);
  store.values[key]="ESP-H3C-CLAIM-1\n192.168.10.99:22\n"+port+"\nforeign\n1\n7\n1000\n";
  const auto erases=store.erases;
  assert(ledger.alreadyClaimed(endpoint,port));
  assert(!observe(ledger,14,393000) && !observe(ledger,15,423000) && !observe(ledger,16,453000));
  assert(store.erases==erases);
}
struct RuntimeHarness {
  h3c::DeviceAdapter adapter;
  LoopDiscovery discovery;H3cBdiAgent isolation;
  switchbdi::StrategySet strategies{&discovery,&isolation};
  Runtime runtime{adapter,strategies,site::switchPolicy()}; LedgerStore store; ActionLedger ledger;
  std::map<std::string,switchcli::Result> replies;
  std::map<std::string,std::string> outputs,states;
  std::set<std::string> truncated;
  std::vector<std::string> reads,writes,logs,outcomes;
  std::string target="192.168.10.2", mode="loop", loopPort="GigabitEthernet1/0/3";
  uint64_t now=1000; unsigned analyses=0,connectionsLost=0;
  bool holdReads=false,autoAck=false,secondLoop=false;
  std::string lastWrite;
  RuntimeHarness() {
    store.attach(ledger);
    runtime.log=[&](const std::string& line){logs.push_back(line);};
    runtime.connectionLost=[&](){++connectionsLost;ledger.disconnected();};
    runtime.claimIntent=[&](const ActionPlan& plan,const std::string& incident){return ledger.claim(target+":22",plan,incident);};
    runtime.alreadyClaimed=[&](const std::string& p){return ledger.alreadyClaimed(target+":22",p);};
    runtime.observeLedger=[&](const Snapshot& s,const Policy& p,uint64_t at){for(const auto& entry:s.interfaces.ports)ledger.observeHealthy(target+":22",entry.first,s,p,at);};
    runtime.recordOutcome=[&](const std::string& p,const std::string& outcome){outcomes.push_back(p+":"+outcome);};
    runtime.analyse=[&](const std::string&,uint64_t,uint64_t){++analyses;};
    runtime.submitRead=[&](const std::string& id,const std::string& command){
      reads.push_back(id); switchcli::Result result; result.id=id;result.command=command;result.target=target;
      result.output=outputs.count(command)?outputs.at(command):fixture(command);
      result.state=holdReads?"running":states.count(command)?states.at(command):"ok";
      result.truncated=truncated.count(command); replies[id]=result;return "queued";
    };
    runtime.submitIsolation=[&](const std::string& id,const std::string& p){
      writes.push_back(p);lastWrite=id;switchcli::Result result;result.id=id;result.command="isolate:"+p;result.target=target;
      result.state=autoAck?"ok":"running";replies[id]=result;if(autoAck)mode="isolated";return "queued";
    };
    runtime.resultReady=[&](const std::string& id){return replies.count(id)&&replies.at(id).done();};
    runtime.result=[&](const std::string& id){return replies.at(id);};
    assert(runtime.bindManagementProfile(target,22,"GE1/0/1"));
    runtime.configure(target,0,"test-boot",true);
  }
  std::string fixture(const std::string& command) const {
    if(command=="version")return "H3C Comware Software, Version 7.1.070, Release 6328P03\nH3C S5130S-52S-EI uptime is 1 day\n<H3C>\n";
    const std::string link=mode=="isolated"?"ADM":mode=="physical"?"DOWN":"UP";
    if(command=="interfaces")return briefHeader+"GE1/0/3 "+link+" 1G(a) F(a) A 10\n"+(secondLoop?"GE1/0/4 UP 1G(a) F(a) A 10\n":"")+"<H3C>\n";
    if(command=="down")return downHeader+(mode=="isolated"?"GE1/0/3 ADM Administratively\n":mode=="physical"?"GE1/0/3 DOWN Not connected\n":"")+"<H3C>\n";
    if(command=="loops") {
      if(mode=="disabled")return "Loop detection is disabled.\n<H3C>\n";
      if(mode=="healthy"||mode=="physical")return loopHeader+"No loop is detected.\n<H3C>\n";
      return loopHeader+"Loop is detected on following interfaces:\nInterface Action mode VLANs\n"+loopPort+" None 10\n"+(secondLoop?"GigabitEthernet1/0/4 None 10\n":"")+"<H3C>\n";
    }
    if(command=="stp")return stpHeader+"0 GigabitEthernet1/0/1 DESI FORWARDING NONE\n"+(link=="UP"?"0 GigabitEthernet1/0/3 DESI FORWARDING NONE\n":"")+(secondLoop?"0 GigabitEthernet1/0/4 DESI FORWARDING NONE\n":"")+"<H3C>\n";
    if(command=="aggregation")return "display link-aggregation verbose\n<H3C>\n";
    if(command=="mac_moves")return "display mac-address mac-move\nMAC address VLAN Current port Source port Last time Times\n--- 0 MAC address moving records found ---\n<H3C>\n";
    if(command=="logs")return "display logbuffer reverse\nLog buffer information\n%Nov 2 10:00:00 H3C IFNET/3/PHY_UPDOWN: Physical state on GigabitEthernet1/0/3 changed to down.\n<H3C>\n";
    if(command=="cpu")return "Slot 1 CPU 0 CPU usage:\n3% in last 5 seconds\n<H3C>\n";
    return "Memory statistics are measured in KB:\nMem: 506412 316704 189708 0 0 112892 37.6%\n<H3C>\n";
  }
  void step(bool connected=true,bool trusted=true,uint64_t delta=100) { now+=delta;runtime.tick(now,connected,trusted); }
  bool is(const std::string& state)const{return runtime.json().find("\"state\":\""+state+"\"")!=std::string::npos;}
  template<class Predicate> void until(Predicate predicate,unsigned limit=2500) {while(limit--&&!predicate())step();assert(predicate());}
  void rounds(unsigned count=3){const auto stop=analyses+count;until([&](){return analyses>=stop;});for(unsigned i=0;i<3;++i)step();}
  void ack(const std::string& state="ok") {assert(!lastWrite.empty());replies[lastWrite].state=state;if(state=="ok")mode="isolated";step();}
};
static void runtimeRealCommandLifecycle() {
  RuntimeHarness h;
  h.until([&](){return h.writes.size()==1;});
  assert(h.writes[0]==port && h.store.writes==1 && h.ledger.alreadyClaimed(endpoint,port));
  h.ack(); assert(h.is("verifying_isolation"));
  h.until([&](){return h.is("contained");});
  assert(h.writes.size()==1);
  assert(std::count(h.outcomes.begin(),h.outcomes.end(),port+":contained")==1);
  assert(std::any_of(h.logs.begin(),h.logs.end(),[](const std::string& line){return line.find("state=contained")!=std::string::npos;}));
  h.rounds(2);assert(h.writes.size()==1); // A continuing loop is not toggled.
  h.mode="healthy";
  h.until([&](){return !h.ledger.alreadyClaimed(endpoint,port);});
  h.mode="loop";
  h.until([&](){return h.writes.size()==2;}); // Cleared old tracker cannot reject a new incident.
  h.ack("timeout_or_disconnected");assert(h.is("needs_review"));
  h.rounds(3);assert(h.writes.size()==2); // Ambiguous write is persistently locked.
}
static void runtimeGatesAndMetadata() {
  {RuntimeHarness h;h.outputs["version"]="H3C S5120V2-LI\n";h.rounds();assert(h.writes.empty()&&h.is("unsupported_device"));}
  {RuntimeHarness h;h.truncated.insert("interfaces");h.rounds();assert(h.writes.empty()&&h.is("unknown"));}
  {RuntimeHarness h;h.states["loops"]="command_error";h.rounds();assert(h.writes.empty()&&h.is("unknown"));}
  {RuntimeHarness h;h.outputs["loops"]="Unrecognized future output\n";h.rounds();assert(h.writes.empty()&&h.is("unknown"));
    const auto status=h.runtime.json();
    assert(status.find("\"loops\":{\"valid\":false,\"complete\":true")!=std::string::npos);
    assert(status.find("\"interfaces\":{\"valid\":true")!=std::string::npos);
    assert(std::any_of(h.logs.begin(),h.logs.end(),[](const std::string& line){return line.find("command=loops state=parse_rejected")!=std::string::npos;}));}
  {RuntimeHarness h;h.loopPort="GigabitEthernet1/0/1";h.rounds();assert(h.writes.empty());assert(h.runtime.json().find("management_port_protected")!=std::string::npos);}
  {RuntimeHarness h;h.mode="physical";h.rounds();assert(h.writes.empty()&&h.is("monitoring"));}
  {RuntimeHarness h;h.mode="disabled";h.rounds();assert(h.writes.empty()&&h.is("monitoring"));assert(h.runtime.json().find("loop_detection_disabled")!=std::string::npos);}
  {RuntimeHarness h;h.store.writeOk=false;h.rounds();assert(h.writes.empty()&&h.store.writes==1);}
  {RuntimeHarness h;h.step(true,false);assert(h.reads.empty()&&h.is("waiting_ssh"));}
  {RuntimeHarness h;h.holdReads=true;h.step();const auto id=h.reads.back();h.replies[id].state="ok";h.replies[id].target="192.168.10.99";h.holdReads=false;h.rounds(1);assert(h.writes.empty()&&h.is("unsupported_device"));}
  {RuntimeHarness h;h.until([&](){return h.writes.size()==1;});h.replies[h.lastWrite].state="ok";h.replies[h.lastWrite].id="old-request";h.step();assert(h.is("needs_review"));assert(h.outcomes.back()==port+":result_identity_mismatch");}
}
static void runtimeReconnectAndOtherPorts() {
  {RuntimeHarness h;h.holdReads=true;h.step();const auto old=h.reads.back();h.step(false);assert(h.connectionsLost>=2);
   h.replies[old].state="ok";h.outputs["version"]="unknown device\n";h.holdReads=false;h.rounds(2);
   assert(h.reads.back()!=old&&h.writes.empty()&&h.is("unsupported_device"));}
  {RuntimeHarness h;h.until([&](){return h.is("awaiting_execution");});assert(h.writes.empty());
   h.target="192.168.10.3";h.runtime.configure(h.target,0,"same-revision-new-endpoint",true);
   h.rounds(2);assert(h.writes.empty()&&h.runtime.json().find("\"enabled\":false")!=std::string::npos);
   assert(h.runtime.bindManagementProfile(h.target,22,"GE1/0/1"));
   h.until([&](){return h.writes.size()==1;});assert(h.replies.at(h.lastWrite).target==h.target);
   assert(h.ledger.alreadyClaimed(endpoint,port)&&h.ledger.alreadyClaimed(h.target+":22",port));}
  {RuntimeHarness h;const auto claimed=isolationPlan(snapshot(),policy(),port,1000);
   assert(h.ledger.claim(endpoint,claimed,"previous-boot"));h.secondLoop=true;
   h.until([&](){return !h.writes.empty();});assert(h.writes.size()==1&&h.writes[0]=="GigabitEthernet1/0/4");}
  {RuntimeHarness h;h.runtime.result=nullptr;h.step();h.step(true,true,111000);assert(h.writes.empty());}
}
static void managementProfileBinding() {
  h3c::DeviceAdapter adapter;switchbdi::StrategySet strategies;
  Runtime bare{adapter,strategies,site::switchPolicy()};
  bare.configure("192.168.10.2",0,"unbound",true);
  assert(bare.json().find("\"enabled\":false")!=std::string::npos);
  assert(!bare.bindManagementProfile("192.168.10.2",0,"GE1/0/1"));
  assert(!bare.bindManagementProfile("192.168.10.2",22,"GE1/0/1\nshutdown"));
  {RuntimeHarness h;
   h.target="192.168.10.99";h.runtime.configure(h.target,1,"new-target",true);h.rounds();
   assert(h.writes.empty()&&h.runtime.json().find("\"topology_confirmed\":false")!=std::string::npos);
   h.target="192.168.10.2";h.runtime.configure(h.target,2,"back-to-old-target",true);h.rounds();
   assert(h.writes.empty()&&h.runtime.json().find("\"topology_confirmed\":false")!=std::string::npos);}
  {RuntimeHarness h;h.runtime.configure(h.target,1,"different-ssh-port",true,2222);h.rounds();
   assert(h.writes.empty()&&h.runtime.json().find("\"enabled\":false")!=std::string::npos);}
  {RuntimeHarness h;h.runtime.configure(h.target,9,"same-endpoint-new-settings",true,22);
   h.until([&](){return h.writes.size()==1;});assert(h.runtime.json().find("\"topology_confirmed\":true")!=std::string::npos);}
}
static void runtimeMultiConditionDiscovery(){
  RuntimeHarness h;h.mode="disabled";h.secondLoop=true;
  h.outputs["cpu"]="Slot 1 CPU 0 CPU usage:\n90% in last 5 seconds\n<H3C>\n";
  auto moving=[&](unsigned n){
    h.outputs["mac_moves"]="MAC address VLAN Current port Source port Last time Times\n"
      "0000-0001-002c 10 GE1/0/3 GE1/0/4 2013-05-20 13:40:52 "+std::to_string(n)+"\n"
      "0000-0001-002c 10 GE1/0/4 GE1/0/3 2013-05-20 13:40:52 "+std::to_string(n)+"\n--- 2 MAC address moving records found ---\n<H3C>\n";
  };
  moving(100);h.rounds(1);
  assert(h.runtime.json().find("baseline_established_history_not_replayed")!=std::string::npos);
  moving(103);h.rounds(1);
  assert(h.runtime.json().find("\"discovery\":{\"state\":\"watch\"")!=std::string::npos);
  moving(106);h.rounds(1);
  assert(h.runtime.json().find("\"discovery\":{\"state\":\"suspected\"")!=std::string::npos);
  assert(h.writes.empty()&&h.store.writes==0); // Statistical suspicion never reaches the isolation agent.
  h.step(false);assert(h.runtime.json().find("waiting_for_observation")!=std::string::npos);
  moving(200);h.rounds(1);
  assert(h.runtime.json().find("baseline_established_history_not_replayed")!=std::string::npos);
  h.outputs["mac_moves"]="Unsupported device response\n<H3C>\n";h.rounds(1);
  const auto status=h.runtime.json();
  assert(status.find("mac_move_evidence_unavailable")!=std::string::npos);
  assert(status.find("\"observation_valid\":true")!=std::string::npos&&h.writes.empty());
}
static void replayDeviceCapture(const std::string& directory){
  RuntimeHarness h;
  for(const char* command:{"version","interfaces","down","loops","stp","aggregation"}){
    std::ifstream file(directory+"/"+command+".txt");assert(file);
    h.outputs[command]=std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
  }
  bool hasDiscoveryCapture=false;
  for(const char* command:{"cpu","mac_moves"}){
    std::ifstream file(directory+"/"+command+".txt");
    if(file){h.outputs[command]=std::string((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());if(std::string(command)=="mac_moves")hasDiscoveryCapture=true;}
  }
  h.rounds(3);
  const auto status=h.runtime.json();
  assert(h.writes.empty()&&h.is("monitoring"));
  assert(status.find("\"observation_valid\":true")!=std::string::npos);
  assert(status.find("loop_detection_disabled")!=std::string::npos);
  assert(status.find("\"loop_detection_enabled\":false")!=std::string::npos);
  if(hasDiscoveryCapture){
    assert(status.find("\"mac_moves_valid\":true")!=std::string::npos);
    assert(status.find("\"cpu_valid\":true")!=std::string::npos);
    assert(status.find("lpdt_disabled_no_current_multi_signal_evidence")!=std::string::npos);
    assert(status.find("\"action\":\"observe_only\"")!=std::string::npos);
    assert(parseMacMoves(h.outputs["mac_moves"],stamp()).records.empty());
    std::cout<<"Real r4 MAC/CPU capture replay: valid empty history, read-only monitoring\n";
  }
  const auto interfaces=parseInterfaces(h.outputs["interfaces"],stamp());
  assert(interfaces.valid&&interfaces.ports.size()==52);
  std::cout<<"Real device capture replay: five tables valid, monitoring with detector unconfigured, no writes\n";
}
int main(int argc,char** argv) {
  parsing(); diagnosisAndGates(); verifiedLifecycle(); failuresDoNotReplayOrRecover(); actualEmbeddedBdi(); durableLedger(); ledgerObservationFences();
  runtimeRealCommandLifecycle();runtimeGatesAndMetadata();runtimeReconnectAndOtherPorts();managementProfileBinding();runtimeMultiConditionDiscovery();
  if(argc==2) {
    std::ifstream file(argv[1]); const std::string text((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
    const auto parsed=parseInterfaces(text,stamp()); assert(parsed.valid && parsed.ports.size()==52);
    assert(parsed.ports.at("GigabitEthernet1/0/1").link==Link::Up && parsed.ports.at("GigabitEthernet1/0/1").pvid==10);
    assert(parsed.ports.at("Ten-GigabitEthernet1/0/52").link==Link::Down);
  }
  if(argc==3&&std::string(argv[1])=="--replay")replayDeviceCapture(argv[2]);
  std::cout<<"H3C parsers, action gates, and verified containment/recovery passed\n";
}
