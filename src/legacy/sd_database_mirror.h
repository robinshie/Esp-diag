#pragma once
#include <Arduino.h>
#include <SD.h>
#include <esp_system.h>
#include <mbedtls/sha256.h>
#include "archive_frame.h"
#include "../storage/journal_files.h"
#include "fault_journal.h"

// Called only by the existing SD task. No filesystem operation runs in the BDI loop.
class SdDatabaseMirror {
  static constexpr uint64_t Budget=12*1024*1024,Reserve=1024*1024;
  File pending;
  mbedtls_sha256_context hash;
  std::string card,key,target;
  uint32_t expected=0,position=0;
  uint64_t bytes=0,count=0;
  bool ready=false;
  std::vector<archive::FileInfo> recent;
  static String path(const std::string& name){return String("/esp-db-mirror/")+name.c_str();}
  void scan(){
    bytes=count=0;recent.clear();File directory=SD.open("/esp-db-mirror");
    for(File entry=directory.openNextFile();entry;entry=directory.openNextFile()){
      archive::FileInfo info;
      if(!entry.isDirectory()&&archive::FileInfo::parse(entry.name(),info)){
        info.size=entry.size();bytes+=info.size;++count;recent.push_back(info);
        std::sort(recent.begin(),recent.end(),[](const archive::FileInfo& a,const archive::FileInfo& b){return a.sequence>b.sequence||(a.sequence==b.sequence&&a.key>b.key);});
        if(recent.size()>32)recent.pop_back();
      }
      entry.close();vTaskDelay(1); // Let the idle task run during large directory scans.
    }directory.close();
  }
  bool makeSpace(uint32_t incoming){
    uint64_t total=SD.totalBytes(),used=SD.usedBytes();if(!total||used>total)return false;
    while(bytes+incoming>Budget||count>=4096||total-used<Reserve+incoming+8192){
      archive::FileInfo oldest;bool found=false;File directory=SD.open("/esp-db-mirror");
      for(File entry=directory.openNextFile();entry;entry=directory.openNextFile()){
        archive::FileInfo item;
        if(!entry.isDirectory()&&archive::FileInfo::parse(entry.name(),item)&&(!found||item.sequence<oldest.sequence||(item.sequence==oldest.sequence&&item.key<oldest.key))){item.size=entry.size();oldest=item;found=true;}
        entry.close();vTaskDelay(1); // Let the idle task run during large directory scans.
      }directory.close();
      if(!found||!SD.remove(path(oldest.name()).c_str()))return false;
      recent.erase(std::remove_if(recent.begin(),recent.end(),[&](const archive::FileInfo& file){return file.name()==oldest.name();}),recent.end());
      bytes=bytes>oldest.size?bytes-oldest.size:0;if(count)--count;used=SD.usedBytes();
    }return true;
  }
  static std::string digest(mbedtls_sha256_context& context){
    uint8_t output[32];if(mbedtls_sha256_finish(&context,output))return "";
    char text[65];for(unsigned i=0;i<32;++i)snprintf(text+2*i,3,"%02x",output[i]);return text;
  }
  bool matches(const std::string& filename,uint32_t size,const std::string& wanted){
    File file=SD.open(path(filename).c_str());if(!file||file.size()!=size){file.close();return false;}
    mbedtls_sha256_context check;mbedtls_sha256_init(&check);mbedtls_sha256_starts(&check,0);
    uint8_t buffer[256];while(file.available()){auto n=file.read(buffer,sizeof(buffer));if(!n){file.close();mbedtls_sha256_free(&check);return false;}mbedtls_sha256_update(&check,buffer,n);}
    file.close();auto actual=digest(check);mbedtls_sha256_free(&check);return actual==wanted;
  }
public:
  SdDatabaseMirror(){mbedtls_sha256_init(&hash);}
  bool mount(){
    ready=false;pending.close();key.clear();
    bool fresh=!SD.exists("/esp-db-mirror");if(fresh&&!SD.mkdir("/esp-db-mirror"))return false;
    const char* marker="/esp-db-mirror/identity.txt";
    if(fresh){
      char id[33];snprintf(id,sizeof(id),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
      File file=SD.open(marker,FILE_WRITE);bool ok=file&&file.print(id)==32&&file.write('\n')==1;file.flush();file.close();if(!ok)return false;
    }
    File file=SD.open(marker);String id=file?file.readString():String();file.close();id.trim();card=id.c_str();if(!archive::hex(card,32))return false;
    scan();ready=true;return true;
  }
  void unavailable(){ready=false;pending.close();key.clear();}
  std::string reply(const archive::Frame& frame,const char* state){
    return std::string("[SDCOPY] req=")+frame.req+" key="+frame.key+" state="+state+" next="+std::to_string(position)+" card="+(card.empty()?"none":card);
  }
  std::string handle(const archive::Frame& frame){
    if(!ready)return reply(frame,"no_card");
    if(frame.op=='S')return reply(frame,"ready");
    if(frame.op=='B'){
      pending.close();archive::FileInfo info;info.sequence=frame.sequence;info.key=frame.key;target=info.name();key=frame.key;position=0;expected=frame.size;
      if(matches(target,expected,key))return reply(frame,"committed");
      if(SD.exists(path(target).c_str())){SD.remove(path(target).c_str());scan();}
      SD.remove("/esp-db-mirror/pending.part");
      if(!makeSpace(expected))return reply(frame,"space_error");
      pending=SD.open("/esp-db-mirror/pending.part",FILE_WRITE);
      if(!pending)return reply(frame,"write_error");
      mbedtls_sha256_starts(&hash,0);return reply(frame,"ready");
    }
    if(!pending||key!=frame.key)return reply(frame,"restart_required");
    if(frame.op=='D'){
      if(frame.offset!=position||position+frame.count>expected)return reply(frame,"offset_error");
      if(pending.write(frame.data,frame.count)!=frame.count||mbedtls_sha256_update(&hash,frame.data,frame.count)){pending.close();return reply(frame,"write_error");}
      position+=frame.count;return reply(frame,"ready");
    }
    if(frame.op=='E'){
      pending.flush();pending.close();
      if(position!=expected||digest(hash)!=key)return reply(frame,"digest_error");
      if(!matches("pending.part",expected,key))return reply(frame,"digest_error");
      if(!SD.rename("/esp-db-mirror/pending.part",path(target).c_str()))return reply(frame,"write_error");
      archive::FileInfo info;archive::FileInfo::parse(target,info);info.size=expected;bytes+=expected;++count;recent.push_back(info);
      std::sort(recent.begin(),recent.end(),[](const archive::FileInfo& a,const archive::FileInfo& b){return a.sequence>b.sequence;});if(recent.size()>32)recent.pop_back();
      return reply(frame,"committed");
    }return reply(frame,"invalid");
  }
  String listing(uint32_t before=0) const {
    if(!ready)return "{\"state\":\"unavailable\",\"files\":[]}";
    std::vector<archive::FileInfo> page=recent;bool more=count>page.size();
    if(before){
      page.clear();uint64_t eligible=0;File directory=SD.open("/esp-db-mirror");
      for(File entry=directory.openNextFile();entry;entry=directory.openNextFile()){
        archive::FileInfo info;if(!entry.isDirectory()&&archive::FileInfo::parse(entry.name(),info)&&info.sequence<before){
          ++eligible;info.size=entry.size();page.push_back(info);std::sort(page.begin(),page.end(),[](const archive::FileInfo& a,const archive::FileInfo& b){return a.sequence>b.sequence;});if(page.size()>32)page.pop_back();
        }entry.close();vTaskDelay(1); // Let the idle task run during large directory scans.
      }directory.close();more=eligible>page.size();
    }
    std::string out="{\"state\":"+journal::quote(ready?"ready":"unavailable")+",\"source\":\"database\",\"card_id\":"+journal::quote(card)+",\"count\":"+std::to_string(count)+",\"bytes\":"+std::to_string(bytes)+",\"files\":[";
    bool first=true;for(const auto& file:page){if(!first)out+=",";first=false;out+="{\"name\":"+journal::quote(file.name())+",\"sequence\":"+std::to_string(file.sequence)+",\"bytes\":"+std::to_string(file.size)+"}";}out+=std::string("],\"has_older\":")+(more?"true":"false")+"}";return out.c_str();
  }
  bool read(const std::string& name,uint32_t offset,char* buffer,size_t capacity,char* output,size_t outCapacity){
    archive::FileInfo info;if(!ready||!archive::FileInfo::parse(name,info))return false;
    File file=SD.open(path(name).c_str());if(!file||offset>file.size()){file.close();return false;}
    file.seek(offset);size_t n=file.read(reinterpret_cast<uint8_t*>(buffer),capacity);uint32_t size=file.size();file.close();
    // The common encoder only receives a generated, validated filename.
    return journal::encodeChunkReply(output,outCapacity,name.c_str(),offset,offset+n,size,offset+n>=size,buffer,n);
  }
};
