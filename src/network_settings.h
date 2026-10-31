#pragma once
#include <string>
#include <cstdio>
#include <cstdint>

struct NetworkSettings {
  std::string ip = "192.168.4.1", mask = "255.255.255.0";
  std::string gateway = "192.168.4.254", dns = "192.168.4.254";
  static bool address(const std::string& s, uint32_t& out) {
    out = 0; unsigned part = 0, digits = 0, count = 0;
    for (char c : s + ".") {
      if (c == '.') {
        if (!digits || ++count > 4 || part > 255) return false;
        out = (out << 8) | part; part = digits = 0;
      } else {
        if (c < '0' || c > '9' || ++digits > 3) return false;
        part = part * 10 + (c - '0');
      }
    }
    return count == 4;
  }
  std::string validate() const {
    uint32_t i, m, g, d;
    if (!address(ip,i) || !address(mask,m) || !address(gateway,g) || !address(dns,d)) return "IP 地址格式错误";
    uint32_t host = ~m;
    if (!m || host < 3 || (host & (host + 1))) return "子网掩码必须连续，且至少容纳两个主机";
    auto unicast = [](uint32_t a) { return (a >> 24) > 0 && (a >> 24) < 224 && (a >> 24) != 127; };
    if (!unicast(i) || !(i & host) || (i & host) == host) return "设备 IP 不能是网络地址、广播或保留地址";
    if (g && (!unicast(g) || (g & m) != (i & m) || !(g & host) || (g & host) == host || g == i)) return "网关必须是同一子网内的其他主机；不用网关可填 0.0.0.0";
    if (d && !unicast(d)) return "DNS 地址无效";
    return "";
  }
  std::string encode() const { return ip + "|" + mask + "|" + gateway + "|" + dns; }
  bool decode(const std::string& text) {
    NetworkSettings next; std::string* fields[] = {&next.ip,&next.mask,&next.gateway,&next.dns};
    size_t start = 0;
    for (int n = 0; n < 4; ++n) {
      size_t end = text.find('|', start);
      if ((n < 3 && end == std::string::npos) || (n == 3 && end != std::string::npos)) return false;
      *fields[n] = text.substr(start, end == std::string::npos ? end : end-start); start = end + 1;
    }
    if (!next.validate().empty()) return false;
    *this = next; return true;
  }
};
