#pragma once
#include "../storage/local_report.h"
#include "inference.h"
#include <deque>
#include <cstdio>
#include <functional>
#include <sstream>

// Device-local inference, invoked by the application loop after real management CLI reads.
// Predictions are evidence for display/archival, never capabilities to write CLI.
class DeviceAnalysis {
  std::deque<uint64_t> seen;
  std::string latest="{\"model\":\"tiny-log-v3-protocol\",\"label\":\"unknown\",\"score\":0,\"accepted\":false}";
  static uint64_t hash(const std::string& text){uint64_t h=14695981039346656037ULL;for(unsigned char c:text){h^=c;h*=1099511628211ULL;}return h;}
public:
  std::function<bool(const std::string&)> save;
  void reset(){seen.clear();latest="{\"model\":\"tiny-log-v3-protocol\",\"label\":\"unknown\",\"score\":0,\"accepted\":false}";}
  void observe(const std::string& output,uint64_t now,uint64_t cycle,const std::string& source="h3c_ssh"){
    std::istringstream input(output);std::string line;unsigned analysed=0;
    while(analysed<8&&std::getline(input,line)){
      // Comware log entries contain a severity/module marker. Headers and CLI
      // prompts are not switch events. The original response is still journaled.
      if(line.size()<10||line.find('/')==std::string::npos||line.find("display ")!=std::string::npos)continue;
      auto safe=LocalReport::safeText(line);const auto fingerprint=hash(safe);
      if(std::find(seen.begin(),seen.end(),fingerprint)!=seen.end())continue;
      const auto prediction=tinylog::predict(safe);char score[32];snprintf(score,sizeof(score),"%.6f",double(prediction.score));
      latest="{\"model\":"+journal::quote(tinylog::VERSION)+",\"label\":"+journal::quote(tinylog::LABELS[prediction.category])+",\"score\":"+score+",\"accepted\":"+(prediction.accepted?"true":"false")+",\"source\":"+journal::quote(source)+",\"cycle\":"+std::to_string(cycle)+",\"at_ms\":"+std::to_string(now)+"}";
      auto record=latest;record.pop_back();record+=",\"evidence\":";
      size_t length=std::min<size_t>(512,safe.size());
      while(length<safe.size()&&length&&(uint8_t(safe[length])&0xc0)==0x80)--length;
      auto evidence=journal::quote(safe.substr(0,length));
      while(record.size()+evidence.size()+1>1000&&length){
        --length;while(length&&(uint8_t(safe[length])&0xc0)==0x80)--length;
        evidence=journal::quote(safe.substr(0,length));
      }
      record+=evidence+"}";
      // Failed persistence is retried on the next real observation, not hidden.
      if(save&&save(record)){seen.push_back(fingerprint);if(seen.size()>128)seen.pop_front();}
      ++analysed;
    }
  }
  const std::string& json()const{return latest;}
};
