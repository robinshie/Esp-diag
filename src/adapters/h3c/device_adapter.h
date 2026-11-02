#pragma once
#include "../device_adapter.h"
#include "observations.h"
#include "discovery_observations.h"
#include "cli.h"

namespace h3c {
class DeviceAdapter final:public switchdevice::IDeviceAdapter {
public:
  const char* id()const override{return "h3c-s5130s-r6328p03";}
  size_t queryCount()const override{return 10;}
  const char* queryAt(size_t index)const override{
    const char* keys[]={"version","interfaces","down","loops","stp","aggregation","logs","cpu","memory","mac_moves"};
    return index<queryCount()?keys[index]:"";
  }
  const char* readCommand(const std::string& key)const override{return h3ccli::command(key);}
  bool accept(const std::string& key,const std::string& output,const switchmodel::Stamp& stamp,switchmodel::Observation& o)const override{
    if(key=="version"){o.identity=stamp.complete&&supportedIdentity(output);return true;}
    if(key=="interfaces"){o.snapshot.interfaces=parseInterfaces(output,stamp);return o.snapshot.interfaces.valid;}
    if(key=="down"){o.snapshot.down=parseDownCauses(output,stamp);return o.snapshot.down.valid;}
    if(key=="loops"){o.snapshot.loops=parseLoops(output,stamp);return o.snapshot.loops.valid;}
    if(key=="stp"){o.snapshot.stp=parseStp(output,stamp);return o.snapshot.stp.valid;}
    if(key=="aggregation"){o.snapshot.aggregation=parseAggregation(output,stamp);return o.snapshot.aggregation.valid;}
    if(key=="cpu"){o.cpu=parseCpuUsage(output,stamp);return o.cpu.valid;}
    if(key=="mac_moves"){o.macMoves=parseMacMoves(output,stamp);return o.macMoves.valid;}
    return key=="logs"||key=="memory";
  }
  std::string canonicalPort(const std::string& native)const override{return switchmodel::normalizePort(native);}
  std::vector<std::string> actionCommands(switchmodel::Action action,const std::string& canonical)const override{
    const auto port=canonicalPort(canonical);
    if(port.empty()||canonical!=port||(action!=switchmodel::Action::Isolate&&action!=switchmodel::Action::RestoreOwned))return {};
    return {"system-view",h3ccli::interfaceCommand(port),action==switchmodel::Action::Isolate?"shutdown":"undo shutdown","return"};
  }
  std::vector<std::string> actionPrompts(switchmodel::Action action,const std::string& canonical,const std::string& user)const override{
    if(actionCommands(action,canonical).empty()||!userPrompt(user))return {};
    const auto system=user.substr(1,user.size()-2),port="["+system+"-"+canonical+"]";
    return {"["+system+"]",port,port,user};
  }
  std::string prompt(const std::string& tail)const override{return h3ccli::prompt(tail);}
  bool userPrompt(const std::string& p)const override{return p.size()>2&&p.front()=='<'&&p.back()=='>';}
  bool failed(const std::string& output)const override{return h3ccli::failed(output);}
  std::vector<std::string> sessionSetup()const override{return {"screen-length disable"};}
  const char* hostKeyAlgorithms()const override{return "+ssh-rsa";}
  const char* freshObservationReason()const override{return "fresh_h3c_observation";}
};
}
