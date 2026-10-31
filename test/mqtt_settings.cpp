#include "../src/mqtt_settings.h"
#include <cassert>
int main(){
 MqttSettings s;assert(!s.enabled);assert(s.validate().empty());
 s.enabled=true;assert(!s.validate().empty());s.host="192.168.23.10";s.username="lab";s.password="a|b&=c";
 assert(s.validate().empty());MqttSettings copy;assert(copy.decode(s.encode()));assert(copy.enabled&&copy.password==s.password&&copy.host==s.host);
 auto before=copy.encode();assert(!copy.decode("1\n127.0.0.1\n1883\nlab\nsecret\nesp-lab"));assert(copy.encode()==before);
 s.prefix="h3c";assert(!s.validate().empty());s.prefix="esp-lab/#";assert(!s.validate().empty());s.prefix="esp-lab-test";
 s.password="bad\npassword";assert(!s.validate().empty());s.password=std::string(129,'a');assert(!s.validate().empty());
 unsigned port=1883;for(auto bad:{"0","65536","1883x","-1","999999999"}){assert(!MqttSettings::parsePort(bad,port));assert(port==1883);}
 assert(MqttSettings::parsePort("65535",port));assert(port==65535);
 assert(!copy.decode(before+"\nextra"));
}
