#pragma once
#include <cctype>
#include <string>
namespace switchcli {
inline const char* command(const std::string& name){
  if(name=="version")return "display version";
  if(name=="device")return "display device";
  if(name=="interfaces")return "display interface brief";
  if(name=="cpu")return "display cpu-usage";
  if(name=="memory")return "display memory";
  if(name=="down")return "display interface brief down";
  if(name=="loops")return "display loopback-detection";
  if(name=="stp")return "display stp brief";
  if(name=="aggregation")return "display link-aggregation verbose";
  if(name=="logs")return "display logbuffer reverse";
  return nullptr;
}
inline bool identifier(const std::string& text){
  if(text.empty()||text.size()>32)return false;
  for(unsigned char c:text)if(!std::isalnum(c)&&c!='_'&&c!='-')return false;
  return true;
}
inline std::string encode(const std::string& text){
  std::string out;const char* hex="0123456789ABCDEF";
  for(unsigned char c:text){if(std::isalnum(c)||c=='-'||c=='_'||c=='.')out+=char(c);else{out+='%';out+=hex[c>>4];out+=hex[c&15];}}return out;
}
struct Result {
  std::string id,command,target,state="idle",output;
  bool truncated=false;
  bool done() const{return state!="idle"&&state!="queued"&&state!="running";}
  std::string fields() const{return "request_id="+encode(id)+"&command="+encode(command)+"&target="+encode(target)+"&state="+encode(state)+"&truncated="+(truncated?"1":"0")+"&output="+encode(output);}
};
// Bounded output and independent tail allow detecting a split prompt even when truncated.
class Capture {
  std::string tail;
  enum Escape {None,Start,Csi,Osc,OscEnd} escape=None;
public:
  static constexpr size_t Limit=8192;
  std::string output;
  bool truncated=false;
  void append(const char* data,size_t size){
    for(size_t i=0;i<size;++i){unsigned char c=data[i];
      if(escape==Start){escape=c=='['?Csi:c==']'?Osc:None;continue;}
      if(escape==Csi){if(c>=0x40&&c<=0x7e)escape=None;continue;}
      if(escape==Osc){if(c==7)escape=None;else if(c==27)escape=OscEnd;continue;}
      if(escape==OscEnd){escape=c=='\\'?None:Osc;continue;}
      if(c==27){escape=Start;continue;}
      if(c=='\r')continue;
      if(c=='\b'){if(!tail.empty())tail.pop_back();if(!output.empty())output.pop_back();continue;}
      if(c<32&&c!='\n'&&c!='\t')continue;
      if(c==127)continue;
      if(c=='\n')tail.clear();else {tail+=char(c);if(tail.size()>160)tail.erase(0,tail.size()-160);}
      if(output.size()<Limit)output+=char(c);else truncated=true;
    }
  }
  std::string prompt() const{
    auto end=tail.find_last_not_of(" \t");if(end==std::string::npos)return "";
    auto start=tail.find_first_not_of(" \t");auto text=tail.substr(start,end-start+1);
    if(text.size()<3||text.size()>128)return "";
    if(!((text.front()=='<'&&text.back()=='>')||(text.front()=='['&&text.back()==']')))return "";
    if(text.find_first_of("\r\n",1)!=std::string::npos)return "";
    return text;
  }
  bool failed() const{return output.find("% Unrecognized")!=std::string::npos||output.find("% Incomplete")!=std::string::npos||output.find("% Ambiguous")!=std::string::npos||output.find("Permission denied")!=std::string::npos||output.find("Error:")!=std::string::npos||output.find("% Too many")!=std::string::npos||output.find("% Failed")!=std::string::npos||output.find("% Permission")!=std::string::npos||output.find("% Wrong parameter")!=std::string::npos;}
};
}
