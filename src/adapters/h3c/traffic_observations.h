#pragma once
#include "observations.h"

namespace h3c {
inline bool counterValue(const std::string& text,uint64_t& value){
  if(text.empty()||text.size()>14)return false;
  value=0;for(unsigned char c:text){if(c<'0'||c>'9')return false;value=value*10+c-'0';}return true;
}
inline TrafficCounters parseTraffic(const std::string& text,const Stamp& stamp){
  TrafficCounters result;result.stamp=stamp;if(!usableText(text,stamp))return result;
  std::istringstream in(text);std::string line;bool header=false,bad=false;std::set<std::string> seen;
  while(std::getline(in,line)){
    line=trim(line);if(promptOrEcho(line))continue;
    const auto fields=words(line);
    const std::vector<std::string> expected={"Interface","Total","(pkts)","Broadcast","(pkts)","Multicast","(pkts)","Err","(pkts)"};
    if(fields==expected){header=true;continue;}
    const auto row=words(line);if(row.empty()||!portLike(row[0]))continue;
    const auto port=normalizePort(row[0]);TrafficCount count;
    if(!header||row.size()!=5||port.empty()||seen.count(port)||seen.size()>=128){bad=true;continue;}
    seen.insert(port);
    if(!counterValue(row[1],count.total)||!counterValue(row[2],count.broadcast)||!counterValue(row[3],count.multicast)||!counterValue(row[4],count.errors))continue; // Unsupported/overflow rows supply no evidence.
    if(count.broadcast>count.total||count.multicast>count.total){bad=true;continue;}
    result.ports[port]=count;
  }
  result.valid=header&&!bad;return result;
}
}
