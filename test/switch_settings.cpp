#include "../src/switch_settings.h"
#include <cassert>
int main(){
  SwitchSettings s,copy;assert(s.protocol=="none"&&s.validate().empty());
  assert(copy.decode(s.encode()));
  s.protocol="ssh";assert(!s.validate().empty());
  s.host="192.168.23.2";s.username="admin";s.password="a|&=+ secret";
  assert(s.validate().empty()); // SSH credentials can be saved without a host fingerprint.
  s.fingerprint="SHA256:"+std::string(43,'A');assert(s.validate().empty());
  s.enablePassword="enable|secret";assert(copy.decode(s.encode()));
  NetworkSettings net;net.ip="192.168.23.6";net.gateway="192.168.23.1";
  assert(s.validateNetwork(net).empty());
  s.host=net.ip;assert(!s.validateNetwork(net).empty());
  s.host="192.168.23.255";assert(!s.validateNetwork(net).empty());
  s.host="192.168.24.2";net.gateway="0.0.0.0";assert(!s.validateNetwork(net).empty());
  net.gateway="192.168.23.1";assert(s.validateNetwork(net).empty());s.host="192.168.23.2";
  assert(copy.password==s.password&&copy.enablePassword==s.enablePassword&&copy.fingerprint==s.fingerprint);
  auto before=copy.encode();assert(!copy.decode(before+"\nextra"));assert(copy.encode()==before);
  s.fingerprint="";assert(copy.decode(s.encode()));assert(copy.protocol=="ssh"&&copy.fingerprint.empty());
  s.protocol="telnet";s.fingerprint="";s.port=23;assert(s.validate().empty());
  for(auto host:{"", "127.0.0.1","0.0.0.0","224.0.0.1","host.example","192.168.2.999"}){s.host=host;assert(!s.validate().empty());}
  s.host="192.168.23.2";s.protocol="snmp";assert(!s.validate().empty());s.protocol="ssh";
  for(auto fp:{"SHA256:short","MD5:00:11"}){s.fingerprint=fp;assert(!s.validate().empty());}
  s.fingerprint="SHA256:"+std::string(42,'A')+"!";assert(!s.validate().empty());
  s.fingerprint="SHA256:"+std::string(43,'A');s.password="line\nbreak";assert(!s.validate().empty());
  s.password=std::string(129,'a');assert(!s.validate().empty());s.password="";
  s.connectTimeout=0;assert(!s.validate().empty());s.connectTimeout=5;s.commandTimeout=31;assert(!s.validate().empty());
  unsigned timeout=5;for(auto bad:{"0","31","-1","5s","999999"}){assert(!SwitchSettings::timeout(bad,timeout));assert(timeout==5);}
  assert(SwitchSettings::timeout("30",timeout)&&timeout==30);
}
