#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <SD.h>
#include <atomic>
#include <ctime>
#include "fault_journal.h"
#include "journal_files.h"

// All filesystem access, including web reads and retention, belongs to this task.
class SdJournal {
  struct Record {uint16_t size;uint8_t kind;char data[2048];};
  enum State {Starting,Ready,NoCard,WriteError,Unavailable};
  SPIClass cardSpi{HSPI};
  QueueHandle_t queue=nullptr;
  SemaphoreHandle_t viewLock=nullptr;
  std::atomic<int> state{Starting};
  std::atomic<unsigned> dropped{0},written{0},evicted{0};
  std::string boot;
  Record scratch{},record{};
  char readBuffer[journal::ReadChunkBytes],readOutput[journal::ReadReplyBytes];
  std::vector<journal::Segment> files;
  uint64_t current[3]={0,0,0},sequence=0,freeBytes=0;
  bool boundary[3]={true,true,true};
  uint32_t spaceAt=0,listedAt=0;
  String listing="{\"state\":\"starting\",\"files\":[]}";
  String readReply;
  std::string readName;
  uint32_t readOffset=0,readId=0;
  bool readWaiting=false,readBusy=false;
  static String path(const std::string& name){return String("/esp-journal/")+name.c_str();}
  void setState(State next){
    if(state.exchange(next)!=next)Serial.printf("[STORAGE] state=%s dropped=%u\n",label(next),dropped.load());
  }
  static const char* label(int value){return value==Ready?"ready":value==NoCard?"no_card":value==WriteError?"write_error":value==Unavailable?"unavailable":"starting";}
  void snapshot(){
    std::string body="{\"state\":"+journal::quote(label(state.load()))+",\"free_bytes\":"+std::to_string(freeBytes)+",\"log_bytes\":"+std::to_string(journal::totalSize(files))+",\"budget_bytes\":"+std::to_string(journal::LogBudget)+",\"reserve_bytes\":"+std::to_string(journal::FreeReserve)+",\"evicted\":"+std::to_string(evicted.load())+",\"files\":[";
    bool first=true;
    for(auto it=files.rbegin();it!=files.rend();++it){
      if(!first)body+=",";first=false;
      body+="{\"name\":"+journal::quote(it->filename())+",\"kind\":"+journal::quote(it->kind==0?"raw":it->kind==1?"process":"summary")+",\"bytes\":"+std::to_string(it->size)+"}";
    }
    body+="]}";
    xSemaphoreTake(viewLock,portMAX_DELAY);listing=body.c_str();xSemaphoreGive(viewLock);listedAt=millis();
  }
  bool mount(){
    SD.end();files.clear();for(auto& id:current)id=0;
    if(!SD.begin(4,cardSpi,4000000,"/sd",5,false)||SD.cardType()==CARD_NONE){setState(NoCard);return false;}
    const bool fresh=!SD.exists("/esp-journal");
    if(fresh&&!SD.mkdir("/esp-journal")){setState(WriteError);return false;}
    File directory=SD.open("/esp-journal");const bool valid=directory&&directory.isDirectory();directory.close();
    if(!valid){setState(WriteError);return false;}
    const char* markerPath="/esp-journal/format.txt";
    const char* signature="esp-fault-journal-v1\n";
    if(fresh){
      File marker=SD.open(markerPath,FILE_WRITE);
      bool ok=marker&&marker.print(signature)==strlen(signature);marker.flush();marker.close();
      if(!ok){setState(WriteError);return false;}
    } else {
      File marker=SD.open(markerPath);const bool owned=marker&&marker.size()==strlen(signature)&&marker.readString()==signature;marker.close();
      if(!owned){setState(WriteError);return false;}
    }
    directory=SD.open("/esp-journal");
    for(File entry=directory.openNextFile();entry;entry=directory.openNextFile()){
      journal::Segment segment;
      const bool accepted=!entry.isDirectory()&&journal::Segment::parse(entry.name(),segment);
      if(accepted){segment.size=entry.size();files.push_back(segment);sequence=std::max(sequence,segment.id);current[segment.kind]=std::max(current[segment.kind],segment.id);}
      entry.close();
      if(files.size()>64){directory.close();setState(WriteError);return false;}
    }
    directory.close();
    std::sort(files.begin(),files.end(),[](const journal::Segment& a,const journal::Segment& b){return a.id<b.id;});
    for(auto& value:boundary)value=true;
    if(!space()){setState(WriteError);return false;}
    setState(Ready);snapshot();return true;
  }
  bool space(){
    const uint64_t total=SD.totalBytes(),used=SD.usedBytes();
    if(!total||used>total)return false;
    freeBytes=total-used;spaceAt=millis();return true;
  }
  bool removeOldest(){
    if(files.empty())return false;
    const auto victim=files.front();
    if(!SD.remove(path(victim.filename()).c_str()))return false;
    if(current[victim.kind]==victim.id)current[victim.kind]=0;
    files.erase(files.begin());++evicted;return space();
  }
  bool ensureSpace(size_t bytes){
    if(uint32_t(millis()-spaceAt)>=10000&&!space())return false;
    // Account conservatively for FAT cluster allocation; confirm actual free space before pruning.
    const uint64_t allocation=bytes+8192;
    if(journal::needsSpace(allocation,freeBytes,journal::totalSize(files),files.size())&&!space())return false;
    while(journal::needsSpace(allocation,freeBytes,journal::totalSize(files),files.size()))if(!removeOldest())return false;
    return true;
  }
  bool append(unsigned kind,const char* data,size_t size){
    if(!ensureSpace(size+2))return false;
    auto it=std::find_if(files.begin(),files.end(),[&](const journal::Segment& s){return s.id==current[kind]&&s.kind==kind;});
    if(it!=files.end() && it->size+size+2>journal::SegmentLimit){current[kind]=0;it=files.end();}
    if(it==files.end()){
      // Keep the file count bounded even for small fragments across many resets.
      while(files.size()>=journal::SegmentCount)if(!removeOldest())return false;
      if(sequence==UINT64_MAX)return false;
      journal::Segment next;next.id=++sequence;next.kind=kind;
      files.push_back(next);current[kind]=next.id;it=files.end()-1;boundary[kind]=false;
    }
    File file=SD.open(path(it->filename()).c_str(),FILE_APPEND);if(!file)return false;
    bool ok=true;
    if(boundary[kind]&&file.size())ok=file.write(uint8_t('\n'))==1;
    boundary[kind]=false;
    ok=ok&&file.write(reinterpret_cast<const uint8_t*>(data),size)==size&&file.write(uint8_t('\n'))==1;
    file.flush();it->size=file.size();file.close();
    freeBytes=freeBytes>size+8192?freeBytes-size-8192:0;
    return ok;
  }
  void serveRead(bool ready){
    std::string filename;uint32_t offset=0,id=0;
    xSemaphoreTake(viewLock,portMAX_DELAY);
    if(readWaiting){filename=readName;offset=readOffset;id=readId;readWaiting=false;readBusy=true;}
    xSemaphoreGive(viewLock);
    if(!id)return;
    strcpy(readOutput,"{\"state\":\"error\",\"message\":\"卡不可用或记录已循环覆盖，请刷新文件列表。\"}");
    auto it=std::find_if(files.begin(),files.end(),[&](const journal::Segment& s){return s.filename()==filename;});
    if(ready && it!=files.end()){
      File file=SD.open(path(filename).c_str());
      if(file && !file.isDirectory() && offset<=file.size()){
        size_t start=offset;
        file.seek(offset?offset-1:0);
        // Start at a whole line. Normal records are bounded to 2048 bytes.
        if(offset && file.read()!='\n'){
          size_t skipped=0;
          while(file.available()&&skipped++<sizeof(readBuffer)&&file.read()!='\n'){}
          if(skipped>sizeof(readBuffer)){
            file.close();
            xSemaphoreTake(viewLock,portMAX_DELAY);readReply="{\"state\":\"error\",\"message\":\"记录行异常过长，请将卡断电取出检查。\"}";readBusy=false;xSemaphoreGive(viewLock);return;
          }
        }
        start=file.position();
        size_t count=file.read(reinterpret_cast<uint8_t*>(readBuffer),sizeof(readBuffer));
        const bool eof=file.position()>=file.size();
        if(!eof){size_t end=count;while(end&&readBuffer[end-1]!='\n')--end;if(end)count=end;}
        if(!journal::encodeReadReply(readOutput,sizeof(readOutput),filename.c_str(),start,start+count,file.size(),eof,readBuffer,count))
          strcpy(readOutput,"{\"state\":\"error\",\"message\":\"记录读取超出缓冲区，请刷新重试。\"}");
      }
      file.close();
    }
    xSemaphoreTake(viewLock,portMAX_DELAY);if(id==readId){readReply=readOutput;readBusy=false;}xSemaphoreGive(viewLock);
  }
  void run(){
    if(!cardSpi.begin(7,5,6,4)){setState(Unavailable);vTaskDelete(nullptr);return;}
    bool ready=false;uint32_t retryAt=0;unsigned reportedDrops=0;
    for(;;){
      if(!ready && int32_t(millis()-retryAt)>=0){ready=mount();retryAt=millis()+30000;snapshot();}
      serveRead(ready);
      if(!ready){vTaskDelay(pdMS_TO_TICKS(50));continue;}
      const auto losses=dropped.load();
      if(losses!=reportedDrops){
        std::string gap="{\"boot_id\":"+journal::quote(boot)+",\"event\":\"RECORD_GAP\",\"dropped_total\":"+std::to_string(losses)+",\"message\":\"队列满、记录过长或写入失败，部分记录未保存。\"}";
        if(!append(2,gap.data(),gap.size())){setState(WriteError);ready=false;snapshot();continue;}
        reportedDrops=losses;
      }
      if(xQueueReceive(queue,&record,pdMS_TO_TICKS(50))==pdTRUE){
        if(!append(record.kind,record.data,record.size)){++dropped;setState(WriteError);ready=false;snapshot();}
        else ++written;
      }
      if(uint32_t(millis()-listedAt)>=2000)snapshot();
    }
  }
public:
  journal::FaultJournal core;
  void begin(const std::string& id){
    boot=id;queue=xQueueCreate(12,sizeof(Record));viewLock=xSemaphoreCreateMutex();
    if(!queue||!viewLock||!readReply.reserve(journal::ReadReplyBytes)){setState(Unavailable);return;}
    core.sink=[this](journal::Kind kind,const std::string& fields){
      const auto epoch=std::time(nullptr);
      std::string line="{\"boot_id\":"+journal::quote(boot)+",\"uptime_ms\":"+std::to_string(core.uptime())+",\"unix_ms\":"+(epoch>=1704067200?std::to_string(uint64_t(epoch)*1000):"null")+",\"source\":\"esp_observation\","+fields+"}";
      if(line.size()>sizeof(scratch.data)){++dropped;return;}
      scratch.size=line.size();scratch.kind=static_cast<uint8_t>(kind);memcpy(scratch.data,line.data(),line.size());
      if(xQueueSend(queue,&scratch,0)!=pdTRUE)++dropped;
    };
    if(xTaskCreate([](void* self){static_cast<SdJournal*>(self)->run();},"tf-journal",8192,this,1,nullptr)!=pdPASS){
      core.sink=nullptr;vQueueDelete(queue);queue=nullptr;setState(Unavailable);return;
    }
    core.clock(millis());
    core.sink(journal::Kind::Summary,"\"event\":\"BOOT\",\"input_source\":\"usb_gateway\",\"message\":\"设备启动；前一开机周期未结束的事件恢复情况未知。USB 来源可能为模拟数据，未独立验证。\"");
  }
  String list(){
    if(!viewLock||xSemaphoreTake(viewLock,0)!=pdTRUE)return "{\"state\":\"busy\",\"files\":[]}";
    String result=listing;xSemaphoreGive(viewLock);return result;
  }
  String read(const String& filename,uint32_t offset,uint32_t job){
    if(!viewLock)return "{\"state\":\"error\",\"message\":\"记录任务不可用\"}";
    if(xSemaphoreTake(viewLock,0)!=pdTRUE)return "{\"state\":\"busy\"}";
    String result;
    if(job){result=job==readId?readReply:String("{\"state\":\"expired\"}");}
    else if(readWaiting||readBusy)result="{\"state\":\"busy\"}";
    else {
      journal::Segment parsed;
      if(!journal::Segment::parse(filename.c_str(),parsed))result="{\"state\":\"error\",\"message\":\"文件名无效\"}";
      else {readName=filename.c_str();readOffset=offset;if(!++readId)++readId;readWaiting=true;readReply="{\"state\":\"pending\",\"job\":";readReply+=readId;readReply+="}";result=readReply;}
    }
    xSemaphoreGive(viewLock);return result;
  }
  String status() const {
    return String("state=")+label(state.load())+"&written="+written.load()+"&dropped="+dropped.load()+"&summary_incomplete="+(core.incomplete?"1":"0")+"&directory=esp-journal&budget_mib=12&reserve_mib=1&file_limit_kib=512&evicted="+evicted.load();
  }
  const char* stateName() const{return label(state.load());}
};
