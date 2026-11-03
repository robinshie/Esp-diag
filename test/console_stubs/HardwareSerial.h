#pragma once
#include <string>
#include <cassert>
#define SERIAL_8N1 8
#define UART_HW_FLOWCTRL_DISABLE 0
class HardwareSerial {
public:
  static HardwareSerial* instance;
  bool active=false;unsigned rate=0;int rx=-1,tx=-1;std::string input,output;
  explicit HardwareSerial(int index){assert(index==1);instance=this;}
  void setRxBufferSize(unsigned){}void setTxBufferSize(unsigned){}
  void begin(unsigned baud,int config,int r,int t){assert(config==SERIAL_8N1);rate=baud;rx=r;tx=t;active=true;}
  void setHwFlowCtrlMode(int mode){assert(mode==UART_HW_FLOWCTRL_DISABLE);}
  explicit operator bool(){return active;}
  void end(){active=false;input.clear();output.clear();}
  int available(){return int(input.size());}
  int read(){if(input.empty())return -1;int c=input[0];input.erase(0,1);return c;}
  int availableForWrite(){return 2;}
  size_t write(const uint8_t* p,size_t n){output.append(reinterpret_cast<const char*>(p),n);return n;}
};
