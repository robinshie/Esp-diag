#include "../src/fault_journal.h"
#include <cassert>
#include <vector>

struct Fixture {
  cluster::Model model{"test-boot"};
  journal::FaultJournal log;
  std::vector<std::pair<journal::Kind,std::string>> records;
  unsigned sequence=0;
  Fixture(){
    log.sink=[this](journal::Kind kind,const std::string& fields){records.emplace_back(kind,fields);};
    model.output=[this](const std::string& line){log.output(line);};
    tick(10);
  }
  void tick(uint64_t now){log.clock(now);model.tick(now);log.observe(model);}
  void input(const std::string& body){
    auto line="H3C LOG device=sw-1 kind=switch rack=rack-a epoch=1 seq="+std::to_string(++sequence)+" "+body;
    assert(model.receive(line));log.input(line);log.observe(model);
  }
  size_t count(const std::string& event) const {
    size_t total=0;for(const auto& item:records)if(item.first==journal::Kind::Summary&&item.second.find("\"event\":\""+event+"\"")!=std::string::npos)++total;return total;
  }
};

int main(){
  assert(journal::quote("a\"\\\n\t")=="\"a\\\"\\\\\\u000a\\u0009\"");
  assert(journal::redact("event=FAULT password=\"secret with spaces\" tail=secret")=="event=FAULT password=[REDACTED_REMAINDER]");
  assert(journal::redact("x=1 MQTT_PASSWORD=secret").find("secret")==std::string::npos);
  Fixture f;
  f.input("event=LINK_DOWN status=ALARM level=CRITICAL component=GE1/0/8 cause=ADMIN_DOWN incident=loop-1");
  assert(f.count("DETECTED")==1);
  f.model.request(f.model.devices.at("sw-1"),"GE1/0/8:LINK_DOWN");
  assert(f.count("ACTION_EVIDENCE")==1);
  auto request=f.model.requests.begin()->first;
  f.tick(20);
  assert(f.model.receive("H3C ACK action=RECOVER_INTERFACE request_id="+request+" device=sw-1 component=GE1/0/8 status=OK"));
  f.log.observe(f.model);assert(f.count("RECOVERED")==0);
  f.tick(30);f.input("event=RECOVERY status=OK component=GE1/0/8 fault=LINK_DOWN");
  f.tick(2029);assert(f.count("RECOVERED")==0);
  f.tick(2030);assert(f.count("RECOVERED")==1);
  f.tick(2040);assert(f.count("RECOVERED")==1);
  bool process=false;for(const auto& record:f.records)if(record.first==journal::Kind::Process&&record.second.find("COMMAND_ACCEPTED")!=std::string::npos)process=true;assert(process);

  Fixture offline;
  offline.input("event=LOOP status=ALARM level=CRITICAL component=GE1/0/9 cause=UNKNOWN incident=real-1");
  offline.tick(90010);assert(offline.count("SOURCE_LOST")==1);assert(offline.count("RECOVERED")==0);
  offline.input("event=HEARTBEAT status=OK component=system");
  assert(offline.count("SOURCE_ONLINE")==2&&offline.count("RECOVERED")==0);
  assert(offline.model.requests.empty()); // Recording a loop does not add an isolation executor.
  offline.log.link(false);offline.log.link(false);assert(offline.count("ETH_LINK_DOWN")==1);
  offline.log.link(true);assert(offline.count("ETH_LINK_UP")==1);
  offline.log.input("H3C LOG event=FAULT password=secret");
  for(const auto& record:offline.records)assert(record.second.find("password=secret")==std::string::npos);
  auto before=offline.records.size();offline.log.output("H3C CONFIG password=secret");assert(offline.records.size()==before);
  offline.log.output("[BDI][STATE] "+std::string(1100,'x'));assert(offline.records.back().second.find("\"truncated\":true")!=std::string::npos);

  Fixture capacity;capacity.model.max_faults=128;
  for(unsigned i=0;i<65;++i)capacity.input("event=THERMAL status=ALARM level=WARN component=p"+std::to_string(i)+" incident=i"+std::to_string(i));
  assert(capacity.log.incomplete&&capacity.count("SUMMARY_CAPACITY")==1&&capacity.count("DETECTED")==64);
  capacity.tick(11);assert(capacity.count("SUMMARY_CAPACITY")==1);
}
