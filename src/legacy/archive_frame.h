#pragma once
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <map>
#include <string>

namespace archive {
inline bool hex(const std::string& s,size_t length){
  if(s.size()!=length)return false;
  for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;
  return true;
}
inline bool number(const std::string& text,uint64_t& value,uint64_t limit){
  if(text.empty()||text.size()>20)return false;
  value=0;
  for(char c:text){if(c<'0'||c>'9'||value>(limit-(c-'0'))/10)return false;value=value*10+(c-'0');}
  return true;
}
struct Frame {
  char req[17]{},key[65]{};char op=0;
  uint64_t sequence=0;uint32_t size=0,offset=0;uint16_t count=0;uint8_t data[240]{};
  bool parse(const std::string& text){
    if(text.compare(0,9,"H3C COPY ")!=0||text.size()>511)return false;
    std::map<std::string,std::string> fields;size_t at=9;
    while(at<text.size()){
      size_t end=text.find(' ',at);if(end==std::string::npos)end=text.size();
      auto token=text.substr(at,end-at);auto split=token.find('=');
      if(split==std::string::npos||!fields.emplace(token.substr(0,split),token.substr(split+1)).second)return false;
      at=end+1;
    }
    if(!hex(fields["req"],16)||!hex(fields["key"],64))return false;
    strcpy(req,fields["req"].c_str());strcpy(key,fields["key"].c_str());
    auto kind=fields["op"];
    if(kind=="status"||kind=="end"){op=kind=="status"?'S':'E';return fields.size()==3;}
    uint64_t n;
    if(kind=="begin"){
      if(fields.size()!=5||!number(fields["seq"],sequence,9007199254740991ULL)||!sequence||!number(fields["size"],n,2*1024*1024)||!n)return false;
      op='B';size=n;return true;
    }
    if(kind!="data"||fields.size()!=5||!number(fields["offset"],n,2*1024*1024))return false;
    offset=n;op='D';const auto& encoded=fields["data"];
    if(encoded.empty()||encoded.size()%4||encoded.size()>320)return false;
    const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t bits=0;unsigned bitCount=0;bool padded=false;count=0;unsigned padding=0;
    for(size_t i=0;i<encoded.size();++i){
      char c=encoded[i];if(c=='='){padded=true;++padding;if(i<encoded.size()-2||padding>2)return false;continue;}
      if(padded)return false;
      const char* p=strchr(alphabet,c);if(!p||!c)return false;
      bits=(bits<<6)|unsigned(p-alphabet);bitCount+=6;
      if(bitCount>=8){bitCount-=8;if(count>=sizeof(data))return false;data[count++]=uint8_t(bits>>bitCount);}
    }
    return count>0&&count<=sizeof(data)&&((padding==0&&bitCount==0)||(padding==1&&bitCount==2)||(padding==2&&bitCount==4))&&(bits&((1u<<bitCount)-1))==0;
  }
};
struct FileInfo {
  uint64_t sequence=0;uint32_t size=0;std::string key;
  std::string name() const {char seq[24];snprintf(seq,sizeof(seq),"%020llu",(unsigned long long)sequence);return std::string(seq)+"-"+key+".json";}
  static bool parse(const std::string& text,FileInfo& out){
    if(text.size()!=90||text[20]!='-'||text.substr(85)!=".json"||!hex(text.substr(21,64),64))return false;
    if(!number(text.substr(0,20),out.sequence,9007199254740991ULL)||!out.sequence)return false;
    out.key=text.substr(21,64);return true;
  }
};
}
