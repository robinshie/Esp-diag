#pragma once
#include "mqtt_settings.h"
#include <cctype>

// Connection profile only. No protocol selection starts a session or executes commands.
struct SwitchSettings {
  std::string protocol="none", host, username, password, enablePassword, fingerprint;
  std::string transport="shell";
  unsigned port=22, connectTimeout=5, commandTimeout=5;
  static bool timeout(const std::string& s,unsigned& out) {
    unsigned n; if(!MqttSettings::parsePort(s,n)||n>30)return false;out=n;return true;
  }
  std::string validate() const {
    if(protocol!="none"&&protocol!="ssh"&&protocol!="telnet")return "请选择 SSH、Telnet 或暂不配置";
    if(!port||port>65535)return "交换机端口必须为 1–65535";
    if(connectTimeout<1||connectTimeout>30||commandTimeout<1||commandTimeout>30)return "连接和命令超时必须为 1–30 秒";
    if(transport!="shell"&&transport!="exec")return "SSH 会话类型无效";
    if(!MqttSettings::printable(username,64)||!MqttSettings::printable(password,128)||!MqttSettings::printable(enablePassword,128))return "账号密码过长或含控制字符";
    if(!host.empty()) {
      uint32_t ip;if(!NetworkSettings::address(host,ip)||(ip>>24)==0||(ip>>24)>=224||(ip>>24)==127)return "请填写交换机管理 IPv4 地址";
    }
    if(protocol!="none"&&(host.empty()||username.empty()))return "请填写交换机管理 IP 和用户名";
    if(!fingerprint.empty()) {
      if(fingerprint.size()!=50||fingerprint.compare(0,7,"SHA256:")!=0)return "SSH 指纹格式为 SHA256: 加 43 位 Base64 字符";
      for(size_t i=7;i<fingerprint.size();++i){char c=fingerprint[i];if(!isalnum((unsigned char)c)&&c!='+'&&c!='/')return "SSH 指纹格式无效";}
    }
    // Empty fingerprint explicitly selects skipping host identity verification.
    return "";
  }
  std::string validateNetwork(const NetworkSettings& net) const {
    auto error=validate();if(!error.empty()||protocol=="none")return error;
    uint32_t target,local,mask;
    NetworkSettings::address(host,target);NetworkSettings::address(net.ip,local);NetworkSettings::address(net.mask,mask);
    if(target==local)return "交换机管理 IP 不能与 ESP 网口 IP 相同";
    if((target&mask)==(local&mask)) {
      if(!(target&~mask)||(target&~mask)==~mask)return "交换机管理 IP 不能是本子网的网络地址或广播地址";
    } else if(net.gateway=="0.0.0.0")return "交换机与 ESP 跨网段时必须配置可达网关";
    return "";
  }
  std::string encode() const {
    return protocol+"\n"+host+"\n"+std::to_string(port)+"\n"+username+"\n"+password+"\n"+enablePassword+"\n"+fingerprint+"\n"+transport+"\n"+std::to_string(connectTimeout)+"\n"+std::to_string(commandTimeout);
  }
  bool decode(const std::string& raw) {
    if(raw.size()>600)return false;
    std::vector<std::string> f;size_t start=0;
    for(size_t i=0;i<=raw.size();++i)if(i==raw.size()||raw[i]=='\n'){f.push_back(raw.substr(start,i-start));start=i+1;}
    if(f.size()!=10)return false;
    SwitchSettings n;n.protocol=f[0];n.host=f[1];n.username=f[3];n.password=f[4];n.enablePassword=f[5];n.fingerprint=f[6];n.transport=f[7];
    if(!MqttSettings::parsePort(f[2],n.port)||!timeout(f[8],n.connectTimeout)||!timeout(f[9],n.commandTimeout)||!n.validate().empty())return false;
    *this=n;return true;
  }
};
