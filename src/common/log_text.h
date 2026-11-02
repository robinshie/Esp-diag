#pragma once
#include <algorithm>
#include <cctype>
#include <string>
#include <vector>
#include <cstdint>
namespace journal {
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
    if(end<line.size())result+=line[end];
    position=end+1;
  }
  return result;
}

}
