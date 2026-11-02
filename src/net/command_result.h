#pragma once
#include <cctype>
#include <string>
namespace switchcli {
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
}
