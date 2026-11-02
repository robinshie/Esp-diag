#pragma once
#include "observations.h"

namespace h3c {
inline bool counter32(const std::string& text,uint32_t& value){
  if(text.empty()||text.size()>10)return false;
  uint64_t n=0;for(char c:text){if(c<'0'||c>'9')return false;n=n*10+unsigned(c-'0');if(n>UINT32_MAX)return false;}
  value=uint32_t(n);return true;
}
inline bool macText(const std::string& text){
  if(text.size()!=14)return false;
  for(size_t i=0;i<text.size();++i)if(i==4||i==9){if(text[i]!='-')return false;}else if(!std::isxdigit(static_cast<unsigned char>(text[i])))return false;
  return true;
}
inline bool moveTime(const std::string& date,const std::string& time){
  if(date.size()!=10||time.size()!=8||date[4]!='-'||date[7]!='-'||time[2]!=':'||time[5]!=':')return false;
  for(size_t i=0;i<date.size();++i)if(i!=4&&i!=7&&!std::isdigit(static_cast<unsigned char>(date[i])))return false;
  for(size_t i=0;i<time.size();++i)if(i!=2&&i!=5&&!std::isdigit(static_cast<unsigned char>(time[i])))return false;
  return true;
}
inline MacMoves parseMacMoves(const std::string& text,const Stamp& stamp){
  MacMoves result;result.stamp=stamp;if(!usableText(text,stamp))return result;
  bool header=false,footer=false,bad=false,empty=false;uint32_t declared=0;
  std::istringstream input(text);std::string line;
  while(std::getline(input,line)){
    line=trim(line);if(promptOrEcho(line))continue;
    if(line.find("MAC address")!=std::string::npos&&line.find("Current port")!=std::string::npos&&line.find("Source port")!=std::string::npos&&line.find("Times")!=std::string::npos){if(header)bad=true;header=true;continue;}
    if(line=="No MAC address moving records found."){if(empty||footer)bad=true;empty=true;continue;}
    const auto row=words(line);
    if(row.size()==8&&row[0]=="---"&&row[2]=="MAC"&&row[3]=="address"&&row[4]=="moving"&&row[5]=="records"&&row[6]=="found"&&row[7]=="---"){
      if(footer||!counter32(row[1],declared)||declared>200)bad=true;
      footer=true;continue;
    }
    MacMove record;
    if(!header||footer||empty||row.size()!=7||!macText(row[0])||!integer(row[1],record.vlan)||!record.vlan||
       (record.current=normalizePort(row[2])).empty()||(record.source=normalizePort(row[3])).empty()||record.current==record.source||
       !moveTime(row[4],row[5])||!counter32(row[6],record.times)||!record.times||result.records.size()>=200){bad=true;continue;}
    record.mac=row[0];std::transform(record.mac.begin(),record.mac.end(),record.mac.begin(),[](unsigned char c){return char(std::tolower(c));});
    record.time=row[4]+" "+row[5];
    const auto key=record.mac+":"+std::to_string(record.vlan)+":"+record.current+":"+record.source;
    if(!result.records.emplace(key,record).second)bad=true;
  }
  // A complete header/footer pair, or an explicit empty response, is needed.
  // Echo+prompt cannot establish absence of move history.
  result.valid=!bad&&(empty?result.records.empty()&&!footer:header&&footer&&declared==result.records.size());return result;
}
inline CpuUsage parseCpuUsage(const std::string& text,const Stamp& stamp){
  CpuUsage result;result.stamp=stamp;if(!usableText(text,stamp))return result;
  bool section=false,bad=false,found=false;std::istringstream input(text);std::string line;
  while(std::getline(input,line)){
    line=trim(line);
    if(starts(line,"Slot ")&&line.find(" CPU ")!=std::string::npos&&line.find(" CPU usage:")!=std::string::npos){section=true;continue;}
    auto at=line.find("% in last 5 seconds");if(at==std::string::npos)continue;
    unsigned value;
    if(!section||line.substr(at)!="% in last 5 seconds"||!integer(line.substr(0,at),value,100)){bad=true;continue;}
    found=true;result.percent=std::max(result.percent,value);
  }
  result.valid=found&&!bad;return result;
}
} // namespace h3c
