#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include "artifacts/model.h"

namespace tinylog {
struct Prediction {
  unsigned category=CLASSES-1;
  float score=0;
  float probabilities[CLASSES]{};
  bool accepted=false;
  bool truncated=false;
};
inline unsigned hash(const std::string& token) {
  uint32_t value=2166136261u;
  for(unsigned char c:token){value^=c;value*=16777619u;}
  return value%DIM;
}
inline bool skipValue(const std::string& token) {
  static const char* const names[]={"device","component","incident","request","req","boot","boot_id","uptime_ms","unix_ms","duration_ms","first_uptime_ms","nonce"};
  for(auto name:names)if(token==name)return true;
  return false;
}
inline Prediction predict(const std::string& text) {
  // Bounded scratch; parameters are const flash data. Called from main loop only.
  uint8_t active[DIM]{};
  std::string previous,word;
  previous.reserve(32);word.reserve(33);
  bool skip=false,hasDigit=false,tooLong=false;
  auto consume=[&](){
    if(word.empty()&&!tooLong)return;
    if(skip){skip=false;}
    else if(skipValue(word)){skip=true;}
    else if(!tooLong&&!hasDigit){
      active[hash("w:"+word)]=1;
      if(word.find('_')!=std::string::npos){
        std::string part,previousPart;
        for(size_t i=0;i<=word.size();++i){
          if(i==word.size()||word[i]=='_'){
            if(!part.empty()){
              active[hash("w:"+part)]=1;
              if(!previousPart.empty())active[hash("b:"+previousPart+":"+part)]=1;
              previousPart=part;part.clear();
            }
          }else part+=word[i];
        }
      }
      if(!previous.empty())active[hash("b:"+previous+":"+word)]=1;
      previous=word;
    }
    word.clear();hasDigit=false;tooLong=false;
  };
  const size_t length=std::min<size_t>(text.size(),1024);
  for(size_t i=0;i<length;++i){
    unsigned char c=text[i];if(c>='A'&&c<='Z')c+=32;
    if((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'){
      if(c>='0'&&c<='9')hasDigit=true;
      if(word.size()<33)word+=char(c);else tooLong=true;
      if(word.size()>32)tooLong=true;
    }else consume();
  }
  consume();
  unsigned count=0;for(auto v:active)count+=v;
  Prediction result;result.truncated=text.size()>length;
  float scale=1.0f/std::sqrt(float(std::max(1u,count))),maximum=-1e30f;
  for(unsigned c=0;c<CLASSES;++c){
    float sum=0;for(unsigned i=0;i<DIM;++i)if(active[i])sum+=WEIGHTS[c][i];
    result.probabilities[c]=BIAS[c]+sum*scale;
    maximum=std::max(maximum,result.probabilities[c]);
  }
  float total=0;
  for(auto& score:result.probabilities){score=std::exp(score-maximum);total+=score;}
  for(unsigned c=0;c<CLASSES;++c){
    result.probabilities[c]/=total;
    if(result.probabilities[c]>result.score){result.category=c;result.score=result.probabilities[c];}
  }
  result.accepted=count>=2&&!result.truncated&&result.score>=THRESHOLD&&std::strcmp(LABELS[result.category],"unknown")!=0;
  return result;
}
}
