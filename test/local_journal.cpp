#include "../src/storage/local_journal.h"
#include <cassert>
#include <iostream>
std::vector<FakeQueue*> fakeQueues;
std::vector<bool*> fakeLocks;
std::vector<void*> fakeAllocations;
uint32_t fakeMillis=0;
std::function<void()> onDelay;
void (*journalWorker)(void*)=nullptr;
void* journalArg=nullptr;
std::map<std::string,std::string> fakeFiles;
unsigned fakeWrites=0;
FakeSD SD;
int main(){
  LocalJournal journal;
  journal.begin("real-worker-test");
  assert(journalWorker);
  // Start with a substantial queue, then continuously enqueue and poll. A slow
  // FAT write is modelled above; the actual firmware task and API run here.
  for(unsigned i=0;i<320;++i)journal.append("[SSH][OUTPUT] initial="+std::to_string(i));
  unsigned ticks=0,readableWhilePending=0,readReplies=0,paginatedReplies=0;
  bool requestedRead=false;
  onDelay=[&](){
    ++ticks;
    for(unsigned i=0;i<4;++i)journal.append("[SSH][OUTPUT] live="+std::to_string(ticks)+"-"+std::to_string(i));
    const std::string status=journal.status().c_str();
    const bool older=ticks>=60&&ticks<110;
    const auto page=std::string(journal.filesPage(older?2:0).c_str());
    if(page.find("\"state\":\"ready\"")!=std::string::npos&&status.find("pending=0&")==std::string::npos)++readableWhilePending;
    if(older&&page.find("\"state\":\"ready\"")!=std::string::npos){
      assert(page.find("\"sequence\":1,")!=std::string::npos);
      assert(page.find("\"sequence\":2,")==std::string::npos);
      ++paginatedReplies;
    }
    if(!requestedRead&&fakeFiles.count("/esp-native/0000000001.json")){
      const std::string reply=journal.read("0000000001.json",0,0).c_str();
      assert(reply.find("pending")!=std::string::npos);requestedRead=true;
    }else if(requestedRead){
      const std::string reply=journal.read("",0,1).c_str();
      if(reply.find("\"state\":\"ready\"")!=std::string::npos)++readReplies;
    }
    if(ticks>150)throw StopJournal();
  };
  try{journalWorker(journalArg);}catch(const StopJournal&){}
  assert(readableWhilePending>40); // No dependency on a globally empty queue.
  assert(fakeWrites>1&&fakeWrites<30); // Polls cannot create one FAT file per line.
  assert(readReplies>0&&paginatedReplies>0);
  assert(std::string(journal.status().c_str()).find("dropped=0&")!=std::string::npos);
  assert(fakeFiles.size()>1);
  std::cout<<"Actual journal worker: continuous logging, committed listing, bounded batching and async read passed\n";
  for(auto p:fakeQueues)delete p;
  for(auto p:fakeLocks)delete p;
  for(auto p:fakeAllocations)free(p);
}
