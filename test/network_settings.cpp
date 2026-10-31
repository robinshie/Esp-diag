#include "../src/network_settings.h"
#include <cassert>
int main(){
 NetworkSettings s;assert(s.validate().empty());
 auto original=s.encode();assert(!s.decode("192.168.4.0|255.255.255.0|192.168.4.254|0.0.0.0"));assert(s.encode()==original);
 assert(!s.decode("192.168.4.255|255.255.255.0|0.0.0.0|0.0.0.0"));
 assert(!s.decode("192.168.4.2|255.0.255.0|0.0.0.0|0.0.0.0"));
 assert(!s.decode("192.168.4.2|255.255.255.0|192.168.5.1|0.0.0.0"));
 assert(!s.decode("127.0.0.1|255.0.0.0|0.0.0.0|0.0.0.0"));
 assert(!s.decode("1.2.3.256|255.255.255.0|0.0.0.0|0.0.0.0"));
 assert(!s.decode("1.2.3.4|255.255.255.0|0.0.0.0|0.0.0.0|extra"));
 assert(s.decode("10.0.0.2|255.255.255.0|10.0.0.1|8.8.8.8"));
 assert(s.decode("10.0.0.2|255.255.255.252|0.0.0.0|0.0.0.0"));
}
