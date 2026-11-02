#pragma once
#include <functional>
#include <string>

// Trust on first use, bound to IPv4 + TCP port. Persistence precedes credentials.
// An existing key is never silently replaced; physical configuration can re-pair.
class SshHostKey {
public:
  using Read=std::function<bool(const std::string&,std::string&)>;
  using Write=std::function<bool(const std::string&,const std::string&)>;
  Read read;
  Write write;
  static bool valid(const std::string& key){
    if(key.size()!=50||key.compare(0,7,"SHA256:")!=0)return false;
    for(size_t i=7;i<key.size();++i){auto c=key[i];if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='+'||c=='/'))return false;}
    return true;
  }
  static std::string endpoint(const std::string& host,unsigned port){return host+":"+std::to_string(port);}
  std::string verify(const std::string& host,unsigned port,const std::string& observed,const std::string& legacy=""){
    if(!read||!write||!valid(observed))return "error";
    const auto id=endpoint(host,port);std::string stored;
    if(!read(id,stored))return "error";
    if(!stored.empty())return valid(stored)&&stored==observed?"trusted":"mismatch";
    if(!legacy.empty()&&legacy!=observed)return "mismatch";
    if(!write(id,observed)||!read(id,stored)||stored!=observed)return "error";
    return "trusted";
  }
};
