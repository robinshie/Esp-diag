#pragma once
#include <cstdint>

// Network reachability evidence only; SSH and business faults are independent.
class HeartbeatState {
public:
  enum class State { WaitingSsh, Unknown, Healthy, Suspect, Lost, Recovering };
  static constexpr uint64_t IntervalMs=5000, FreshMs=15000;
  State state=State::WaitingSsh;
  bool armed=false, alarm=false, haveSample=false;
  unsigned misses=0, successes=0;
  uint64_t lastSample=0;
  uint32_t rtt=0;
  const char* reason="WAITING_SSH";
  void reset(){*this=HeartbeatState();}
  void ssh(bool connected){if(connected&&!armed){armed=true;state=State::Unknown;reason="WAITING_REPLY";}}
  void observe(bool ok,uint64_t at,uint32_t elapsed,const char* why){
    haveSample=true;lastSample=at;rtt=ok?elapsed:0;reason=why;
    if(ok){
      misses=0;if(successes<2)++successes;
      if(alarm&&successes<2)state=State::Recovering;
      else {alarm=false;state=State::Healthy;}
    }else{
      successes=0;if(misses<3)++misses;
      if(misses>=3)alarm=true;
      state=alarm?State::Lost:State::Suspect;
    }
  }
  void tick(uint64_t now){
    if(armed&&haveSample&&now-lastSample>FreshMs){state=State::Unknown;successes=0;reason="MONITOR_STALE";}
  }
  const char* name() const {
    switch(state){
      case State::WaitingSsh:return "waiting_ssh";case State::Unknown:return "unknown";
      case State::Healthy:return "healthy";case State::Suspect:return "suspect";
      case State::Lost:return "lost";case State::Recovering:return "recovering";
    }return "unknown";
  }
};
