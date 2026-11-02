#pragma once
#include "heartbeat_state.h"
#include <functional>
#include <string>
namespace switchbdi {
// Heartbeats arrive asynchronously and run independently of CLI collection.
// Keep their strategy input separate from complete switch-observation rounds.
class IHeartbeatStrategy {
public:
  std::function<void()> requestPing;
  virtual ~IHeartbeatStrategy()=default;
  virtual void reset(const std::string& target)=0;
  virtual void tick(uint64_t now,bool sshConnected)=0;
  virtual void probeError()=0;
  virtual void observe(bool ok,uint64_t now,uint32_t rtt,const char* reason)=0;
  virtual const HeartbeatState& state()const=0;
};
}
