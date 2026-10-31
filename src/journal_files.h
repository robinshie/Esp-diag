#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace journal {
struct Segment {
  uint64_t id=0,size=0;
  unsigned kind=0;
  std::string filename() const {
    char out[48];snprintf(out,sizeof(out),"%020llu-%s.jsonl",(unsigned long long)id,kind==0?"raw":kind==1?"process":"summary");return out;
  }
  static bool parse(const std::string& name,Segment& result){
    if(name.size()<30 || name[20]!='-')return false;
    uint64_t id=0;
    for(unsigned i=0;i<20;++i){
      if(name[i]<'0'||name[i]>'9')return false;
      unsigned digit=name[i]-'0';if(id>(UINT64_MAX-digit)/10)return false;id=id*10+digit;
    }
    const auto suffix=name.substr(21);
    unsigned kind=suffix=="raw.jsonl"?0:suffix=="process.jsonl"?1:suffix=="summary.jsonl"?2:3;
    if(!id||kind==3)return false;
    result.id=id;result.kind=kind;result.size=0;return true;
  }
};
constexpr uint64_t LogBudget=12*1024*1024,FreeReserve=1024*1024,SegmentLimit=512*1024;
constexpr size_t SegmentCount=32;
constexpr size_t ReadChunkBytes=3072,ReadReplyBytes=(ReadChunkBytes+2)/3*4+256;
// Fixed-capacity encoding: no base64/string temporaries on the ESP's limited heap.
inline bool encodeReadReply(char* out,size_t capacity,const char* filename,uint64_t start,uint64_t next,uint64_t size,bool eof,const char* data,size_t count){
  Segment parsed;
  if(count>ReadChunkBytes||!Segment::parse(filename,parsed)||!capacity)return false;
  int prefix=snprintf(out,capacity,"{\"state\":\"ready\",\"file\":\"%s\",\"start\":%llu,\"next\":%llu,\"size\":%llu,\"eof\":%s,\"base64\":\"",filename,(unsigned long long)start,(unsigned long long)next,(unsigned long long)size,eof?"true":"false");
  const size_t encoded=(count+2)/3*4;
  if(prefix<0||size_t(prefix)+encoded+3>capacity)return false;
  size_t cursor=prefix;
  const char* alphabet="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  for(size_t i=0;i<count;i+=3){
    uint32_t value=uint32_t(uint8_t(data[i]))<<16;
    if(i+1<count)value|=uint32_t(uint8_t(data[i+1]))<<8;
    if(i+2<count)value|=uint8_t(data[i+2]);
    out[cursor++]=alphabet[value>>18];out[cursor++]=alphabet[(value>>12)&63];
    out[cursor++]=i+1<count?alphabet[(value>>6)&63]:'=';out[cursor++]=i+2<count?alphabet[value&63]:'=';
  }
  out[cursor++]='"';out[cursor++]='}';out[cursor]=0;return true;
}
inline uint64_t totalSize(const std::vector<Segment>& files){uint64_t sum=0;for(const auto& file:files)sum+=file.size;return sum;}
inline bool needsSpace(uint64_t bytes,uint64_t freeBytes,uint64_t logBytes,size_t count){
  return freeBytes<FreeReserve+bytes || logBytes+bytes>LogBudget || count>SegmentCount;
}
}
