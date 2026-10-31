#include "../src/boot_gesture.h"
#include <cassert>
#include <vector>
struct Button {
  BootGesture gesture;uint32_t now=0;std::vector<BootGesture::Action> events;
  void step(bool low,unsigned ms){for(unsigned i=0;i<ms;++i){auto action=gesture.tick(low,now++);if(action!=BootGesture::None)events.push_back(action);}}
  void tap(){step(true,100);step(false,100);}
};
int main(){
  Button b;b.tap();assert(b.events.empty());b.tap();assert(b.events.size()==1&&b.events[0]==BootGesture::ViewLogs);
  b.step(false,1000);b.step(true,3200);b.step(false,100);assert(b.events.size()==2&&b.events[1]==BootGesture::Configure);
  b.step(false,1000);b.tap();b.step(false,700);b.tap();assert(b.events.size()==2);
  Button bounced;for(int i=0;i<10;++i){bounced.step(true,5);bounced.step(false,5);}assert(bounced.events.empty());
  bounced.step(false,100);bounced.tap();assert(bounced.events.empty());bounced.step(true,3100);assert(bounced.events.size()==1&&bounced.events[0]==BootGesture::Configure);
  bounced.step(true,4000);bounced.step(false,100);assert(bounced.events.size()==1);
  Button medium;medium.step(true,1500);medium.step(false,100);medium.tap();assert(medium.events.empty());
  Button wrap;wrap.now=UINT32_MAX-150;wrap.tap();wrap.tap();assert(wrap.events.size()==1&&wrap.events[0]==BootGesture::ViewLogs);
}
