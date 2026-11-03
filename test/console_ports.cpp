#include <cassert>
#include "../src/net/console_ports.h"
HardwareSerial* HardwareSerial::instance=nullptr;
int main(){
  using namespace switchtransport;
  UartConsolePort uart;assert(uart.begin(9600));auto* serial=HardwareSerial::instance;
  assert(serial->rx==18&&serial->tx==17&&serial->rate==9600);
  serial->input="abc";char bytes[8];assert(uart.read(bytes,2)==2&&std::string(bytes,2)=="ab");
  assert(uart.write("abcd",4)==2&&serial->output=="ab");auto epoch=uart.generation();uart.end();assert(!uart.ready()&&uart.generation()!=epoch&&uart.write("x",1)==0);
  TcpConsolePort tcp;assert(tcp.begin(9600));ETH.up=false;tcp.tick(1);assert(!tcp.ready()&&wire::connects==0);
  ETH.up=true;tcp.tick(2);assert(tcp.ready()&&wire::connects==1);
  wire::input="response";assert(tcp.read(bytes,8)==8&&std::string(bytes,8)=="response");
  assert(tcp.write("abcd",4)==2&&wire::output=="ab");assert(tcp.read(bytes,8)==0&&tcp.ready());
  epoch=tcp.generation();wire::eof=true;assert(tcp.read(bytes,8)==0&&!tcp.ready()&&epoch!=tcp.generation());
  wire::eof=false;wire::selection=0;tcp.tick(10);assert(!tcp.ready());tcp.tick(5011);assert(!tcp.ready());
  wire::selection=1;tcp.tick(10012);assert(tcp.ready());
  ETH.up=false;tcp.tick(10013);assert(!tcp.ready());tcp.end();
}
