#pragma once
#include "../config/firmware_mode.h"
#include <functional>
#include <string>
#include <cstdarg>
#include <cstdio>
namespace firmware {
extern std::function<void(const std::string&)> output;
inline void println(const std::string& line){if(output)output(line);}
inline void printf(const char* format,...){
  char buffer[1024];va_list args;va_start(args,format);int size=vsnprintf(buffer,sizeof(buffer),format,args);va_end(args);
  if(size<=0)return;
  std::string line(buffer);while(!line.empty()&&(line.back()=='\n'||line.back()=='\r'))line.pop_back();
  println(line);
}
}
