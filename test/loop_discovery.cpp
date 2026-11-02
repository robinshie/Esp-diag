#include "../src/h3c/loop_discovery.h"
#include <cassert>
#include <iostream>
using namespace h3c;
static const std::string A="GigabitEthernet1/0/2",B="GigabitEthernet1/0/3";
static Stamp at(unsigned cycle){Stamp stamp;stamp.cycle=cycle;stamp.atMs=cycle*10000;stamp.complete=true;stamp.targetRevision=1;return stamp;}
static Snapshot sample(unsigned cycle){
  auto st=at(cycle);Snapshot s;s.targetRevision=1;s.sshConnected=s.identityVerified=true;
  s.interfaces=parseInterfaces("Brief information on interfaces in bridge mode:\nInterface Link Speed Duplex Type PVID Description\nGE1/0/2 UP 1G F A 10\nGE1/0/3 UP 1G F A 10\n",st);
  s.down=parseDownCauses("Brief information on interfaces in bridge mode:\nInterface Link Cause\n",st);
  s.loops=parseLoops("LPDT is not configured.\n",st);
  s.stp=parseStp("MST ID Port Role STP State Protection\n0 GE1/0/2 DESI FORWARDING NONE\n0 GE1/0/3 DESI FORWARDING NONE\n",st);
  s.aggregation=parseAggregation("No link aggregation group exists.\n",st);return s;
}
static Policy policy(){Policy p;p.managementPort="GE1/0/1";p.allowedPorts={A,B};return p;}
static MacMoves moves(unsigned cycle,unsigned forward,unsigned reverse){
  const auto text="display mac-address mac-move\nMAC address VLAN Current port Source port Last time Times\n"
    "0000-0001-002c 10 GE1/0/3 GE1/0/2 2013-05-20 13:40:52 "+std::to_string(forward)+"\n"
    "0000-0001-002c 10 GE1/0/2 GE1/0/3 2013-05-20 13:40:52 "+std::to_string(reverse)+"\n--- 2 MAC address moving records found ---\n<H3C>\n";
  return parseMacMoves(text,at(cycle));
}
static CpuUsage cpu(unsigned cycle,unsigned value=90){return parseCpuUsage("Slot 1 CPU 0 CPU usage:\n"+std::to_string(value)+"% in last 5 seconds\n<H3C>\n",at(cycle));}
static bool state(const LoopDiscovery& d,const char* value,unsigned cycle){return d.json(cycle*10000).find("\"state\":\""+std::string(value)+"\"")!=std::string::npos;}
static void parsers(){
  assert(moves(1,20,20).valid&&moves(1,20,20).records.size()==2);
  assert(parseMacMoves("MAC address VLAN Current port Source port Last time Times\n--- 0 MAC address moving records found ---\n",at(1)).valid);
  assert(parseMacMoves("No MAC address moving records found.\n",at(1)).valid);
  assert(!parseMacMoves("display mac-address mac-move\n<H3C>\n",at(1)).valid);
  assert(!parseMacMoves("MAC address VLAN Current port Source port Last time Times\n--- 3 MAC address moving records found ---\n",at(1)).valid);
  assert(!parseMacMoves("No MAC address moving records found.\nUnexpected future format\n",at(1)).valid);
  auto incomplete=at(1);incomplete.complete=false;
  assert(!parseMacMoves("No MAC address moving records found.\n",incomplete).valid);
  assert(cpu(1,80).valid&&cpu(1,80).percent==80);
  assert(!cpu(1,101).valid);
  assert(!parseCpuUsage("90% in last 5 seconds\n",at(1)).valid);
  assert(!parseCpuUsage("Slot 1 CPU 0 CPU usage:\n90% in last 1 minute\n",at(1)).valid);
}
static void genuineMultiCondition(){
  LoopDiscovery d;std::vector<std::string> logs;d.log=[&](const std::string& line){logs.push_back(line);};
  auto p=policy();
  d.observe(sample(1),moves(1,1000,1000),cpu(1),p,10000);assert(state(d,"monitoring",1));
  d.observe(sample(2),moves(2,1003,1003),cpu(2),p,20000);assert(state(d,"watch",2));
  d.observe(sample(3),moves(3,1006,1006),cpu(3),p,30000);assert(state(d,"suspected",3));
  const auto json=d.json(30000);assert(json.find("\"rounds\":2")!=std::string::npos&&json.find("observe_only")!=std::string::npos);
  assert(json.find("cpu_high")!=std::string::npos&&json.find(A)!=std::string::npos&&json.find(B)!=std::string::npos);
  assert(state(d,"unknown",8)); // Live JSON expires, without waiting for another cycle.
  assert(std::any_of(logs.begin(),logs.end(),[](const std::string& line){return line.find("state=suspected")!=std::string::npos;}));
  d.observe(sample(3),moves(3,2000,2000),cpu(3),p,30001);assert(d.json(30001).find("\"forward_delta\":3")!=std::string::npos);
  d.observe(sample(4),moves(4,1006,1006),cpu(4),p,40000);assert(state(d,"monitoring",4)); // Replayed history cannot sustain suspicion.
}
static void negativesAndReset(){
  auto p=policy();
  {LoopDiscovery d;for(unsigned c=1;c<5;++c)d.observe(sample(c),moves(c,10000,10000),cpu(c),p,c*10000);assert(state(d,"monitoring",4));}
  {LoopDiscovery d;for(unsigned c=1;c<5;++c)d.observe(sample(c),moves(c,c*10,20),cpu(c),p,c*10000);assert(state(d,"monitoring",4));}
  {LoopDiscovery d;for(unsigned c=1;c<5;++c)d.observe(sample(c),moves(c,c*10,c*10),cpu(c,5),p,c*10000);assert(state(d,"watch",4));}
  {LoopDiscovery d;auto s=sample(1);d.observe(s,moves(1,10,10),cpu(1),p,10000);
   s=sample(2);s.stp.ports[A][0].state="DISCARDING";d.observe(s,moves(2,20,20),cpu(2),p,20000);assert(state(d,"monitoring",2));}
  {LoopDiscovery d;d.observe(sample(1),moves(1,10,10),cpu(1),p,10000);d.observe(sample(2),moves(2,20,20),cpu(2),p,20000);
   auto broken=sample(3);broken.down.valid=false;d.observe(broken,moves(3,30,30),cpu(3),p,30000);assert(state(d,"unknown",3));
   d.observe(sample(4),moves(4,40,40),cpu(4),p,40000);assert(state(d,"monitoring",4));
   d.observe(sample(5),moves(5,50,50),cpu(5),p,50000);assert(state(d,"watch",5));}
  {LoopDiscovery d;d.observe(sample(1),moves(1,100,100),cpu(1),p,10000);d.observe(sample(2),moves(2,110,110),cpu(2),p,20000);
   d.observe(sample(3),moves(3,1,1),cpu(3),p,30000);assert(state(d,"monitoring",3));assert(d.json(30000).find("mac_counter_reset_rebaseline")!=std::string::npos);}
  {LoopDiscovery d;d.observe(sample(1),moves(1,100,100),cpu(1),p,10000);d.observe(sample(3),moves(3,200,200),cpu(3),p,30000);assert(state(d,"monitoring",3));}
  {LoopDiscovery d;d.observe(sample(1),moves(1,10,10),cpu(1),p,10000);d.observe(sample(2),moves(2,20,20),cpu(2),p,20000);
   auto m=moves(3,30,30);m.stamp.cycle=2;d.observe(sample(3),m,cpu(3),p,30000);assert(state(d,"unknown",3));}
  {LoopDiscovery d;d.observe(sample(1),moves(1,10,10),cpu(1),p,10000);d.reset();d.observe(sample(2),moves(2,20,20),cpu(2),p,20000);assert(state(d,"monitoring",2));}
}
static void exclusionsAndCorroboration(){
  for(unsigned kind=0;kind<6;++kind){
    LoopDiscovery d;auto p=policy();
    for(unsigned c=1;c<=3;++c){auto s=sample(c);
      if(kind==0)p.managementPort=A;
      if(kind==1)s.interfaces.ports[A].type='T';
      if(kind==2)s.aggregation.members.insert(A);
      if(kind==3)s.stp.ports[A][0].role="ROOT";
      if(kind==4)s.interfaces.ports[A].pvid=20;
      if(kind==5)s.interfaces.ports[A].link=Link::Down;
      d.observe(s,moves(c,c*10,c*10),cpu(c),p,c*10000);
    }assert(state(d,"monitoring",3));
  }
  {LoopDiscovery d;auto p=policy();d.observe(sample(1),moves(1,10,10),cpu(1,5),p,10000);
   auto s=sample(2);s.stp.ports[A][0].instance="1";d.observe(s,moves(2,20,20),cpu(2,5),p,20000);assert(state(d,"watch",2));
   s=sample(3);s.stp.ports[A][0].instance="1";d.observe(s,moves(3,30,30),cpu(3,5),p,30000);assert(state(d,"suspected",3));
   assert(d.json(30000).find("recent_stp_change")!=std::string::npos);}
}
static void directAndConflict(){
  auto p=policy();
  {LoopDiscovery d;auto s=sample(1);s.loops.enabled=true;s.loops.intervalSeconds=30;s.loops.ports[A]=LoopPort();
   d.observe(s,MacMoves(),CpuUsage(),p,10000);assert(state(d,"confirmed",1));}
  {LoopDiscovery d;auto s=sample(1);s.loops.enabled=true;s.loops.intervalSeconds=30;s.loops.ports[A]=LoopPort();
   s.interfaces.ports[A].link=Link::Down;DownPort down;down.link=Link::Down;down.cause=DownCause::Physical;s.down.ports[A]=down;
   d.observe(s,moves(1,1,1),cpu(1),p,10000);assert(state(d,"unknown",1));}
}
int main(){parsers();genuineMultiCondition();negativesAndReset();exclusionsAndCorroboration();directAndConflict();std::cout<<"Read-only loop discovery BDI, MAC deltas, temporal corroboration and false-positive gates passed\n";}
