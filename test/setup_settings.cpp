#include "../src/config/setup_settings.h"
#include <cassert>
int main(){
  {SetupSettings old,loaded;auto device=old.device.encode();device=device.substr(0,device.rfind('\n'));
   std::string raw="2\n";for(const auto& part:{old.network.encode(),device,old.mqtt.encode()})raw+=std::to_string(part.size())+":"+part;
   assert(loaded.decode(raw)&&loaded.device.failoverWaitSeconds==5);}
  SetupSettings saved,copy;
  saved.network.ip="192.168.23.6";saved.network.gateway="192.168.23.1";
  saved.device.protocol="ssh";saved.device.host="192.168.23.2";saved.device.username="admin";
  saved.device.failoverWaitSeconds=27;
  saved.device.password="  |:&=+ secret  ";saved.device.fingerprint="SHA256:"+std::string(43,'A');
  saved.mqtt.enabled=true;saved.mqtt.host="192.168.23.10";saved.mqtt.password="mqtt|:&=+";
  assert(saved.validate().empty());assert(copy.decode(saved.encode()));
  assert(copy.network.ip==saved.network.ip&&copy.device.password==saved.device.password&&copy.mqtt.password==saved.mqtt.password&&copy.mqtt.enabled);
  assert(copy.device.failoverWaitSeconds==27);
  auto original=copy.encode();
  saved.mqtt.port=0;assert(!copy.decode(saved.encode()));assert(copy.encode()==original);
  saved.mqtt.port=1883;saved.device.host=saved.network.ip;assert(!copy.decode(saved.encode()));assert(copy.encode()==original);
  for(auto bad:{"", "2\n", "2\n9999:x", "2\n-1:x", "2\n999999999:x", "3\n1:a"}){assert(!copy.decode(bad));assert(copy.encode()==original);}
  assert(!copy.decode(original+"extra"));assert(copy.encode()==original);
  for(size_t i=0;i<original.size();++i){assert(!copy.decode(original.substr(0,i)));assert(copy.encode()==original);}
  SetupSettings empty;assert(copy.decode(empty.encode()));assert(copy.device.protocol=="ssh"&&copy.device.host=="192.168.10.2"&&!copy.mqtt.enabled);
}
