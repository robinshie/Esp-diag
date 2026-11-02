#pragma once
#include "../common/log_text.h"

// JSON report format shared by the on-device page. No invented wall-clock time.
class LocalReport {
  struct Line {uint64_t at;std::string text;};
  std::vector<Line> lines;
  std::vector<std::string> analysis;
  size_t bytes=0,encodedBytes=0;
  uint64_t analysisAt=0;
public:
  static constexpr size_t MaxLines=64,MaxBytes=48*1024;
  static constexpr size_t MaxWireBytes=64*1024;
  static std::string safeText(const std::string& raw){
    auto text=journal::redact(raw);std::string out;size_t at=0;
    while(at<text.size()){
      const auto c=uint8_t(text[at]);unsigned n=c<0x80?1:c>=0xc2&&c<=0xdf?2:c>=0xe0&&c<=0xef?3:c>=0xf0&&c<=0xf4?4:0;
      bool valid=n&&at+n<=text.size();
      for(unsigned i=1;valid&&i<n;++i)valid=(uint8_t(text[at+i])&0xc0)==0x80;
      if(valid&&n>=3){auto second=uint8_t(text[at+1]);valid=!(c==0xe0&&second<0xa0)&&!(c==0xed&&second>=0xa0)&&!(c==0xf0&&second<0x90)&&!(c==0xf4&&second>=0x90);}
      const unsigned size=valid?n:3;if(out.size()+size>1000)break;
      if(valid){out.append(text,at,n);at+=n;}else{out+="\xef\xbf\xbd";++at;}
    }
    if(at<text.size())out+=" [truncated]";
    return out;
  }
  bool add(uint64_t at,const std::string& text){
    if(lines.size()>=MaxLines||bytes+text.size()>MaxBytes)return false;
    auto safe=safeText(text);const auto encoded=journal::quote(safe).size()+64;
    if(encodedBytes+encoded>MaxWireBytes-1024)return false;
    lines.push_back({at,safe});bytes+=safe.size();encodedBytes+=encoded;return true;
  }
  bool addAnalysis(uint64_t at,const std::string& raw){
    if(analysis.size()>=8||raw.empty()||raw.size()>1000||raw.front()!='{'||raw.back()!='}'||encodedBytes+raw.size()+1>MaxWireBytes-1024)return false;
    analysis.push_back(raw);encodedBytes+=raw.size()+1;analysisAt=std::max(analysisAt,at);return true;
  }
  bool empty() const{return lines.empty()&&analysis.empty();}
  size_t count() const{return lines.size()+analysis.size();}
  void clear(){lines.clear();analysis.clear();bytes=0;encodedBytes=0;analysisAt=0;}
  std::string json(const std::string& boot,uint64_t sequence) const {
    uint64_t at=analysisAt;for(const auto& line:lines)at=std::max(at,line.at);
    std::string result="{\"schema_version\":1,\"source\":\"esp_native\",\"transport\":\"ETH\",\"clock\":\"uptime\",\"boot\":"+journal::quote(boot)+",\"report_id\":"+journal::quote(boot+"-"+std::to_string(sequence))+",\"sent_at_ms\":"+std::to_string(at)+",\"lines\":[";
    bool first=true;for(const auto& line:lines){if(!first)result+=',';first=false;result+="{\"at_ms\":"+std::to_string(line.at)+",\"text\":"+journal::quote(line.text)+"}";}
    result+="],\"analysis\":[";first=true;for(const auto& item:analysis){if(!first)result+=',';first=false;result+=item;}
    return result+"]}";
  }
};
