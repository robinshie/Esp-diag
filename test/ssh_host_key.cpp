#include "../src/security/ssh_host_key.h"
#include <cassert>
#include <map>
int main(){
  SshHostKey trust;std::map<std::string,std::string> store;bool readOk=true,writeOk=true;unsigned writes=0;
  trust.read=[&](const std::string& key,std::string& value){value=store[key];return readOk;};
  trust.write=[&](const std::string& key,const std::string& value){++writes;if(writeOk)store[key]=value;return writeOk;};
  auto a="SHA256:"+std::string(43,'A'),b="SHA256:"+std::string(43,'B');
  assert(trust.verify("192.168.10.2",22,a)=="trusted"&&writes==1);
  assert(trust.verify("192.168.10.2",22,a)=="trusted"&&writes==1);
  assert(trust.verify("192.168.10.2",22,b)=="mismatch"&&writes==1);
  assert(trust.verify("192.168.10.2",23,b)=="trusted"&&writes==2);
  assert(trust.verify("192.168.10.3",22,b,a)=="mismatch"&&writes==2);
  readOk=false;assert(trust.verify("192.168.10.2",22,b)=="error"&&writes==2);
  readOk=true;writeOk=false;assert(trust.verify("192.168.10.4",22,a)=="error");
  assert(trust.verify("192.168.10.5",22,"not-a-sha256") == "error");
  writeOk=true;store.erase("192.168.10.2:22");assert(trust.verify("192.168.10.2",22,b)=="trusted");
}
