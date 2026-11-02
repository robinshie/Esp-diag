#ifndef ESP_CLUSTER_MODEL_H
#define ESP_CLUSTER_MODEL_H

// Hardware-independent authoritative state. Also compiled into the native test
// agent, so scenario tests exercise exactly the firmware's parser and state.
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace cluster {
using Fields = std::map<std::string, std::string>;
inline std::string get(const Fields& f, const std::string& key) {
  auto i = f.find(key);
  return i == f.end() ? "" : i->second;
}
inline bool identifier(const std::string& s) {
  if (s.empty() || s.size() > 64) return false;
  for (char c : s)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.' || c == '/'))
      return false;
  return true;
}
inline bool number(const std::string& s, uint64_t& n) {
  if (s.empty() || s.size() > 16) return false;
  n = 0;
  for (char c : s) {
    if (c < '0' || c > '9') return false;
    n = n * 10 + static_cast<unsigned>(c - '0');
  }
  return true;
}
inline bool parse(const std::string& line, Fields& f) {
  std::istringstream in(line);
  std::string token;
  while (in >> token) {
    auto p = token.find('=');
    if (p == std::string::npos) continue;
    auto key = token.substr(0, p);
    if (f.count(key)) return false;
    f[key] = token.substr(p + 1);
  }
  return true;
}
struct Fault {
  std::string component, event, cause, severity, incident;
  uint64_t generation = 0;
  uint64_t recovering_since = 0;
  bool recovering = false;
};
struct Device {
  std::string id, kind = "server", rack = "unknown";
  uint64_t epoch = 0, sequence = 0, last_seen = 0;
  bool online = false;
  bool incomplete = false;
  std::map<std::string, Fault> faults;
  std::string last_snapshot;
};
struct Request {
  std::string id, device, key, component, action, state = "WAIT_ACK";
  uint64_t generation = 0, deadline = 0;
  unsigned attempts = 1;
};
class Model {
 public:
  std::map<std::string, Device> devices;
  std::map<std::string, Request> requests;
  std::function<void(const std::string&)> output;
  std::function<void(Device&, const std::string&)> opened;
  std::function<void(Device&)> changed;
  uint64_t now = 0;
  uint64_t stale_ms = 90000, settle_ms = 2000, ack_ms = 3000, verify_ms = 10000;
  size_t max_devices = 32, max_faults = 32, max_total_faults = 128;
  unsigned rejected = 0;
  std::string last_rejection;
  explicit Model(std::string boot) : boot_(std::move(boot)) {}

  void emit(const std::string& s) { if (output) output(s); }
  bool reject(const std::string& why) {
    ++rejected;
    last_rejection = why;
    emit("[BDI][WARN] reason=" + why);
    return false;
  }
  static std::string key(const std::string& component, const std::string& event) {
    return component + ":" + event;
  }
  static bool locked(const Device& d) {
    for (auto& f : d.faults) if (f.second.event == "AUTH_LOCKED") return true;
    return false;
  }
  static bool critical(const Device& d) {
    for (auto& f : d.faults) if (f.second.severity == "CRITICAL") return true;
    return false;
  }
  static bool link_fault(const Device& d) {
    for (auto& f : d.faults) if (f.second.event == "LINK_DOWN") return true;
    return false;
  }
  static std::string health(const Device& d) {
    if (!d.online || d.incomplete) return "UNKNOWN";
    if (critical(d)) return "FAULT";
    return d.faults.empty() ? "NORMAL" : "DEGRADED";
  }
  void snapshot(Device& d, bool force = false) {
    std::string faults;
    for (auto& f : d.faults) {
      if (!faults.empty()) faults += ",";
      faults += f.first;
    }
    std::string data = "device=" + d.id + " kind=" + d.kind + " rack=" + d.rack +
        " state=" + health(d) + " availability=" + (d.online ? "ONLINE" : "OFFLINE") +
        " locked=" + (locked(d) ? "true" : "false") +
        " healthy=" + (d.online && !d.incomplete && d.faults.empty() ? "true" : "false") +
        " critical=" + (critical(d) ? "true" : "false") +
        " link_fault=" + (link_fault(d) ? "true" : "false") +
        " incomplete=" + (d.incomplete ? "true" : "false") +
        " active_faults=" + std::to_string(d.faults.size()) +
        " faults=" + (faults.empty() ? "none" : faults);
    if (force || data != d.last_snapshot) {
      d.last_snapshot = data;
      emit("[BDI][STATE] " + data + " observed_ms=" + std::to_string(now));
    }
    if (changed) changed(d);
  }
  bool ingest(const std::string& line) {
    if (line.size() > 511) return reject("FRAME_TOO_LONG");
    Fields f;
    if (!parse(line, f)) return reject("DUPLICATE_FIELD");
    if (line.compare(0, 9, "H3C PING ") == 0) {
      if (!identifier(get(f, "nonce"))) return reject("INVALID_NONCE");
      emit("H3C PONG nonce=" + get(f, "nonce") + " boot=" + boot_ + " transport=USB");
      return true;
    }
    if (line.compare(0, 11, "H3C CONFIG ") == 0) {
      uint64_t stale, settle, ack, verify;
      if (f.size() != 5 || !identifier(get(f, "nonce")) ||
          !number(get(f, "stale_ms"), stale) || stale < 1000 || stale > 3600000 ||
          !number(get(f, "settle_ms"), settle) || settle < 100 || settle > 60000 ||
          !number(get(f, "ack_ms"), ack) || ack < 100 || ack > 60000 ||
          !number(get(f, "verify_ms"), verify) || verify < 1000 || verify > 600000 || verify <= settle)
        return reject("INVALID_CONFIG");
      bool differs = stale != stale_ms || settle != settle_ms || ack != ack_ms || verify != verify_ms;
      if (differs)
        for (auto& request : requests)
          if (request.second.state != "FAILED") return reject("CONFIG_DURING_ACTION");
      stale_ms = stale; settle_ms = settle; ack_ms = ack; verify_ms = verify;
      emit("H3C CONFIGURED nonce=" + get(f, "nonce") + " stale_ms=" + std::to_string(stale_ms) +
           " settle_ms=" + std::to_string(settle_ms) + " ack_ms=" + std::to_string(ack_ms) +
           " verify_ms=" + std::to_string(verify_ms));
      return true;
    }
    if (line.compare(0, 8, "H3C ACK ") == 0) return acknowledge(f);
    if (line.compare(0, 8, "H3C LOG ") != 0) return reject("UNSUPPORTED_INPUT");
    std::string id = get(f, "device"), kind = get(f, "kind");
    std::string event = get(f, "event"), status = get(f, "status");
    uint64_t epoch, seq, age = 0;
    if (!identifier(id) || id.find('/') != std::string::npos ||
        (kind != "server" && kind != "switch") ||
        !number(get(f, "epoch"), epoch) || !epoch ||
        !number(get(f, "seq"), seq) || !seq)
      return reject("INVALID_ENVELOPE");
    if ((!get(f, "age_ms").empty() && !number(get(f, "age_ms"), age)) ||
        (!get(f, "incident").empty() && !identifier(get(f, "incident"))))
      return reject("INVALID_AGE_OR_INCIDENT");
    bool heartbeat = event == "HEARTBEAT" && status == "OK";
    bool recovery = (event == "RECOVERY" || event == "LINK_UP") && status == "OK";
    bool alarm = status == "ALARM" || (event == "AUTH_LOCKED" && status == "LOCKED");
    const std::string component = get(f, "component");
    std::string fault_event = recovery ? get(f, "fault") : event;
    if ((!heartbeat && !recovery && !alarm) ||
        (!heartbeat && (!identifier(component) || !identifier(fault_event))) ||
        (alarm && get(f, "level") != "CRITICAL" && get(f, "level") != "WARN"))
      return reject("INVALID_EVENT");
    if (!get(f, "rack").empty() && !identifier(get(f, "rack"))) return reject("INVALID_RACK");
    auto it = devices.find(id);
    if (it == devices.end()) {
      if (devices.size() >= max_devices) return reject("DEVICE_CAPACITY");
      Device d; d.id = id; d.kind = kind;
      it = devices.emplace(id, std::move(d)).first;
    }
    Device& d = it->second;
    if (d.kind != kind || epoch < d.epoch || (epoch == d.epoch && seq <= d.sequence))
      return reject("STALE_OR_DUPLICATE");
    // A new source epoch never clears active faults. Explicit matching recovery
    // or a reconciled replay from the durable gateway is still required.
    d.epoch = epoch; d.sequence = seq;
    d.last_seen = age > now ? 0 : now - age; d.online = age < stale_ms;
    if (!get(f, "rack").empty()) d.rack = get(f, "rack");
    std::string k = key(component, fault_event);
    bool is_new = false;
    if (recovery) {
      auto fault = d.faults.find(k);
      if (d.online && fault != d.faults.end() && !fault->second.recovering) {
        fault->second.recovering = true;
        fault->second.recovering_since = now;
      }
    } else if (alarm) {
      auto old = d.faults.find(k);
      if (old == d.faults.end()) {
        size_t total_faults = 0;
        for (auto& item : devices) total_faults += item.second.faults.size();
        if (d.faults.size() >= max_faults || total_faults >= max_total_faults) {
          d.incomplete = true; snapshot(d);
          return reject("FAULT_CAPACITY");
        }
        Fault fault;
        fault.component = component; fault.event = event;
        fault.cause = get(f, "cause"); fault.severity = get(f, "level");
        fault.generation = ++generation_;
        fault.incident = get(f, "incident");
        if (fault.incident.empty()) fault.incident = std::to_string(epoch) + "-" + std::to_string(seq);
        d.faults.emplace(k, std::move(fault));
        is_new = true;
      } else {
        old->second.recovering = false;
        old->second.severity = get(f, "level");
      }
    }
    snapshot(d, heartbeat || get(f, "replay") == "true");
    if (is_new && opened) opened(d, k);
    if (heartbeat && d.online && opened)
      for (auto& item : d.faults)
        if (!item.second.recovering) opened(d, item.first);
    return true;
  }
  bool receive(const std::string& line) {
    bool ok = ingest(line);
    if (line.compare(0, 8, "H3C LOG ") == 0) {
      Fields f;
      uint64_t epoch, seq;
      if (line.size() <= 511 && parse(line, f) && identifier(get(f, "device")) &&
          number(get(f, "epoch"), epoch) && number(get(f, "seq"), seq)) {
        bool duplicate = false;
        auto d = devices.find(get(f, "device"));
        if (!ok && last_rejection == "STALE_OR_DUPLICATE" && d != devices.end())
          duplicate = d->second.epoch == epoch && d->second.sequence == seq;
        emit("[BDI][RECEIPT] device=" + get(f, "device") + " epoch=" + get(f, "epoch") +
             " seq=" + get(f, "seq") + " result=" + (ok ? "ACCEPTED" : duplicate ? "DUPLICATE" : "REJECTED"));
      }
    }
    return ok;
  }
  void request(Device& d, const std::string& k) {
    auto f = d.faults.find(k);
    if (f == d.faults.end() || !d.online || d.incomplete) return;
    std::string action;
    if (f->second.event == "AUTH_LOCKED") action = "UNLOCK";
    else if (f->second.event == "LINK_DOWN" && f->second.cause == "ADMIN_DOWN")
      action = "RECOVER_INTERFACE";
    else return;  // Cable loss, PSU failure, etc. cannot be fixed by undo shutdown.
    for (auto& p : requests)
      if (p.second.device == d.id && p.second.key == k &&
          p.second.generation == f->second.generation) return;
    if (requests.size() >= max_devices * max_faults) { reject("REQUEST_CAPACITY"); return; }
    Request r;
    r.id = boot_ + "-" + std::to_string(++request_counter_);
    r.device = d.id; r.key = k; r.component = f->second.component;
    r.action = action; r.generation = f->second.generation;
    r.deadline = now + ack_ms;
    requests.emplace(r.id, r);
    command(r);
  }
  void command(const Request& r) {
    const auto& fault = devices.at(r.device).faults.at(r.key);
    emit("[BDI][CMD] " + r.action + " request_id=" + r.id + " device=" + r.device +
         " component=" + r.component + " target=" + r.component +
         " fault=" + r.key.substr(r.key.find(':') + 1) +
         " incident=" + fault.incident +
         " attempt=" + std::to_string(r.attempts));
  }
  bool acknowledge(const Fields& f) {
    auto p = requests.find(get(f, "request_id"));
    if (p == requests.end()) return reject("UNKNOWN_ACK");
    Request& r = p->second;
    if (get(f, "device") != r.device || get(f, "component") != r.component ||
        get(f, "action") != r.action || r.state != "WAIT_ACK" || now >= r.deadline)
      return reject("MISMATCH_OR_LATE_ACK");
    std::string status = get(f, "status");
    if (status != "OK" && status != "FAILED") return reject("INVALID_ACK");
    if (status == "OK") {
      r.state = "VERIFY"; r.deadline = now + verify_ms;
      audit(r, "COMMAND_ACCEPTED");
    } else {
      // Keep the same idempotency key. The executor never repeats an accepted
      // operation, even if the USB ACK was lost.
      r.deadline = now;
      audit(r, "ATTEMPT_FAILED");
    }
    return true;
  }
  void audit(const Request& r, const std::string& result) {
    emit("[BDI][ACK] action=" + r.action + " request_id=" + r.id +
         " device=" + r.device + " component=" + r.component + " state=" + r.state +
         " result=" + result + " attempt=" + std::to_string(r.attempts));
  }
  void tick(uint64_t time) {
    now = time;
    for (auto& item : devices) {
      Device& d = item.second;
      if (d.online && now - d.last_seen >= stale_ms) {
        d.online = false;
        for (auto& f : d.faults) f.second.recovering = false;
      }
      for (auto f = d.faults.begin(); f != d.faults.end();) {
        if (d.online && f->second.recovering && now - f->second.recovering_since >= settle_ms) {
          emit("[BDI][RECOVERED] device=" + d.id + " component=" + f->second.component +
               " fault=" + f->second.event + " incident=" + f->second.incident);
          f = d.faults.erase(f);
        } else ++f;
      }
      snapshot(d);
    }
    for (auto p = requests.begin(); p != requests.end();) {
      Request& r = p->second;
      auto& d = devices.at(r.device);
      auto f = d.faults.find(r.key);
      if (f == d.faults.end() || f->second.generation != r.generation) {
        r.state = "DONE"; audit(r, "SUCCESS");
        p = requests.erase(p); continue;
      }
      if (r.state != "FAILED" && now >= r.deadline) {
        if (r.state == "WAIT_ACK" && r.attempts < 3 && d.online) {
          ++r.attempts; r.deadline = now + ack_ms; command(r);
        } else {
          r.state = "FAILED";
          audit(r, "FAILED");  // Circuit stays open until matching recovery.
        }
      }
      ++p;
    }
  }
 private:
  std::string boot_;
  uint64_t request_counter_ = 0, generation_ = 0;
};
}  // namespace cluster
#endif
