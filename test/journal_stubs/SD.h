#pragma once
#include "Arduino.h"
#include "SPI.h"
#include <map>
constexpr unsigned CARD_NONE=0,FILE_WRITE=1;
extern std::map<std::string,std::string> fakeFiles;
extern unsigned fakeWrites;
class File {
  std::string path;
  bool valid=false,directory=false;
  size_t offset=0,index=0;
  std::vector<std::string> children;
public:
  File()=default;
  explicit File(const std::string& p,bool dir=false):path(p),valid(true),directory(dir){
    if(dir)for(const auto& entry:fakeFiles)if(entry.first.find(p+"/")==0)children.push_back(entry.first);
  }
  explicit operator bool()const{return valid;}
  bool isDirectory()const{return directory;}
  const char* name()const{return path.c_str();}
  File openNextFile(){return index<children.size()?File(children[index++]):File();}
  size_t size()const{return directory?0:fakeFiles.at(path).size();}
  bool seek(size_t n){offset=n;return n<=size();}
  size_t read(uint8_t* out,size_t count){auto& body=fakeFiles.at(path);auto n=std::min(count,body.size()-offset);memcpy(out,body.data()+offset,n);offset+=n;return n;}
  size_t write(const uint8_t* data,size_t count){fakeFiles[path].append(reinterpret_cast<const char*>(data),count);++fakeWrites;fakeMillis+=120;return count;}
  void flush(){}
  void close(){}
};
class FakeSD {
public:
  void end(){}
  bool begin(int,SPIClass&,unsigned,const char*,int,bool){return true;}
  unsigned cardType(){return 1;}
  bool exists(const char* path){return std::string(path)=="/esp-native"||fakeFiles.count(path);}
  bool mkdir(const char*){return true;}
  File open(const char* path,unsigned mode=0){
    if(std::string(path)=="/esp-native")return File(path,true);
    if(mode==FILE_WRITE){fakeFiles[path]="";return File(path);}
    return fakeFiles.count(path)?File(path):File();
  }
  bool remove(const char* path){return fakeFiles.erase(path)!=0;}
  bool rename(const char* oldPath,const char* newPath){if(!fakeFiles.count(oldPath)||fakeFiles.count(newPath))return false;fakeFiles[newPath]=fakeFiles[oldPath];fakeFiles.erase(oldPath);return true;}
  uint64_t totalBytes(){return 64*1024*1024;}
  uint64_t usedBytes(){uint64_t total=0;for(const auto& entry:fakeFiles)total+=entry.second.size();return total;}
};
extern FakeSD SD;
