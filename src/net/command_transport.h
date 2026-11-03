#pragma once
#include "command_result.h"
#include "../domain/switch_policy.h"

namespace switchtransport {
// BDI and HTTP use named commands, never an arbitrary terminal input string.
// Implementations own their session, queue and command boundary detection.
class ICommandTransport {
public:
  virtual ~ICommandTransport()=default;
  virtual const char* kind()const=0;
  virtual const char* stateName()const=0;
  virtual bool connected()=0;
  // SSH requires its pinned host key. Console requires an explicitly enabled
  // physical connection and a user-view prompt; this is not cryptographic trust.
  virtual bool sessionAccepted()=0;
  virtual std::string submit(const std::string& id,const std::string& command)=0;
  virtual std::string submitIsolation(const std::string& id,const std::string& port)=0;
  virtual std::string submitAction(const std::string& id,switchmodel::Action action,const std::string& port){
    return action==switchmodel::Action::Isolate?submitIsolation(id,port):"action_unsupported";
  }
  virtual bool resultReady(const std::string& id)=0;
  virtual switchcli::Result result(const std::string& id)=0;
  virtual switchcli::Result takeResult(const std::string& id)=0;
};
}
