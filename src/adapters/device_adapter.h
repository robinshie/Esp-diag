#pragma once
#include "../domain/switch_policy.h"

namespace switchdevice {
// Immutable, stateless device dialect. The same instance is shared by Runtime
// and the SSH worker; it owns no SSH objects, clocks, credentials or action state.
class IDeviceAdapter {
public:
  virtual ~IDeviceAdapter()=default;
  virtual const char* id()const=0;
  virtual size_t queryCount()const=0;
  virtual const char* queryAt(size_t index)const=0;
  virtual const char* readCommand(const std::string& key)const=0;
  virtual bool accept(const std::string& key,const std::string& output,const switchmodel::Stamp& stamp,switchmodel::Observation& observation)const=0;
  virtual std::string canonicalPort(const std::string& native)const=0;
  virtual std::vector<std::string> actionCommands(switchmodel::Action action,const std::string& canonical)const=0;
  virtual std::vector<std::string> actionPrompts(switchmodel::Action action,const std::string& canonical,const std::string& userPrompt)const=0;
  virtual std::string prompt(const std::string& tail)const=0;
  virtual bool userPrompt(const std::string& prompt)const=0;
  virtual bool failed(const std::string& output)const=0;
  virtual std::vector<std::string> sessionSetup()const=0;
  virtual const char* hostKeyAlgorithms()const{return nullptr;}
  virtual const char* freshObservationReason()const{return "fresh_device_observation";}
};
}
