#pragma once
// Pure Comware R63xx parsing. Only complete, successful SSH responses may carry
// complete=true. Unknown formats deliberately remain invalid evidence.
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace h3c {
struct Stamp {
  uint64_t cycle = 0, atMs = 0, targetRevision = 0;
  bool complete = false;
};
inline std::string trim(const std::string& s) {
  const auto b = s.find_first_not_of(" \t\r\n");
  return b == std::string::npos ? "" : s.substr(b, s.find_last_not_of(" \t\r\n") - b + 1);
}
inline std::vector<std::string> words(const std::string& s) {
  std::istringstream in(s); std::vector<std::string> out; std::string word;
  while (in >> word) out.push_back(word);
  return out;
}
inline bool starts(const std::string& s, const std::string& prefix) { return s.compare(0, prefix.size(), prefix) == 0; }
inline bool integer(const std::string& s, unsigned& n, unsigned maximum = 4094) {
  if (s.empty() || s.size() > 5) return false;
  n = 0;
  for (unsigned char c : s) { if (c < '0' || c > '9') return false; n = n * 10 + (c - '0'); }
  return n <= maximum;
}
// A canonical physical port can never contain whitespace, CLI separators,
// ranges or view-changing text. Logical/aggregate ports are never actionable.
inline std::string normalizePort(const std::string& input) {
  std::string suffix, prefix;
  if (starts(input, "GigabitEthernet")) { prefix = "GigabitEthernet"; suffix = input.substr(15); }
  else if (starts(input, "Ten-GigabitEthernet")) { prefix = "Ten-GigabitEthernet"; suffix = input.substr(19); }
  else if (starts(input, "XGE")) { prefix = "Ten-GigabitEthernet"; suffix = input.substr(3); }
  else if (starts(input, "GE")) { prefix = "GigabitEthernet"; suffix = input.substr(2); }
  else return "";
  const auto a = suffix.find('/'), b = suffix.find('/', a == std::string::npos ? 0 : a + 1);
  if (a == std::string::npos || b == std::string::npos || suffix.find('/', b + 1) != std::string::npos) return "";
  unsigned member, slot, port;
  if (!integer(suffix.substr(0,a), member, 9) || !member ||
      !integer(suffix.substr(a+1,b-a-1), slot, 8) ||
      !integer(suffix.substr(b+1), port, 64) || !port) return "";
  return prefix + std::to_string(member) + "/" + std::to_string(slot) + "/" + std::to_string(port);
}
inline bool portLike(const std::string& s) {
  return starts(s,"GE") || starts(s,"XGE") || starts(s,"GigabitEthernet") || starts(s,"Ten-GigabitEthernet");
}
inline bool promptOrEcho(const std::string& s) {
  if (s.empty()) return true;
  if (s.front() == '<' || s.front() == '[') return s.find('>') != std::string::npos || s.find(']') != std::string::npos;
  return starts(s,"display ");
}
inline bool usableText(const std::string& text, const Stamp& stamp) {
  if (!stamp.complete || !stamp.cycle || !stamp.targetRevision || text.size() > 16384) return false;
  const char* errors[] = {"% Unrecognized", "% Incomplete", "% Ambiguous", "Permission denied", "Error:", "---- More ----"};
  for (const char* error : errors) if (text.find(error) != std::string::npos) return false;
  return text.find('\0') == std::string::npos;
}
inline bool fresh(const Stamp& stamp, uint64_t now, uint64_t revision, uint64_t maxAgeMs) {
  return stamp.complete && stamp.cycle && revision && stamp.targetRevision == revision && stamp.atMs <= now && now-stamp.atMs <= maxAgeMs;
}

enum class Link { Unknown, Up, Down, AdminDown, Standby };
inline Link parseLink(const std::string& s) {
  return s == "UP" ? Link::Up : s == "DOWN" ? Link::Down : s == "ADM" ? Link::AdminDown : s == "Stby" ? Link::Standby : Link::Unknown;
}
struct Interface { std::string name; Link link = Link::Unknown; char type = '?'; unsigned pvid = 0; };
struct Interfaces { Stamp stamp; bool valid = false; std::map<std::string,Interface> ports; };
inline Interfaces parseInterfaces(const std::string& text, const Stamp& stamp) {
  Interfaces result; result.stamp=stamp;
  if (!usableText(text,stamp)) return result;
  std::istringstream in(text); std::string line; bool bridge=false, header=false, malformed=false;
  while (std::getline(in,line)) {
    line=trim(line);
    if (line=="Brief information on interfaces in bridge mode:") { bridge=true; continue; }
    if (line=="Brief information on interfaces in route mode:") { bridge=false; continue; }
    if (!bridge) continue;
    auto row=words(line); if(row.empty()) continue;
    if(row.size()>=6 && row[0]=="Interface" && row[1]=="Link" && row[2]=="Speed" && row[4]=="Type") { header=true; continue; }
    if(!portLike(row[0])) continue;
    Interface port; port.name=normalizePort(row[0]);
    if(!header || row.size()<6 || port.name.empty() ||
       (port.link=parseLink(row[1]))==Link::Unknown || row[4].size()!=1 ||
       std::string("ATH").find(row[4])==std::string::npos || !integer(row[5],port.pvid) || !port.pvid ||
       result.ports.count(port.name) || result.ports.size()>=128) { malformed=true; continue; }
    port.type=row[4][0]; result.ports.emplace(port.name,port);
  }
  result.valid=header && !malformed && !result.ports.empty(); return result;
}

enum class DownCause { Unknown, Physical, Administrative, Loopback, StpGuard, Aggregation, MonitorLink, Storm, Security, Mad, OtherProtection };
inline DownCause parseCause(const std::string& text) {
  if (text=="Not connected") return DownCause::Physical;
  if (text=="Administratively" || text=="Administratively DOWN") return DownCause::Administrative;
  if (text.find("Loopback detection down")!=std::string::npos) return DownCause::Loopback;
  if (text.find("STP DOWN")!=std::string::npos) return DownCause::StpGuard;
  if (text.find("Link-Aggregation interface down")!=std::string::npos) return DownCause::Aggregation;
  if (text.find("Monitor-Link uplink down")!=std::string::npos) return DownCause::MonitorLink;
  if (text.find("Storm-Constrain")!=std::string::npos) return DownCause::Storm;
  if (text.find("Port Security Disabled")!=std::string::npos) return DownCause::Security;
  if (text.find("MAD ShutDown")!=std::string::npos) return DownCause::Mad;
  if (text.find("OFP DOWN")!=std::string::npos) return DownCause::OtherProtection;
  return DownCause::Unknown;
}
struct DownPort { Link link=Link::Unknown; DownCause cause=DownCause::Unknown; std::string detail; };
struct DownCauses { Stamp stamp; bool valid=false; std::map<std::string,DownPort> ports; };
inline DownCauses parseDownCauses(const std::string& text, const Stamp& stamp) {
  DownCauses result; result.stamp=stamp; if(!usableText(text,stamp)) return result;
  std::istringstream in(text); std::string line; bool bridge=false,header=false,bad=false;
  while(std::getline(in,line)) {
    line=trim(line);
    if(line=="Brief information on interfaces in bridge mode:") { bridge=true; continue; }
    if(line=="Brief information on interfaces in route mode:") { bridge=false; continue; }
    if(!bridge) continue;
    auto row=words(line); if(row.empty()) continue;
    if(row.size()==3 && row[0]=="Interface" && row[1]=="Link" && row[2]=="Cause") { header=true; continue; }
    if(!portLike(row[0])) continue;
    const auto name=normalizePort(row[0]); DownPort port;
    if(!header || name.empty() || row.size()<3 || result.ports.count(name) || result.ports.size()>=128 ||
       ((port.link=parseLink(row[1]))!=Link::Down && port.link!=Link::AdminDown)) { bad=true; continue; }
    for(size_t i=2;i<row.size();++i) { if(i>2) port.detail+=' '; port.detail+=row[i]; }
    port.cause=parseCause(port.detail); result.ports.emplace(name,port);
  }
  result.valid=header && !bad; return result;
}

enum class LoopAction { None, NoLearning, Block, Shutdown };
struct LoopPort { LoopAction action=LoopAction::None; std::string vlans; };
struct LoopDetection { Stamp stamp; bool valid=false, enabled=false; unsigned intervalSeconds=0; std::map<std::string,LoopPort> ports; };
inline LoopDetection parseLoops(const std::string& text, const Stamp& stamp) {
  LoopDetection result; result.stamp=stamp; if(!usableText(text,stamp)) return result;
  std::istringstream in(text); std::string line; bool status=false,clear=false,header=false,bad=false;
  while(std::getline(in,line)) {
    line=trim(line);
    if(line=="Loop detection is enabled.") { if(status) bad=true; status=true; result.enabled=true; continue; }
    if(line=="Loop detection is disabled." || line=="LPDT is not configured.") { if(status) bad=true; status=true; continue; }
    if(starts(line,"Loop detection interval is ")) {
      auto w=words(line); unsigned interval;
      if(w.size()!=6 || !integer(w[4],interval,300) || !interval || w[5]!="second(s).") bad=true;
      else result.intervalSeconds=interval;
      continue;
    }
    if(line=="No loop is detected." || line=="No loops are detected.") { clear=true; continue; }
    if(line.find("Interface")!=std::string::npos && line.find("Action mode")!=std::string::npos && line.find("VLANs")!=std::string::npos) { header=true; continue; }
    auto row=words(line); if(row.empty() || !portLike(row[0])) continue;
    const auto name=normalizePort(row[0]); LoopPort port;
    if(!header || name.empty() || row.size()<3 || result.ports.count(name) || result.ports.size()>=128) { bad=true; continue; }
    if(row[1]=="None") port.action=LoopAction::None;
    else if(row[1]=="No-learning") port.action=LoopAction::NoLearning;
    else if(row[1]=="Block") port.action=LoopAction::Block;
    else if(row[1]=="Shutdown") port.action=LoopAction::Shutdown;
    else { bad=true; continue; }
    for(size_t i=2;i<row.size();++i) {
      unsigned vlan;
      if(row[i]!="to" && !integer(row[i],vlan)) { bad=true; break; }
      if(i>2) port.vlans+=' ';
      port.vlans+=row[i];
    }
    result.ports.emplace(name,port);
  }
  result.valid=status && !bad && (!clear || result.ports.empty()) &&
    (!result.enabled ? result.ports.empty() : result.intervalSeconds && (clear || !result.ports.empty()));
  return result;
}

struct StpPort { std::string instance,role,state,protection; };
struct Stp { Stamp stamp; bool valid=false, enabled=true; std::map<std::string,std::vector<StpPort>> ports; };
inline Stp parseStp(const std::string& text, const Stamp& stamp) {
  Stp result; result.stamp=stamp; if(!usableText(text,stamp)) return result;
  std::istringstream in(text); std::string line; bool header=false,disabled=false,bad=false; size_t count=0;
  while(std::getline(in,line)) {
    line=trim(line);
    if(line=="STP is disabled." || line=="Spanning tree protocol is disabled.") { disabled=true; result.enabled=false; continue; }
    if(line.find("Port")!=std::string::npos && line.find("Role")!=std::string::npos && line.find("STP State")!=std::string::npos && line.find("Protection")!=std::string::npos) { header=true; continue; }
    auto row=words(line);
    for(size_t i=0;i<row.size();++i) if(portLike(row[i])) {
      const auto name=normalizePort(row[i]); StpPort port;
      if(!header || name.empty() || i==0 || row.size()!=i+4 || ++count>512) { bad=true; break; }
      unsigned instance; if(!integer(row[0],instance)) { bad=true; break; }
      port.instance=row[0]; port.role=row[i+1]; port.state=row[i+2]; port.protection=row[i+3];
      if(port.state!="FORWARDING" && port.state!="DISCARDING" && port.state!="LEARNING") { bad=true; break; }
      if(port.role!="ROOT" && port.role!="DESI" && port.role!="ALTE" && port.role!="BACK" && port.role!="MAST" && port.role!="DISA") { bad=true; break; }
      if(port.protection!="NONE" && port.protection!="LOOP" && port.protection!="ROOT" && port.protection!="BPDU") { bad=true; break; }
      result.ports[name].push_back(port); break;
    }
  }
  result.valid=!bad && (header || disabled) && !(disabled && !result.ports.empty()); return result;
}

struct Aggregation { Stamp stamp; bool valid=false; std::set<std::string> members; };
inline Aggregation parseAggregation(const std::string& text, const Stamp& stamp) {
  Aggregation result; result.stamp=stamp; if(!usableText(text,stamp)) return result;
  std::istringstream in(text); std::string line; bool header=false,group=false,bad=false,body=false,onlyLegend=true,empty=false;
  while(std::getline(in,line)) {
    line=trim(line); if(promptOrEcho(line)) continue;
    body=true;
    if(line=="No link aggregation group exists."){if(empty)bad=true;empty=true;continue;}
    if(starts(line,"Aggregation Interface:") || starts(line,"Aggregate Interface:")) group=true;
    if(starts(line,"Port Status:") && line.find("Selected")!=std::string::npos) header=true;
    if(starts(line,"Loadsharing Type:") || starts(line,"Flags:")) header=true;
    if(!starts(line,"Port Status:") && !starts(line,"Loadsharing Type:") && !starts(line,"Flags:") &&
       !starts(line,"Port:") && !starts(line,"D -- Synchronization") && !starts(line,"G -- Defaulted")) onlyLegend=false;
    auto row=words(line); if(row.empty()) continue;
    auto name=normalizePort(row[0]);
    // Some output annotates the local member name with (R)/(M)/(A).
    if(name.empty() && portLike(row[0])) {
      const auto mark=row[0].find('(');
      if(mark!=std::string::npos && row[0].back()==')') name=normalizePort(row[0].substr(0,mark));
    }
    if(!portLike(row[0])) continue;
    if(name.empty() || !header || !group || row.size()<2 || result.members.size()>=128) { bad=true; continue; }
    result.members.insert(name);
  }
  // With zero groups Comware can return only echo + prompt. This exception is
  // valid solely for a command that the SSH worker confirmed completed OK.
  result.valid=!bad && (empty?onlyLegend&&!group&&result.members.empty():!body || (header && (group || onlyLegend))); return result;
}

struct Snapshot {
  Interfaces interfaces; DownCauses down; LoopDetection loops; Stp stp; Aggregation aggregation;
  bool sshConnected=false, identityVerified=false;
  uint64_t targetRevision=0;
};
inline bool supportedIdentity(const std::string& version) {
  return version.find("H3C Comware Software, Version 7.1.070, Release 6328P03")!=std::string::npos &&
         version.find("S5130S-52S-EI")!=std::string::npos;
}
} // namespace h3c
