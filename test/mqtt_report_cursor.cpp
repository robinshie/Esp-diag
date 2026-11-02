#include "../src/net/mqtt_report_cursor.h"
#include <cassert>
#include <iostream>
int main(){
  const std::string scope="192.168.10.10:1883/esp-switch/esp-123456abcdef";uint32_t cursor=7;
  assert(mqtt_report::decodeCursor(mqtt_report::encodeCursor(scope,19),scope,cursor)&&cursor==19);
  for(auto value:{scope+"\n",scope+"\n-1",scope+"\n4294967295",scope+"\n20\n",scope+"\n1x",std::string("other\n99")}){
    assert(!mqtt_report::decodeCursor(value,scope,cursor)&&cursor==19);
  }
  mqtt_report::Flight flight;assert(!flight.busy());assert(flight.acknowledge(3)==0);
  flight.sent(3,19);assert(flight.busy());assert(flight.acknowledge(2)==0&&flight.busy());
  assert(flight.acknowledge(3)==19&&!flight.busy());assert(flight.acknowledge(3)==0);
  flight.sent(4,20);flight.reset();assert(flight.acknowledge(4)==0);
  flight.sent(5,20);assert(flight.acknowledge(5)==20);
  std::cout<<"MQTT cursor identity, corruption, ACK matching, retry and deduplication passed\n";
}
