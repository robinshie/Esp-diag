#pragma once
#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include "local_report.h"
#include "journal_files.h"
#include "../net/mqtt_report_cursor.h"

// A single task owns the TF filesystem. Producers submit bounded records only.
// Native records live separately from historical host/database copies.
class LocalJournal {
  struct Record {uint64_t at;bool analysis;char text[1025];};
  struct FileInfo {uint32_t sequence;size_t bytes;std::string name;};
  // One real SSH round can publish >200 lines in bursts (52 interfaces,
  // down causes and the bounded logbuffer). Keep this queue in PSRAM.
  static constexpr unsigned Capacity=384;
  Record* records=nullptr;
  QueueHandle_t freeSlots=nullptr,queue=nullptr;
  SemaphoreHandle_t lock=nullptr;
  SPIClass spi{HSPI};
  std::atomic<const char*> state{"starting"};
  std::atomic<unsigned> dropped{0},written{0};
  std::atomic<unsigned> pendingRecords{0};
  std::vector<FileInfo> files;
  uint32_t sequence=0,before=0,listedBefore=UINT32_MAX,readId=0,readOffset=0;
  uint64_t totalBytes=0,freeBytes=0;
  std::string boot,listing="{\"state\":\"starting\",\"files\":[]}",readName,readReply;
  bool readWaiting=false,readBusy=false;
  std::atomic<bool> flushRequested{false};
  // TF owns cursor IO. Arduino loop reads one immutable report at a time.
  std::string mqttWantedScope,mqttScope,mqttBody;
  bool mqttChanged=false;
  uint32_t mqttCursor=0,mqttSequence=0,mqttGeneration=1;
  std::atomic<uint32_t> mqttAck{0},mqttGaps{0},mqttAcked{0};
  char readBuffer[journal::ReadChunkBytes],readOutput[journal::ReadReplyBytes];
  static constexpr uint64_t Budget=12*1024*1024,Reserve=1024*1024;
  static std::string name(uint32_t seq){char out[32];snprintf(out,sizeof(out),"%010lu.json",(unsigned long)seq);return out;}
  static bool parse(const std::string& text,uint32_t& seq){
    if(text.size()!=15||text.substr(10)!=".json")return false;
    uint64_t value=0;for(unsigned i=0;i<10;++i){if(text[i]<'0'||text[i]>'9')return false;value=value*10+text[i]-'0';}
    if(!value||value>=UINT32_MAX)return false;
    seq=value;return true;
  }
  static String path(const std::string& file){return String("/esp-native/")+file.c_str();}
  void snapshot(){
    uint32_t cursor;xSemaphoreTake(lock,portMAX_DELAY);cursor=before;xSemaphoreGive(lock);
    std::string result="{\"state\":"+journal::quote(state.load())+",\"source\":\"esp_native\",\"count\":"+std::to_string(files.size())+",\"bytes\":"+std::to_string(totalBytes)+",\"files\":[";
    unsigned count=0;bool more=false;
    for(auto it=files.rbegin();it!=files.rend();++it){
      if(cursor&&it->sequence>=cursor)continue;
      if(count==32){more=true;break;}
      if(count++)result+=',';
      result+="{\"name\":"+journal::quote(it->name)+",\"sequence\":"+std::to_string(it->sequence)+",\"bytes\":"+std::to_string(it->bytes)+"}";
    }
    result+=std::string("],\"has_older\":")+(more?"true":"false")+"}";
    xSemaphoreTake(lock,portMAX_DELAY);listing=std::move(result);listedBefore=cursor;xSemaphoreGive(lock);
  }
  bool mount(){
    SD.end();files.clear();totalBytes=0;sequence=0;
    if(!SD.begin(4,spi,4000000,"/sd",5,false)||SD.cardType()==CARD_NONE){state="no_card";return false;}
    if(!SD.exists("/esp-native")&&!SD.mkdir("/esp-native")){state="write_error";return false;}
    File directory=SD.open("/esp-native");if(!directory||!directory.isDirectory()){directory.close();state="write_error";return false;}
    for(File entry=directory.openNextFile();entry;entry=directory.openNextFile()){
      uint32_t seq;std::string file=entry.name();auto slash=file.find_last_of('/');if(slash!=std::string::npos)file=file.substr(slash+1);
      if(!entry.isDirectory()&&parse(file,seq)){files.push_back({seq,entry.size(),file});totalBytes+=entry.size();sequence=std::max(sequence,seq);}
      entry.close();vTaskDelay(1);
    }directory.close();std::sort(files.begin(),files.end(),[](const FileInfo& a,const FileInfo& b){return a.sequence<b.sequence;});
    const auto total=SD.totalBytes(),used=SD.usedBytes();if(!total||used>total){state="write_error";return false;}
    freeBytes=total-used;state="ready";
    xSemaphoreTake(lock,portMAX_DELAY);mqttChanged=true;if(!++mqttGeneration)++mqttGeneration;xSemaphoreGive(lock);return true;
  }
  bool save(LocalReport& report){
    if(report.empty())return true;
    if(sequence>=UINT32_MAX-1)return false;
    auto body=report.json(boot,uint64_t(sequence)+1);
    while(totalBytes+body.size()>Budget||files.size()>=256||freeBytes<Reserve+body.size()+8192){
      if(files.empty()||!SD.remove(path(files.front().name).c_str()))return false;
      totalBytes-=files.front().bytes;files.erase(files.begin());const auto total=SD.totalBytes(),used=SD.usedBytes();if(used>total)return false;freeBytes=total-used;
    }
    const auto next=sequence+1;auto fileName=name(next);SD.remove("/esp-native/pending.part");
    File file=SD.open("/esp-native/pending.part",FILE_WRITE);bool ok=file&&file.write(reinterpret_cast<const uint8_t*>(body.data()),body.size())==body.size();file.flush();file.close();
    if(!ok||!SD.rename("/esp-native/pending.part",path(fileName).c_str()))return false;
    files.push_back({next,body.size(),fileName});sequence=next;totalBytes+=body.size();freeBytes=freeBytes>body.size()+8192?freeBytes-body.size()-8192:0;
    written+=report.count();pendingRecords-=report.count();report.clear();snapshot();return true;
  }
  void serveRead(){
    std::string fileName;uint32_t offset=0,id=0;
    xSemaphoreTake(lock,portMAX_DELAY);if(readWaiting){fileName=readName;offset=readOffset;id=readId;readWaiting=false;readBusy=true;}xSemaphoreGive(lock);
    if(!id)return;
    strcpy(readOutput,"{\"state\":\"error\",\"message\":\"记录暂不可用或已被循环保留覆盖，请刷新。\"}");
    uint32_t seq;
    if(std::string(state.load())=="ready"&&parse(fileName,seq)){
      File file=SD.open(path(fileName).c_str());
      if(file&&!file.isDirectory()&&offset<=file.size()){
        file.seek(offset);size_t n=file.read(reinterpret_cast<uint8_t*>(readBuffer),sizeof(readBuffer));auto size=file.size();
        journal::encodeChunkReply(readOutput,sizeof(readOutput),fileName.c_str(),offset,offset+n,size,offset+n>=size,readBuffer,n);
      }file.close();
    }
    xSemaphoreTake(lock,portMAX_DELAY);if(readId==id){readReply=readOutput;readBusy=false;}xSemaphoreGive(lock);
  }
  bool saveMqttCursor(uint32_t cursor){
    auto body=mqtt_report::encodeCursor(mqttScope,cursor);
    SD.remove("/esp-native/mqtt-cursor.part");File file=SD.open("/esp-native/mqtt-cursor.part",FILE_WRITE);
    bool ok=file&&file.write(reinterpret_cast<const uint8_t*>(body.data()),body.size())==body.size();file.flush();file.close();
    if(!ok)return false;
    // FAT rename cannot replace an existing path. Power loss in this window
    // replays retained reports; it never skips an unacknowledged report.
    SD.remove("/esp-native/mqtt.cursor");
    return SD.rename("/esp-native/mqtt-cursor.part","/esp-native/mqtt.cursor");
  }
  void serveMqtt(){
    bool changed=false;std::string nextScope;
    xSemaphoreTake(lock,portMAX_DELAY);if(mqttChanged){changed=true;nextScope=mqttWantedScope;mqttChanged=false;mqttBody.clear();mqttSequence=0;}xSemaphoreGive(lock);
    if(changed){
      mqttScope=nextScope;mqttCursor=0;mqttAck=0;
      if(!mqttScope.empty()){
        File file=SD.open("/esp-native/mqtt.cursor");
        if(file&&file.size()>0&&file.size()<=256){std::string raw;raw.resize(file.size());if(file.read(reinterpret_cast<uint8_t*>(&raw[0]),raw.size())==raw.size())mqtt_report::decodeCursor(raw,mqttScope,mqttCursor);}file.close();
        if(mqttCursor>sequence)mqttCursor=0;
      }
    }
    if(mqttScope.empty())return;
    const auto ack=mqttAck.load();
    if(ack){
      if(!saveMqttCursor(ack))return;
      mqttCursor=ack;mqttAcked=ack;mqttAck=0;
      xSemaphoreTake(lock,portMAX_DELAY);mqttBody.clear();mqttSequence=0;xSemaphoreGive(lock);
    }
    xSemaphoreTake(lock,portMAX_DELAY);const bool occupied=mqttSequence!=0;xSemaphoreGive(lock);if(occupied)return;
    auto item=std::find_if(files.begin(),files.end(),[this](const FileInfo& f){return f.sequence>mqttCursor;});
    if(item==files.end())return;
    if(item->sequence>mqttCursor+1){
      mqttGaps+=item->sequence-mqttCursor-1;mqttCursor=item->sequence-1;
      append("[MQTT] state=archive_gap reason=retention_overwrite lost_reports="+std::to_string(mqttGaps.load()));
    }
    if(item->bytes>LocalReport::MaxWireBytes){
      ++mqttGaps;mqttCursor=item->sequence;saveMqttCursor(mqttCursor);
      append("[MQTT] state=archive_gap reason=legacy_report_too_large");return;
    }
    File file=SD.open(path(item->name).c_str());if(!file||!item->bytes||file.size()!=item->bytes){file.close();return;}
    std::string body;body.resize(item->bytes);const auto read=file.read(reinterpret_cast<uint8_t*>(&body[0]),body.size());file.close();
    if(read!=body.size())return;
    xSemaphoreTake(lock,portMAX_DELAY);if(!mqttChanged){mqttBody=std::move(body);mqttSequence=item->sequence;}xSemaphoreGive(lock);
  }
  void run(){
    if(!spi.begin(7,5,6,4)){state="unavailable";vTaskDelete(nullptr);return;}
    LocalReport report;bool ready=false;uint32_t retry=0,flushed=millis(),listed=0;unsigned losses=0;
    for(;;){
      if(!ready&&int32_t(millis()-retry)>=0){ready=mount();retry=millis()+30000;snapshot();}
      // Keep queued evidence intact while the card is unavailable.
      // Drain a bounded batch before a requested flush. A viewer poll must not
      // turn every queued line into its own FAT create/flush/rename transaction.
      uint16_t index;
      for(unsigned batch=0;ready&&batch<32&&xQueueReceive(queue,&index,0)==pdTRUE;++batch){
        auto add=[&](){return records[index].analysis?report.addAnalysis(records[index].at,records[index].text):report.add(records[index].at,records[index].text);};
        if(!add()){
          if(!save(report)||!add()){++dropped;--pendingRecords;ready=false;state="write_error";}
        }
        xQueueSend(freeSlots,&index,0);
      }
      if(ready&&losses!=dropped.load()){
        auto text="[STORAGE] state=record_gap dropped="+std::to_string(dropped.load());
        if(report.add(uint64_t(esp_timer_get_time())/1000,text)){++pendingRecords;losses=dropped.load();}
      }
      const auto elapsed=uint32_t(millis()-flushed);
      if(ready&&((elapsed>=1000&&flushRequested.exchange(false))||elapsed>=30000)){
        if(!save(report)){ready=false;state="write_error";}flushed=millis();
      }
      serveRead();
      if(ready)serveMqtt();
      if(uint32_t(millis()-listed)>=500){snapshot();listed=millis();}
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
public:
  void begin(const std::string& id){
    boot=id;records=static_cast<Record*>(heap_caps_calloc(Capacity,sizeof(Record),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
    freeSlots=xQueueCreate(Capacity,sizeof(uint16_t));queue=xQueueCreate(Capacity,sizeof(uint16_t));lock=xSemaphoreCreateMutex();
    if(!records||!freeSlots||!queue||!lock){state="unavailable";return;}
    for(uint16_t i=0;i<Capacity;++i)xQueueSend(freeSlots,&i,0);
    if(xTaskCreate([](void* self){static_cast<LocalJournal*>(self)->run();},"native-journal",8192,this,1,nullptr)!=pdPASS)state="unavailable";
  }
  void append(const std::string& line){
    if(!records||!freeSlots||!queue){++dropped;return;}
    uint16_t index;if(xQueueReceive(freeSlots,&index,0)!=pdTRUE){++dropped;return;}
    auto safe=LocalReport::safeText(line);
    records[index].at=uint64_t(esp_timer_get_time())/1000;records[index].analysis=false;memcpy(records[index].text,safe.data(),safe.size());records[index].text[safe.size()]=0;
    ++pendingRecords;
    if(xQueueSend(queue,&index,0)!=pdTRUE){--pendingRecords;xQueueSend(freeSlots,&index,0);++dropped;}
  }
  bool appendAnalysis(const std::string& raw){
    if(raw.empty()||raw.size()>1000||raw.front()!='{'||raw.back()!='}'||!records||!freeSlots||!queue){++dropped;return false;}
    uint16_t index;if(xQueueReceive(freeSlots,&index,0)!=pdTRUE){++dropped;return false;}
    records[index].at=uint64_t(esp_timer_get_time())/1000;records[index].analysis=true;
    memcpy(records[index].text,raw.data(),raw.size());records[index].text[raw.size()]=0;++pendingRecords;
    if(xQueueSend(queue,&index,0)!=pdTRUE){--pendingRecords;xQueueSend(freeSlots,&index,0);++dropped;return false;}return true;
  }
  void configureMqtt(const std::string& scope){
    if(!lock)return;
    xSemaphoreTake(lock,portMAX_DELAY);
    if(scope!=mqttWantedScope){mqttWantedScope=scope;mqttChanged=true;mqttBody.clear();mqttSequence=0;mqttAck=0;if(!++mqttGeneration)++mqttGeneration;}
    xSemaphoreGive(lock);
  }
  bool nextMqttReport(std::string& body,uint32_t& seq,uint32_t& generation){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return false;
    bool ready=!mqttChanged&&mqttSequence&&!mqttAck.load();if(ready){body=mqttBody;seq=mqttSequence;generation=mqttGeneration;}xSemaphoreGive(lock);return ready;
  }
  void acknowledgeMqtt(uint32_t seq,uint32_t generation){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return;
    if(!mqttChanged&&generation==mqttGeneration&&seq==mqttSequence)mqttAck=seq;
    xSemaphoreGive(lock);
  }
  uint32_t mqttGapCount() const{return mqttGaps.load();}
  void flush(){flushRequested=true;}
  String filesPage(uint32_t cursor=0){
    flush();if(std::string(state.load())!="ready")return String("{\"state\":\"")+state.load()+"\",\"files\":[]}";
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return "{\"state\":\"busy\"}";
    // Published files are immutable and atomically renamed. Pending producers
    // do not invalidate that committed snapshot; requiring an empty queue here
    // starves readers whenever SSH/logging continues.
    before=cursor;String result=listedBefore==cursor?String(listing.c_str()):String("{\"state\":\"busy\"}");xSemaphoreGive(lock);return result;
  }
  String read(const String& filename,uint32_t offset,uint32_t job){
    if(!lock||xSemaphoreTake(lock,0)!=pdTRUE)return "{\"state\":\"busy\"}";
    String result;uint32_t seq;std::string file=filename.c_str();
    if(job)result=job==readId?String(readReply.c_str()):String("{\"state\":\"expired\"}");
    else if(!parse(file,seq))result="{\"state\":\"error\",\"message\":\"文件名无效\"}";
    else if(readBusy||readWaiting)result="{\"state\":\"busy\"}";
    else {readName=file;readOffset=offset;if(!++readId)++readId;readWaiting=true;readReply="{\"state\":\"pending\",\"job\":"+std::to_string(readId)+"}";result=readReply.c_str();}
    xSemaphoreGive(lock);return result;
  }
  String status() const{return String("state=")+state.load()+"&source=esp_native&written="+written.load()+"&pending="+pendingRecords.load()+"&dropped="+dropped.load()+"&mqtt_ack_sequence="+mqttAcked.load()+"&mqtt_gaps="+mqttGaps.load()+"&directory=esp-native&budget_mib=12&reserve_mib=1";}
  const char* stateName() const{return state.load();}
};
