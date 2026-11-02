// Worker tests with a deterministic clock/network and the real libssh API headers.
// Build after pio run:
// c++ -std=c++11 -Itest/ssh_stubs -I.pio/libdeps/esp32-s3-devkitc-1/LibSSH-ESP32/src test/switch_ssh.cpp -o /tmp/esp-switch-ssh
#include "../src/net/switch_ssh.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <map>
#include "../src/security/ssh_host_key.h"
uint32_t fakeMillis=0;
std::function<void()> onDelay;
void (*worker)(void*)=nullptr;
void* workerArg=nullptr;
std::string serialOutput;
std::function<void(const std::string&)> firmware::output=[](const std::string& line){serialOutput+=line+"\n";};
FakeSerial Serial;
FakeEthernet ETH;
unsigned connects=0,auths=0,sessionsFreed=0,channelsFreed=0;
std::deque<std::string> incoming;
std::vector<std::string> written;
bool emulateCommands=false,wrongView=false;
int authResult=SSH_AUTH_SUCCESS,connectResult=SSH_OK,shellResult=SSH_OK;
void libssh_begin(){}
extern "C" {
ssh_session ssh_new(){return reinterpret_cast<ssh_session>(1);}
int ssh_options_set(ssh_session,enum ssh_options_e option,const void* value){
  if(option==SSH_OPTIONS_HOSTKEYS)assert(std::string(static_cast<const char*>(value))=="+ssh-rsa");
  return SSH_OK;
}
const char* ssh_get_error(void*){return "simulated connection failure";}
int ssh_connect(ssh_session){++connects;return connectResult;}
void ssh_disconnect(ssh_session){}
void ssh_free(ssh_session){++sessionsFreed;}
int ssh_is_connected(ssh_session){return 1;}
int ssh_get_server_publickey(ssh_session,ssh_key* key){*key=reinterpret_cast<ssh_key>(2);return SSH_OK;}
int ssh_get_publickey_hash(const ssh_key,enum ssh_publickey_hash_type,unsigned char** hash,size_t* size){static unsigned char bytes[32];*hash=bytes;*size=32;return SSH_OK;}
void ssh_key_free(ssh_key){}
char* ssh_get_fingerprint_hash(enum ssh_publickey_hash_type,unsigned char*,size_t){static char value[]="SHA256:AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";return value;}
void ssh_string_free_char(char*){}
void ssh_clean_pubkey_hash(unsigned char**){}
int ssh_userauth_password(ssh_session,const char*,const char*){++auths;return authResult;}
ssh_channel ssh_channel_new(ssh_session){return reinterpret_cast<ssh_channel>(3);}
int ssh_channel_open_session(ssh_channel){return SSH_OK;}
int ssh_channel_get_exit_state(ssh_channel,uint32_t* code,char**,int*){*code=0;return SSH_OK;}
int ssh_channel_request_exec(ssh_channel,const char*){return SSH_OK;}
int ssh_channel_write(ssh_channel,const void* data,uint32_t length){
  std::string wire(static_cast<const char*>(data),length);written.push_back(wire);
  if(emulateCommands){
    if(wire=="\r")incoming.push_back("\r\n<H3C>");
    else if(wire=="screen-length disable\r")incoming.push_back("screen-length disable\r\n<H3C>");
    else if(wire=="system-view\r")incoming.push_back("system-view\r\n[H3C]");
    else if(wire=="interface GigabitEthernet 1/0/2\r")incoming.push_back(wire+(wrongView?"\n[H3C-GigabitEthernet1/0/3]":"\n[H3C-GigabitEthernet1/0/2]"));
    else if(wire=="shutdown\r")incoming.push_back(wire+"\n[H3C-GigabitEthernet1/0/2]");
    else if(wire=="return\r")incoming.push_back(wire+"\n<H3C>");
    else incoming.push_back(wire+"H3C Comware Software, Version test\r\n<H3C>");
  }return length;
}
int ssh_channel_request_pty(ssh_channel){return SSH_OK;}
int ssh_channel_request_shell(ssh_channel){return shellResult;}
int ssh_channel_read_nonblocking(ssh_channel,void* data,uint32_t count,int){
  if(incoming.empty())return 0;
  auto size=std::min<size_t>(count,incoming.front().size());memcpy(data,incoming.front().data(),size);incoming.front().erase(0,size);if(incoming.front().empty())incoming.pop_front();return size;
}
int ssh_channel_is_eof(ssh_channel){return 0;}
int ssh_channel_is_closed(ssh_channel){return 0;}
void ssh_channel_free(ssh_channel){++channelsFreed;}
int ssh_send_ignore(ssh_session,const char*){return SSH_OK;}
}
struct Stop {};
std::map<std::string,std::string> trustedKeys;
bool trustWritable=true;
void prepare(SwitchSsh& ssh){
  ssh.setHostKeyVerifier([](const std::string& host,unsigned port,const std::string& observed,const std::string& legacy){
    SshHostKey trust;trust.read=[](const std::string& endpoint,std::string& value){value=trustedKeys[endpoint];return true;};
    trust.write=[](const std::string& endpoint,const std::string& key){if(!trustWritable)return false;trustedKeys[endpoint]=key;return true;};
    return trust.verify(host,port,observed,legacy);
  });
}
void reset(){wrongView=false;trustedKeys.clear();trustWritable=true;incoming.clear();written.clear();emulateCommands=false;fakeMillis=connects=auths=sessionsFreed=channelsFreed=0;serialOutput.clear();ETH.up=true;authResult=connectResult=shellResult=SSH_OK;}
void execute(){try{worker(workerArg);}catch(const Stop&){}assert(serialOutput.find("Admin@12356")==std::string::npos);}
int main(){
  reset();SwitchSettings profile;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);
    onDelay=[&](){if(fakeMillis>=1000)ETH.up=false;if(fakeMillis>=2000)throw Stop{};};execute();
    assert(serialOutput.find("state=connected\n")!=std::string::npos);assert(connects==1&&auths==1&&sessionsFreed==1&&channelsFreed==1);
  }
  reset();authResult=SSH_AUTH_DENIED;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[](){if(fakeMillis>=120000)throw Stop{};};execute();
    assert(std::string(ssh.stateName())=="authentication_failed");assert(connects==1&&auths==1&&sessionsFreed==1);
  }
  reset();profile.fingerprint="SHA256:"+std::string(43,'B');
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[](){if(fakeMillis>=120000)throw Stop{};};execute();
    assert(std::string(ssh.stateName())=="host_key_mismatch");assert(auths==0&&connects==1&&sessionsFreed==1);
  }
  reset();profile.fingerprint="SHA256:"+std::string(43,'A');
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[&](){if(fakeMillis>=500)ETH.up=false;if(fakeMillis>=1000)throw Stop{};};execute();assert(auths==1&&sessionsFreed==1);
  }
  reset();profile.fingerprint.clear();authResult=SSH_AUTH_DENIED;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);bool changed=false;
    onDelay=[&](){if(fakeMillis>=1000&&!changed){changed=true;profile.password="replacement";authResult=SSH_AUTH_SUCCESS;ssh.configure(profile);}if(fakeMillis>=2000)ETH.up=false;if(fakeMillis>=3000)throw Stop{};};execute();
    assert(connects==2&&auths==2&&sessionsFreed==2);assert(serialOutput.find("state=connected\n")!=std::string::npos);
  }
  reset();connectResult=SSH_ERROR;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[](){if(fakeMillis>=61000)throw Stop{};};execute();assert(connects==3&&auths==0&&sessionsFreed==3);
  }
  reset();ETH.up=false;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[](){if(fakeMillis>=1000)throw Stop{};};execute();assert(connects==0&&std::string(ssh.stateName())=="waiting_network");
  }
  reset();emulateCommands=true;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);bool submitted=false;
    assert(ssh.submit("bad", "version;reboot")=="invalid_command");
    onDelay=[&](){
      if(ssh.connected()&&!submitted){submitted=true;assert(ssh.submit("query1","version")=="queued");assert(ssh.submit("query1","version")=="queued");assert(ssh.submit("query2","memory")=="busy");}
      if(ssh.resultReady("query1")){auto result=ssh.result("query1");assert(result.state=="ok"&&result.output.find("Comware")!=std::string::npos);ETH.up=false;}
      if(fakeMillis>=2000)throw Stop{};
    };execute();assert(written.size()==3);assert(written.back()=="display version\r");assert(sessionsFreed==1);
  }
  reset();
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);bool submitted=false;
    onDelay=[&](){
      if(ssh.connected()&&!submitted){submitted=true;assert(ssh.submit("timeout","memory")=="queued");}
      if(fakeMillis>=7000)throw Stop{};
    };execute();assert(ssh.result("timeout").state=="timeout_or_disconnected");assert(sessionsFreed==1);
  }

  reset();trustWritable=false;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[](){if(fakeMillis>=1000)throw Stop{};};execute();
    assert(auths==0&&std::string(ssh.stateName())=="host_key_store_error");
  }
  reset();emulateCommands=true;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);bool submitted=false;
    assert(ssh.submit("web", "isolate:GigabitEthernet1/0/2")=="invalid_command");
    assert(ssh.submitIsolation("bdi-bad","GE1/0/2;reboot")=="invalid_command");
    onDelay=[&](){
      if(ssh.connected()&&!submitted){submitted=true;assert(ssh.submitIsolation("bdi-act1","GE1/0/2")=="queued");}
      if(ssh.resultReady("bdi-act1")){
        assert(ssh.submit("human-query","version")=="busy");
        assert(ssh.takeResult("bdi-act1").state=="ok");
        assert(ssh.submit("human-query","version")=="queued");ETH.up=false;
      }
      if(fakeMillis>=2000)throw Stop{};
    };execute();assert(written.size()==6);assert(written[2]=="system-view\r");assert(written[4]=="shutdown\r");assert(written[5]=="return\r");
  }

  reset();emulateCommands=true;wrongView=true;
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);bool submitted=false;
    onDelay=[&](){
      if(ssh.connected()&&!submitted){submitted=true;assert(ssh.submitIsolation("bdi-wrong","GE1/0/2")=="queued");}
      if(fakeMillis>=2000)throw Stop{};
    };execute();
    assert(ssh.result("bdi-wrong").state!="ok");
    for(const auto& command:written)assert(command!="shutdown\r");
  }
  reset();trustedKeys["192.168.10.2:22"]="SHA256:"+std::string(43,'B');
  {
    SwitchSsh ssh;prepare(ssh);ssh.begin(profile);onDelay=[](){if(fakeMillis>=1000)throw Stop{};};execute();
    assert(auths==0&&std::string(ssh.stateName())=="host_key_mismatch");
  }

}
