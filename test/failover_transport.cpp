#include "../src/net/failover_transport.h"
#include <cassert>
#include <vector>
using namespace switchtransport;
struct Link : ICommandTransport {
  const char* name;bool online=false,trusted=true;unsigned submissions=0;std::string state="disabled";switchcli::Result reply;
  explicit Link(const char* n):name(n){}
  const char* kind()const override{return name;}
  const char* stateName()const override{return online?"connected":state.c_str();}
  bool connected()override{return online;}
  bool sessionAccepted()override{return online&&trusted;}
  std::string submit(const std::string& id,const std::string& command)override{
    ++submissions;reply.id=id;reply.command=command;reply.target="192.168.10.2";reply.state="running";return "queued";
  }
  std::string submitIsolation(const std::string& id,const std::string& port)override{return submit(id,"isolate:"+port);}
  bool resultReady(const std::string& id)override{return id==reply.id&&reply.done();}
  switchcli::Result result(const std::string&)override{return reply;}
  switchcli::Result takeResult(const std::string&)override{auto r=reply;reply=switchcli::Result();return r;}
};
struct Test {
  Link ssh{"ssh"},uart{"uart_console"};FailoverTransport route{ssh,uart};
  unsigned starts=0,stops=0,resumes=0,pauses=0;bool stopped=true;
  Test(){
    uart.online=true;
    route.setBackupEnabled=[&](bool on,uint64_t){if(on)++starts;else ++stops;};
    route.setPrimaryEnabled=[&](bool on){if(on)++resumes;else ++pauses;ssh.online=false;ssh.state=on?"connecting":"disabled";};
    route.primaryStopped=[&](){return stopped;};route.configure(true);
  }
  void failover(){route.tick(100);route.tick(5100);}
  void completeRead(uint64_t at){assert(route.submit("console-read","version")=="queued");uart.reply.state="ok";route.tick(at);}
};
int main(){
  {Test t;t.ssh.online=true;t.route.tick(0);t.route.tick(100000);assert(t.starts==0&&t.pauses==0);}
  for(unsigned wait:{1u,30u,300u}){Test t;t.route.configure(true,0,wait);t.route.tick(100);
   t.route.tick(100+uint64_t(wait)*1000-1);assert(t.starts==0);t.route.tick(100+uint64_t(wait)*1000);assert(t.route.usingBackup()&&t.pauses==1);}
  {Test t;t.uart.online=false;t.failover();assert(t.starts==1&&t.pauses==0&&!t.route.usingBackup());
   t.ssh.online=true;t.route.tick(6000);assert(t.stops==1&&!t.route.usingBackup());}
  {Test t;t.ssh.online=true;t.route.submitIsolation("bdi-old","GE1/0/2");t.ssh.online=false;t.failover();assert(t.starts==0&&t.pauses==0);
   t.ssh.reply.state="timeout_or_disconnected";t.route.tick(6000);assert(t.route.usingBackup()&&t.uart.submissions==0);
   assert(t.route.takeResult("bdi-old").state=="timeout_or_disconnected");}
  {Test t;t.failover();t.route.tick(1000000);assert(t.resumes==0);} // Console must actually finish a command first.
  {Test t;t.failover();assert(t.route.submitIsolation("bdi-console","GE1/0/2")=="queued");
   t.route.tick(6000);t.route.tick(20000);assert(t.resumes==0);
   t.uart.reply.state="ok";t.route.tick(21000);t.route.tick(25999);assert(t.resumes==0);
   t.route.tick(26000);assert(t.resumes==1&&t.route.usingBackup()&&t.stops==0);
   assert(t.route.submit("must-wait","cpu")=="busy"&&t.uart.submissions==1);
   t.ssh.online=true;t.route.tick(26001);assert(!t.route.usingBackup()&&t.stops==1);
   assert(t.route.takeResult("bdi-console").state=="ok"&&t.ssh.submissions==0);
   assert(t.route.submit("new-ssh","cpu")=="queued"&&t.ssh.submissions==1);}
  {Test t;t.failover();t.completeRead(6000);t.route.tick(10000);
   assert(t.route.submit("another-console","cpu")=="queued");t.route.tick(20000);assert(t.resumes==0);
   t.uart.reply.state="ok";t.route.tick(21000);t.route.tick(25999);assert(t.resumes==0);t.route.tick(26000);assert(t.resumes==1);}
  {Test t;t.failover();t.completeRead(6000);t.route.tick(11000);assert(t.resumes==1);
   t.ssh.state="connect_failed";t.route.tick(11001);assert(t.route.usingBackup()&&t.stops==0&&t.pauses==2);
   assert(t.route.result("console-read").state=="ok");assert(t.route.submit("continue-console","cpu")=="queued");
   t.uart.reply.state="ok";t.route.tick(12000);t.route.tick(17000);assert(t.resumes==2);}
  {Test t;t.failover();t.completeRead(6000);t.route.tick(11000);
   t.ssh.state="host_key_mismatch";t.route.tick(11001);t.route.tick(999999);assert(t.resumes==1&&t.route.usingBackup());
   assert(t.route.submit("still-console","cpu")=="queued");}
  {Test t;t.stopped=false;t.failover();assert(!t.route.connected());assert(t.route.submit("too-soon","version")=="not_connected");
   t.stopped=true;t.route.tick(6000);assert(t.route.connected()&&t.route.submit("safe-console","version")=="queued");}
  {Test t;t.failover();t.completeRead(6000);t.route.tick(11000);t.route.tick(70999);assert(t.pauses==1);
   t.route.tick(71000);assert(t.pauses==2&&t.stops==0&&t.route.usingBackup());}
  {Test t;t.failover();t.completeRead(6000);t.route.tick(11000);t.ssh.online=true;t.ssh.trusted=false;t.route.tick(12000);assert(t.route.usingBackup());
   t.ssh.trusted=true;t.route.tick(12001);assert(!t.route.usingBackup()&&t.stops==1);
   t.ssh.online=false;t.route.tick(13000);t.route.tick(18000);assert(t.route.usingBackup()&&t.starts==2);}
  {Test t;t.route.configure(false);t.failover();assert(!t.route.connected()&&t.starts==0);}
  {Test t;t.ssh.online=true;t.route.submitIsolation("bdi-old","GE1/0/2");t.route.configure(true);t.ssh.online=false;t.failover();
   assert(!t.route.connected()&&t.starts==0&&t.route.submit("new","version")=="reconfiguration_requires_restart");}
  {Test t;t.ssh.online=true;t.route.submit("saved-read","version");t.ssh.reply.state="ok";assert(t.route.result("saved-read").state=="ok");
   t.ssh.online=false;t.failover();assert(t.route.submit("saved-read","version")=="ok"&&t.uart.submissions==0);
   assert(t.route.submit("saved-read","cpu")=="id_conflict");}
  {Test t;t.route.tick(100);t.route.setWaitSeconds(30);t.route.tick(5100);assert(t.starts==0);
   t.route.setWaitSeconds(1);t.route.tick(5101);assert(t.starts==1);}
}
