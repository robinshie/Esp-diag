#pragma once
#include <Arduino.h>
#include <ETH.h>
#include <HardwareSerial.h>
#include <fcntl.h>
#include <errno.h>
#include "managed_console.h"
#include "../config/communication_settings.h"
#include <lwip/sockets.h>
namespace switchtransport {
class UartConsolePort : public IManagedConsolePort {
  HardwareSerial serial{1};
  bool active=false;
  uint64_t epoch=0;
public:
  bool begin(unsigned baud)override{
    end();serial.setRxBufferSize(4096);serial.setTxBufferSize(256);
    serial.begin(baud,SERIAL_8N1,ESP_CONSOLE_RX,ESP_CONSOLE_TX);
    serial.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
    active=bool(serial);++epoch;return active;
  }
  void end()override{if(active)serial.end();active=false;++epoch;}
  void tick(uint64_t)override{}
  bool ready()const override{return active;}
  uint64_t generation()const override{return epoch;}
  const char* stateName()const override{return active?"uart_ready":"disabled";}
  size_t read(char* bytes,size_t capacity)override{
    size_t n=0;while(active&&n<capacity&&serial.available()){int c=serial.read();if(c<0)break;bytes[n++]=char(c);}return n;
  }
  size_t write(const char* bytes,size_t size)override{
    if(!active)return 0;
    return serial.write(reinterpret_cast<const uint8_t*>(bytes),std::min(size,size_t(std::max(0,serial.availableForWrite()))));
  }
};
// Raw TCP only: the bridge must expose a transparent TCP server, not Telnet/RFC2217.
class TcpConsolePort : public IManagedConsolePort {
  int fd=-1;bool active=false,online=false;
  uint64_t epoch=0,retry=0,deadline=0;
  std::string state="disabled";
  void closeLink(){if(fd>=0)::close(fd);fd=-1;online=false;++epoch;}
  void fail(){closeLink();state="tcp_disconnected";retry=0;}
public:
  bool begin(unsigned)override{end();active=true;state="waiting_ethernet";return true;}
  void end()override{closeLink();active=false;retry=0;state="disabled";}
  bool ready()const override{return online;}
  uint64_t generation()const override{return epoch;}
  const char* stateName()const override{return state.c_str();}
  void tick(uint64_t now)override{
    if(!active)return;
    if(!ETH.linkUp()||uint32_t(ETH.localIP())==0){closeLink();state="waiting_ethernet";return;}
    if(fd<0){
      if(retry&&now<retry)return;
      retry=now+5000;fd=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
      if(fd<0){state="tcp_socket_failed";return;}
      if(fcntl(fd,F_SETFL,O_NONBLOCK)<0){fail();retry=now+5000;return;}
      sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=uint32_t(ETH.localIP());
      sockaddr_in remote{};remote.sin_family=AF_INET;remote.sin_port=htons(ESP_CONSOLE_BRIDGE_PORT);
      if(inet_pton(AF_INET,ESP_CONSOLE_BRIDGE_HOST,&remote.sin_addr)!=1||bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))<0){fail();retry=now+5000;return;}
      int rc=connect(fd,reinterpret_cast<sockaddr*>(&remote),sizeof(remote));
      if(rc==0){online=true;++epoch;state="tcp_ready";return;}
      if(errno!=EINPROGRESS){fail();retry=now+5000;return;}
      deadline=now+5000;state="tcp_connecting";
    }
    if(!online){
      fd_set writable;FD_ZERO(&writable);FD_SET(fd,&writable);timeval timeout{};
      int rc=select(fd+1,nullptr,&writable,nullptr,&timeout);
      if(rc>0){int error=0;socklen_t len=sizeof(error);
        if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&len)<0||error){fail();retry=now+5000;return;}
        online=true;++epoch;state="tcp_ready";
      }else if(rc<0||now>=deadline){fail();retry=now+5000;}
    }
  }
  size_t read(char* bytes,size_t capacity)override{
    if(!online)return 0;
    int n=recv(fd,bytes,capacity,0);if(n>0)return size_t(n);
    if(n==0||(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR))fail();
    return 0;
  }
  size_t write(const char* bytes,size_t size)override{
    if(!online)return 0;
    int n=send(fd,bytes,size,0);if(n>=0)return size_t(n);
    if(errno!=EAGAIN&&errno!=EWOULDBLOCK&&errno!=EINTR)fail();
    return 0;
  }
};
}
