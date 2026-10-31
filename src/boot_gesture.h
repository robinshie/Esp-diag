#pragma once
#include <cstdint>

// Debounced active-low BOOT gestures; time differences also work across millis wrap.
class BootGesture {
  bool raw=false,down=false,held=false;
  uint32_t changed=0,pressed=0,released=0;
  unsigned clicks=0;
public:
  enum Action {None,ViewLogs,Configure};
  Action tick(bool low,uint32_t now){
    if(low!=raw){raw=low;changed=now;}
    if(raw!=down && uint32_t(now-changed)>=30){
      down=raw;
      if(down){pressed=now;held=false;if(clicks && uint32_t(now-released)>600)clicks=0;}
      else {
        const auto duration=uint32_t(now-pressed);
        if(!held && duration>=40 && duration<=600){
          released=now;
          if(++clicks==2){clicks=0;return ViewLogs;}
        }else clicks=0;
      }
    }
    if(down && !held && uint32_t(now-pressed)>=3000){held=true;clicks=0;return Configure;}
    return None;
  }
};
