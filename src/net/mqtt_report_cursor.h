#pragma once
#include <cstdint>
#include <string>

namespace mqtt_report {
// Cursor identity includes broker and topic: changing destinations replays the
// retained TF window. A missing/corrupt cursor also safely replays that window.
inline std::string encodeCursor(const std::string& scope,uint32_t sequence){return scope+"\n"+std::to_string(sequence);}
inline bool decodeCursor(const std::string& raw,const std::string& scope,uint32_t& sequence){
  if(raw.size()>256||raw.compare(0,scope.size()+1,scope+"\n")!=0)return false;
  auto value=raw.substr(scope.size()+1);if(value.empty()||value.size()>10)return false;
  uint64_t parsed=0;for(char c:value){if(c<'0'||c>'9')return false;parsed=parsed*10+c-'0';}
  if(parsed>=UINT32_MAX)return false;
  sequence=uint32_t(parsed);return true;
}

// All methods run on Arduino loop. The MQTT callback only queues ACK ids.
class Flight {
  int packet=-1;uint32_t sequence=0;
public:
  bool busy() const{return packet>=0;}
  void sent(int id,uint32_t seq){packet=id;sequence=seq;}
  uint32_t acknowledge(int id){if(packet<0||id!=packet)return 0;auto result=sequence;reset();return result;}
  void reset(){packet=-1;sequence=0;}
};
}
