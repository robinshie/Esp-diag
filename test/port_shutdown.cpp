#define PORT_WIRE_ONLY
#include "port_exercise.cpp"
static void single(Wire& w){w.exercise.shutdownPortTwoOnly();}
int main(){
  {Wire w;single(w);w.admin[0]=false;w.run();assert(w.exercise.state()=="passed"&&w.writes.size()==1&&w.writes[0]=="isolate:GigabitEthernet1/0/2"&&w.admin[0]&&w.admin[1]);}
  {Wire w;single(w);w.run();assert(w.exercise.state()=="passed"&&w.writes.empty()&&w.claimed);assert(w.exercise.json().find("already_administratively_down")!=std::string::npos);}
  {Wire w;single(w);w.enforceOtherAdmin=false;w.admin[0]=w.admin[1]=false;w.run();assert(w.writes.size()==1&&w.admin[0]&&!w.admin[1]);}
  {Wire w;single(w);w.admin[0]=false;w.claimOk=false;w.run();assert(w.writes.empty()&&w.exercise.state()=="needs_review");}
  {Wire w;single(w);w.admin[0]=false;w.claimed=true;w.run();assert(w.writes.empty());}
  {Wire w;single(w);w.admin[0]=false;w.failAt=1;w.run();w.run();assert(w.writes.size()==1&&w.exercise.state()=="needs_review");}
  {Wire w;single(w);w.admin[0]=false;w.mismatch=true;w.run();assert(w.writes.size()==1&&w.exercise.state()=="needs_review");}
  {Wire w;single(w);w.admin[0]=false;while(w.writes.empty()){w.now+=100;w.exercise.tick(w.now);}w.connected_=false;w.run();assert(w.writes.size()==1&&w.exercise.state()=="needs_review");}
  {Wire w;single(w);w.exercise.configure("192.168.10.3",22,"ssh","shell");w.run();assert(w.writes.empty());}
  {Wire w;single(w);w.exercise.log=[&](const std::string& line){if(line.find("shutdown_readback")!=std::string::npos)w.admin[0]=false;};w.run();assert(w.writes.empty()&&w.exercise.state()=="needs_review");}
  std::cout<<"10 single-port shutdown safety scenarios passed; no enable and no port-3 writes\n";
}
