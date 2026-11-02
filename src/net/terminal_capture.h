#pragma once
#include <cstddef>
#include <string>
namespace switchcli {
// Bounded output and independent tail allow detecting a split prompt even when truncated.
class TerminalCapture {
  std::string tail;
  enum Escape {None,Start,Csi,Osc,OscEnd} escape=None;
public:
  static constexpr size_t Limit=8192;
  std::string output;
  bool truncated=false;
  void append(const char* data,size_t size){
    for(size_t i=0;i<size;++i){unsigned char c=data[i];
      if(escape==Start){escape=c=='['?Csi:c==']'?Osc:None;continue;}
      if(escape==Csi){if(c>=0x40&&c<=0x7e)escape=None;continue;}
      if(escape==Osc){if(c==7)escape=None;else if(c==27)escape=OscEnd;continue;}
      if(escape==OscEnd){escape=c=='\\'?None:Osc;continue;}
      if(c==27){escape=Start;continue;}
      if(c=='\r')continue;
      if(c=='\b'){if(!tail.empty())tail.pop_back();if(!output.empty())output.pop_back();continue;}
      if(c<32&&c!='\n'&&c!='\t')continue;
      if(c==127)continue;
      if(c=='\n')tail.clear();else {tail+=char(c);if(tail.size()>160)tail.erase(0,tail.size()-160);}
      if(output.size()<Limit)output+=char(c);else truncated=true;
    }
  }
  const std::string& tailText() const { return tail; }
};
}
