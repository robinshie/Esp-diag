#pragma once
#include "cluster_model.h"
#include <algorithm>
#include <cctype>
#include <set>

namespace journal {
enum class Kind : uint8_t { Raw, Process, Summary };
inline std::string quote(const std::string& text) {
  std::string out="\"";const char* hex="0123456789abcdef";
  for(unsigned char c:text){
    if(c=='"'||c=='\\'){out+='\\';out+=char(c);}
    else if(c<32){out+="\\u00";out+=hex[c>>4];out+=hex[c&15];}
    else out+=char(c);
  }
  return out+'"';
}
inline std::string redact(const std::string& line) {
  // Accepted device log frames can contain extra fields. Never persist common credentials.
  std::string result;size_t position=0;
  while(position<line.size()){
    size_t end=line.find_first_of(" \t\r\n",position);if(end==std::string::npos)end=line.size();
    std::string token=line.substr(position,end-position);auto split=token.find('=');
    if(split!=std::string::npos){
      auto key=token.substr(0,split);std::transform(key.begin(),key.end(),key.begin(),[](unsigned char c){return char(std::tolower(c));});
      if(key.find("password")!=std::string::npos||key.find("secret")!=std::string::npos||key.find("token")!=std::string::npos||key=="passwd"||key=="pwd"||key=="private_key")return result+token.substr(0,split+1)+"[REDACTED_REMAINDER]";
    }
    result+=token;
    if(end<line.size())result+=line[end];position=end+1;
  }
  return result;
}

// Summaries use the authoritative model, not an assumption that a successful ACK fixes a fault.
class FaultJournal {
  struct Active { std::string fields;uint64_t generation,start; };
  std::map<std::string,Active> active;
  std::map<std::string,bool> availability;
  bool capacityReported=false, linkKnown=false, lastLink=false;
  uint64_t now=0;
  void summary(const std::string& event,const std::string& fields,const std::string& message) {
    if(sink)sink(Kind::Summary,"\"event\":"+quote(event)+","+fields+",\"message\":"+quote(message));
  }
public:
  std::function<void(Kind,const std::string&)> sink;
  bool incomplete=false;
  void clock(uint64_t uptime){now=uptime;}
  uint64_t uptime() const{return now;}
  void input(const std::string& line) {
    if(line.compare(0,8,"H3C LOG ")==0&&sink)sink(Kind::Raw,"\"direction\":\"input\",\"text\":"+quote(redact(line)));
  }
  void output(const std::string& line) {
    if(line.compare(0,6,"[BDI][")!=0)return;
    const bool truncated=line.size()>1024;
    const auto safe=redact(line.substr(0,1024));
    const auto fields="\"direction\":\"output\",\"truncated\":"+std::string(truncated?"true":"false")+",\"text\":"+quote(safe);
    if(sink)sink(Kind::Raw,fields);
    if(line.compare(0,10,"[BDI][CMD]")==0||line.compare(0,10,"[BDI][ACK]")==0||line.compare(0,16,"[BDI][RECOVERED]")==0){
      if(sink)sink(Kind::Process,fields);
      if(line.compare(0,16,"[BDI][RECOVERED]")!=0)summary("ACTION_EVIDENCE",fields,"处置请求或回执；是否恢复以独立的故障恢复记录为准。");
    }
  }
  void link(bool up) {
    if(linkKnown&&lastLink==up)return;
    const bool first=!linkKnown;linkKnown=true;lastLink=up;
    summary(up?"ETH_LINK_UP":"ETH_LINK_DOWN","\"component\":\"esp_ethernet\"",up?"ESP 网口物理链路已连接；不代表交换机 SSH 已登录。":first?"启动时 ESP 网口无链路；未确认是否接线。":"ESP 网口链路中断；无法仅凭此区分拔线、交换机断电或端口故障。");
  }
  void observe(const cluster::Model& model) {
    std::set<std::string> seen;
    for(const auto& entry:model.devices){
      const auto& d=entry.second;const auto found=availability.find(d.id);
      if(found==availability.end()||found->second!=d.online){
        summary(d.online?"SOURCE_ONLINE":"SOURCE_LOST","\"device\":"+quote(d.id),d.online?"日志来源恢复通信；不代表已有故障恢复。":"来源心跳过期或不可达；原因待确认，不认定为交换机断电。");
        availability[d.id]=d.online;
      }
      for(const auto& item:d.faults){
        const auto& f=item.second;const std::string key=d.id+":"+item.first;seen.insert(key);
        auto old=active.find(key);
        if(old!=active.end()&&old->second.generation==f.generation)continue;
        if(old!=active.end()){
          summary("SUPERSEDED",old->second.fields,"事件已被新一代故障替代；旧事件恢复情况未确认。");active.erase(old);
        }
        if(active.size()>=64){
          incomplete=true;
          if(!capacityReported){capacityReported=true;summary("SUMMARY_CAPACITY","\"limit\":64","摘要同时跟踪容量已满；后续原始日志仍尝试记录，摘要可能不完整。");}
          continue;
        }
        std::string fields="\"device\":"+quote(d.id)+",\"component\":"+quote(f.component)+",\"fault\":"+quote(f.event)+",\"incident\":"+quote(f.incident)+",\"reported_cause\":"+quote(f.cause)+",\"severity\":"+quote(f.severity)+",\"first_uptime_ms\":"+std::to_string(now);
        active[key]={fields,f.generation,now};
        summary("DETECTED",fields,"发现上报故障；reported_cause 为来源提供的原因，不是独立确认的根因。");
      }
    }
    for(auto item=active.begin();item!=active.end();){
      if(seen.count(item->first)){++item;continue;}
      const auto device=model.devices.find(item->first.substr(0,item->first.find(':')));
      if(device!=model.devices.end()&&device->second.online){
        summary("RECOVERED",item->second.fields+",\"duration_ms\":"+std::to_string(now-item->second.start),"固件已完成该故障的恢复稳定性验证。");
      } else summary("TRACKING_LOST",item->second.fields,"故障跟踪状态丢失，未确认恢复。");
      item=active.erase(item);
    }
  }
};
} // namespace journal
