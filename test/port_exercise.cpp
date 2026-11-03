#include "../src/application/port_exercise.h"
#include "../src/adapters/h3c/device_adapter.h"
#ifdef PORT_EXERCISE_BDI_TEST
#include "../src/bdi/port_acceptance.h"
#endif
#include <cassert>
#include <iostream>
using switchmodel::Action;
struct Wire: switchtransport::ICommandTransport {
  h3c::DeviceAdapter adapter;
  bool admin[2]={true,true},connected_=true,claimed=false,claimOk=true,mismatch=false,unsupported=false,aggregated=false;
  unsigned failAt=0;
  bool enforceOtherAdmin=true;
  std::vector<std::string> writes;
  switchcli::Result reply;
  switchapp::PortExercise exercise{adapter,*this};
  uint64_t now=1000;
#ifdef PORT_EXERCISE_BDI_TEST
  switchbdi::PortAcceptance bdi;
#endif
  Wire(){
    exercise.claimOnce=[this](){if(!claimOk||claimed)return false;claimed=true;return true;};exercise.configure("192.168.10.2",22,"ssh","shell");
#ifdef PORT_EXERCISE_BDI_TEST
    exercise.requireDeliberation=true;
    exercise.deliberateAction=[this](const switchmodel::Snapshot& s,uint64_t t,const std::string& p,unsigned phase,bool claim){return bdi.deliberate(s,t,p,phase,claim);};
#endif
  }
  const char* kind()const override{return "ssh";}
  const char* stateName()const override{return connected_?"connected":"disconnected";}
  bool connected()override{return connected_;}bool sessionAccepted()override{return connected_;}
  std::string fixture(const std::string& key){
    if(key=="version")return unsupported?"unknown device":"H3C Comware Software, Version 7.1.070, Release 6328P03\nH3C S5130S-52S-EI uptime is 1 day\n<H3C>";
    if(key=="interfaces")return std::string("Brief information on interfaces in bridge mode:\nInterface Link Speed Duplex Type PVID Description\nGE1/0/1 UP 100M F A 10\nGE1/0/2 ")+(admin[0]?"ADM":"DOWN")+" auto A A 10\nGE1/0/3 "+(admin[1]?"ADM":"DOWN")+" auto A A 10\n<H3C>";
    if(key=="down")return std::string("Brief information on interfaces in bridge mode:\nInterface Link Cause\nGE1/0/2 ")+(admin[0]?"ADM Administratively":"DOWN Not connected")+"\nGE1/0/3 "+(admin[1]?"ADM Administratively":"DOWN Not connected")+"\n<H3C>";
    if(key=="loops")return "Loop detection is enabled.\nLoop detection interval is 30 second(s).\nNo loop is detected.\n<H3C>";
    if(key=="stp")return "MST ID Port Role STP State Protection\n0 GigabitEthernet1/0/1 DESI FORWARDING NONE\n<H3C>";
    if(key=="aggregation")return aggregated?"Port Status: S -- Selected\nAggregate Interface: Bridge-Aggregation1\nGE1/0/2 S\n<H3C>":"display link-aggregation verbose\n<H3C>";
    return "";
  }
  std::string submit(const std::string& id,const std::string& key)override{reply=switchcli::Result();reply.id=id;reply.command=key;reply.target="192.168.10.2";reply.state="ok";reply.output=fixture(key);return "queued";}
  std::string submitIsolation(const std::string& id,const std::string& port)override{return submitAction(id,Action::Isolate,port);}
  std::string submitAction(const std::string& id,Action a,const std::string& port)override{
    assert(port=="GigabitEthernet1/0/2"||port=="GigabitEthernet1/0/3");const unsigned p=port.back()-'2';if(enforceOtherAdmin)assert(admin[1-p]);
    writes.push_back(switchmodel::actionCommandKey(a,port));reply=switchcli::Result();reply.id=mismatch?"wrong":id;reply.command=writes.back();reply.target="192.168.10.2";reply.state=writes.size()==failAt?"timeout_or_disconnected":"ok";
    if(reply.state=="ok")admin[p]=a==Action::Isolate;
    return "queued";
  }
  bool resultReady(const std::string&)override{return reply.done();}
  switchcli::Result result(const std::string&)override{return reply;}
  switchcli::Result takeResult(const std::string&)override{auto r=reply;reply=switchcli::Result();return r;}
  void run(){for(unsigned i=0;i<3000&&exercise.exclusive();++i){now+=100;exercise.tick(now);}}
};
#ifndef PORT_WIRE_ONLY
int main(){
  {Wire w;w.run();assert(w.exercise.state()=="passed"&&w.writes.size()==8&&w.admin[0]&&w.admin[1]);for(unsigned i=0;i<8;++i)assert(w.writes[i]==switchmodel::actionCommandKey(i%2?Action::Isolate:Action::RestoreOwned,"GigabitEthernet1/0/"+std::to_string(i<4?2:3)));
#ifdef PORT_EXERCISE_BDI_TEST
    assert(w.bdi.decisions()==8);
#endif
  }
  {Wire w;w.claimOk=false;w.run();assert(w.writes.empty()&&w.exercise.state()=="needs_review");}
  {Wire w;w.claimed=true;w.run();assert(w.writes.empty());}
  {Wire w;w.admin[1]=false;w.run();assert(w.writes.empty());}
  {Wire w;w.unsupported=true;w.run();assert(w.writes.empty());}
  {Wire w;w.aggregated=true;w.run();assert(w.writes.empty());}
  {Wire w;w.exercise.configure("192.168.10.3",22,"ssh","shell");w.run();assert(w.writes.empty());}
  {Wire w;w.failAt=1;w.run();assert(w.writes.size()==1&&w.exercise.state()=="needs_review");w.run();assert(w.writes.size()==1);}
  {Wire w;w.mismatch=true;w.run();assert(w.writes.size()==1&&w.exercise.state()=="needs_review");}
  {Wire w;while(w.writes.empty()){w.now+=100;w.exercise.tick(w.now);}w.connected_=false;w.run();assert(w.writes.size()==1&&w.exercise.state()=="needs_review");}
#ifdef PORT_EXERCISE_BDI_TEST
  {Wire w;w.exercise.deliberateAction={};w.run();assert(w.writes.empty()&&w.exercise.state()=="needs_review");}
  {Wire w;w.exercise.deliberateAction=[](const switchmodel::Snapshot&,uint64_t,const std::string&,unsigned,bool){return switchmodel::ActionPlan();};w.run();assert(w.writes.empty());}
  {Wire w;auto actual=w.exercise.deliberateAction;w.exercise.deliberateAction=[&](const switchmodel::Snapshot& s,uint64_t t,const std::string& p,unsigned phase,bool claim){auto plan=actual(s,t,p,phase,claim);plan.port="GigabitEthernet1/0/1";return plan;};w.run();assert(w.writes.empty());}
  {Wire w;auto actual=w.exercise.deliberateAction;w.exercise.deliberateAction=[&](const switchmodel::Snapshot& s,uint64_t t,const std::string& p,unsigned phase,bool claim){auto plan=actual(s,t,p,phase,claim);++plan.cycle;return plan;};w.run();assert(w.writes.empty());}
  {Wire w;auto actual=w.exercise.deliberateAction;w.exercise.deliberateAction=[&](const switchmodel::Snapshot& s,uint64_t t,const std::string& p,unsigned phase,bool claim){auto plan=actual(s,t,p,phase,claim);plan.action=Action::Isolate;return plan;};w.run();assert(w.writes.empty());}
  {Wire w;auto actual=w.exercise.deliberateAction;w.exercise.deliberateAction=[&](const switchmodel::Snapshot& s,uint64_t t,const std::string& p,unsigned phase,bool){return actual(s,t,p,phase,false);};w.run();assert(w.writes.empty());}
  {Wire w;auto actual=w.exercise.deliberateAction;w.exercise.deliberateAction=[&](const switchmodel::Snapshot& s,uint64_t t,const std::string& p,unsigned phase,bool claim){return actual(s,t+15001,p,phase,claim);};w.run();assert(w.writes.empty());}
  std::cout<<"17 BDI operator acceptance safety scenarios passed; 8 semantic intentions verified\n";
#else
  std::cout<<"10 one-shot physical port exercise safety scenarios passed\n";
#endif
}
#endif
