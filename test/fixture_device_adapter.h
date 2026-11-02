#pragma once
#include "../src/adapters/device_adapter.h"

// Deliberately not a supported vendor. Proves orchestration/transport can change
// dialect and collection order without pretending a second real device is tested.
class FixtureDeviceAdapter final:public switchdevice::IDeviceAdapter {
public:
  const char* id()const override{return "test-fixture-switch";}
  size_t queryCount()const override{return 2;}
  const char* queryAt(size_t index)const override{return index==0?"state":index==1?"identity":"";}
  const char* readCommand(const std::string& key)const override{
    return key=="state"?"show state":key=="identity"?"show identity":nullptr;
  }
  bool accept(const std::string& key,const std::string& output,const switchmodel::Stamp& stamp,switchmodel::Observation& o)const override{
    using namespace switchmodel;
    if(key=="identity"){o.identity=stamp.complete&&output=="FixtureSwitch";return true;}
    if(key!="state")return false;
    if(output=="omitted")return false; // Deliberately leave fields untouched to test round isolation.
    o.snapshot=Snapshot();o.snapshot.sshConnected=true;o.snapshot.targetRevision=stamp.targetRevision;
    o.snapshot.interfaces.stamp=o.snapshot.down.stamp=o.snapshot.loops.stamp=o.snapshot.stp.stamp=o.snapshot.aggregation.stamp=stamp;
    const bool valid=stamp.complete&&(output=="normal"||output=="loop");
    o.snapshot.interfaces.valid=o.snapshot.down.valid=o.snapshot.loops.valid=o.snapshot.stp.valid=o.snapshot.aggregation.valid=valid;
    Interface p;p.name=canonicalPort("Ethernet2");p.link=Link::Up;p.type='A';p.pvid=10;
    o.snapshot.interfaces.ports[p.name]=p;
    if(output=="loop"){o.snapshot.loops.enabled=true;o.snapshot.loops.intervalSeconds=30;o.snapshot.loops.ports[p.name]=LoopPort();}
    o.macMoves=MacMoves();o.macMoves.stamp=stamp;o.macMoves.valid=valid;
    o.cpu=CpuUsage();o.cpu.stamp=stamp;o.cpu.valid=valid;o.cpu.percent=5;
    return valid;
  }
  std::string canonicalPort(const std::string& native)const override{
    if(native=="Ethernet1")return "GigabitEthernet1/0/1";
    if(native=="Ethernet2")return "GigabitEthernet1/0/2";
    return switchmodel::normalizePort(native);
  }
  std::vector<std::string> actionCommands(switchmodel::Action action,const std::string& port)const override{
    if(action!=switchmodel::Action::Isolate||canonicalPort(port)!=port||port!="GigabitEthernet1/0/2")return {};
    return {"configure","port Ethernet2","disable","end"};
  }
  std::vector<std::string> actionPrompts(switchmodel::Action action,const std::string& port,const std::string& user)const override{
    if(actionCommands(action,port).empty()||user!="Fixture#")return {};
    return {"Fixture(config)#","Fixture(port2)#","Fixture(port2)#","Fixture#"};
  }
  std::string prompt(const std::string& tail)const override{
    const auto text=switchmodel::trim(tail);return !text.empty()&&text.back()=='#'?text:"";
  }
  bool userPrompt(const std::string& text)const override{return text=="Fixture#";}
  bool failed(const std::string& output)const override{return output.find("fixture-error")!=std::string::npos;}
  std::vector<std::string> sessionSetup()const override{return {"pager off"};}
};
