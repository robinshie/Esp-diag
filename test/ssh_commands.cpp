#include "../src/net/ssh_commands.h"
#include <cassert>
int main(){
  assert(std::string(switchcli::command("interfaces"))=="display interface brief");
  for(auto bad:{"version;reboot","display version","current-configuration","version\nreboot","reboot",""})assert(!switchcli::command(bad));
  assert(switchcli::identifier("request_123-A"));assert(!switchcli::identifier("a b"));assert(!switchcli::identifier(std::string(33,'a')));
  switchcli::Capture capture;
  for(const std::string part:{"\033[3","1mversion\033[0m\r\n<H","3C>"})capture.append(part.data(),part.size());
  assert(capture.output=="version\n<H3C>"&&capture.prompt()=="<H3C>");
  auto large=std::string(10000,'x')+"\r\n<H3C>";capture.append(large.data(),large.size());assert(capture.truncated&&capture.output.size()==8192&&capture.prompt()=="<H3C>");
  switchcli::Capture error;auto message=std::string("% Unrecognized command\r\n<H3C>");error.append(message.data(),message.size());assert(error.failed());
  assert(switchcli::encode("a&b=\n")=="a%26b%3D%0A");
}
