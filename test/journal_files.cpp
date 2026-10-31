#include "../src/journal_files.h"
#include <cassert>
#include <set>
int main(){
  using namespace journal;
  Segment parsed;
  for(auto bad:{"raw-0.jsonl","../../password.jsonl","/esp-journal/00000000000000000001-raw.jsonl","00000000000000000000-raw.jsonl","18446744073709551616-raw.jsonl","00000000000000000001-raw.jsonl/evil","00000000000000000001-other.jsonl"})assert(!Segment::parse(bad,parsed));
  for(unsigned kind=0;kind<3;++kind)for(uint64_t id:{uint64_t(1),uint64_t(400),UINT64_MAX}){
    Segment entry;entry.kind=kind;entry.id=id;assert(Segment::parse(entry.filename(),parsed));assert(parsed.id==id&&parsed.kind==kind);
  }
  assert(!needsSpace(2048,FreeReserve+2048,LogBudget-2048,32));
  assert(needsSpace(2048,FreeReserve+2047,0,1));assert(needsSpace(2048,FreeReserve+10000,LogBudget-2047,1));assert(needsSpace(1,FreeReserve+10000,0,33));
  // Recover creation order from filenames after a restart, then reclaim across all kinds.
  std::vector<Segment> files;
  for(unsigned i=30;i>0;--i){Segment s;s.id=i;s.kind=i%3;s.size=SegmentLimit;files.push_back(s);}
  std::sort(files.begin(),files.end(),[](const Segment& a,const Segment& b){return a.id<b.id;});
  uint64_t free=FreeReserve-100;std::set<uint64_t> removed;
  while(needsSpace(4096,free,totalSize(files),files.size())){assert(!files.empty());auto victim=files.front();removed.insert(victim.id);free+=victim.size;files.erase(files.begin());}
  assert(!removed.empty());assert(*removed.begin()==1);assert(*removed.rbegin()<files.front().id);assert(totalSize(files)+4096<=LogBudget);assert(free>=FreeReserve+4096);
  // If other card contents consume all space, no unrelated filename is a deletion candidate.
  assert(!Segment::parse("family-photo.jpg",parsed));files.clear();assert(needsSpace(4096,0,totalSize(files),0));
  // Regression for the real-device OOM: encode max chunks directly into a bounded buffer.
  char response[ReadReplyBytes];
  const char* filename="00000000000000000001-summary.jsonl";
  for(auto pair:std::vector<std::pair<std::string,std::string>>{{"",""},{"f","Zg=="},{"fo","Zm8="},{"foo","Zm9v"},{std::string("\xff\x00\xfe",3),"/wD+"}}){
    assert(encodeReadReply(response,sizeof(response),filename,0,pair.first.size(),123,false,pair.first.data(),pair.first.size()));
    assert(std::string(response).find("\"base64\":\""+pair.second+"\"")!=std::string::npos);
  }
  std::string chunk(ReadChunkBytes,'\xff');
  assert(encodeReadReply(response,sizeof(response),filename,UINT64_MAX,UINT64_MAX,UINT64_MAX,true,chunk.data(),chunk.size()));
  assert(std::string(response).size()<ReadReplyBytes);
  struct Guarded {char buffer[16];char guard='X';} small;
  assert(!encodeReadReply(small.buffer,sizeof(small.buffer),filename,0,1,1,true,"a",1));assert(small.guard=='X');
  assert(!encodeReadReply(response,sizeof(response),filename,0,0,0,true,chunk.data(),ReadChunkBytes+1));
  assert(!encodeReadReply(response,sizeof(response),"../../outside",0,0,0,true,"",0));
}
