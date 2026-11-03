#pragma once
#include <algorithm>
#include <cctype>
#include <cstdint>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace switchmodel {
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

inline bool fresh(const Stamp& stamp, uint64_t now, uint64_t revision, uint64_t maxAgeMs) {
  return stamp.complete && stamp.cycle && revision && stamp.targetRevision == revision && stamp.atMs <= now && now-stamp.atMs <= maxAgeMs;
}


enum class Link { Unknown, Up, Down, AdminDown, Standby };

struct Interface { std::string name; Link link = Link::Unknown; char type = '?'; unsigned pvid = 0; };
struct Interfaces { Stamp stamp; bool valid = false; std::map<std::string,Interface> ports; };

enum class DownCause { Unknown, Physical, Administrative, Loopback, StpGuard, Aggregation, MonitorLink, Storm, Security, Mad, OtherProtection };

struct DownPort { Link link=Link::Unknown; DownCause cause=DownCause::Unknown; std::string detail; };
struct DownCauses { Stamp stamp; bool valid=false; std::map<std::string,DownPort> ports; };

enum class LoopAction { None, NoLearning, Block, Shutdown };
struct LoopPort { LoopAction action=LoopAction::None; std::string vlans; };
struct LoopDetection { Stamp stamp; bool valid=false, enabled=false; unsigned intervalSeconds=0; std::map<std::string,LoopPort> ports; };

struct StpPort { std::string instance,role,state,protection; };
struct Stp { Stamp stamp; bool valid=false, enabled=true; std::map<std::string,std::vector<StpPort>> ports; };

struct Aggregation { Stamp stamp; bool valid=false; std::set<std::string> members; };

struct TrafficCount { uint64_t total=0,broadcast=0,multicast=0,errors=0; };
struct TrafficCounters { Stamp stamp; bool valid=false; std::map<std::string,TrafficCount> ports; };
struct StormPort { uint64_t broadcastPps=0; unsigned rounds=0; bool measured=false; };
struct StormEvidence { Stamp stamp; bool valid=false; std::map<std::string,StormPort> ports; };

struct Snapshot {
  Interfaces interfaces; DownCauses down; LoopDetection loops; Stp stp; Aggregation aggregation;
  TrafficCounters traffic; StormEvidence storm;
  bool managementConnected=false, identityVerified=false;
  uint64_t targetRevision=0;
};

struct MacMove {std::string mac,current,source,time;unsigned vlan=0;uint32_t times=0;};
struct MacMoves {Stamp stamp;bool valid=false;std::map<std::string,MacMove> records;};

struct CpuUsage {Stamp stamp;bool valid=false;unsigned percent=0;};

struct Observation { Snapshot snapshot; MacMoves macMoves; CpuUsage cpu; bool identity=false; };
} // namespace switchmodel
