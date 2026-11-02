#include "../src/storage/local_report.h"
#include <cassert>
#include <iostream>
int main(){
  LocalReport report;assert(report.empty());
  assert(LocalReport::safeText(std::string("invalid:")+char(0xff))=="invalid:\xef\xbf\xbd");
  assert(LocalReport::safeText(std::string(999,'x')+"中文")==std::string(999,'x')+" [truncated]");
  assert(report.add(1234,"[SSH] state=connected"));
  assert(report.add(1250,"[NETWORK] password=secret token=hidden"));
  auto json=report.json("real-boot",7);
  assert(json.find("secret")==std::string::npos&&json.find("hidden")==std::string::npos);
  std::cout<<json;
  report.clear();assert(report.empty());
  for(unsigned i=0;i<LocalReport::MaxLines;++i)assert(report.add(i,"line"));
  assert(!report.add(1000,"overflow"));report.clear();
  assert(!report.add(1000,std::string(LocalReport::MaxBytes+1,'x')));
  assert(report.add(2000,"later"));assert(report.add(1000,"earlier producer"));
  assert(report.json("b",1).find("\"sent_at_ms\":2000")!=std::string::npos);
  report.clear();unsigned escaped=0;while(report.add(1,std::string(1000,'\x01')))++escaped;
  assert(escaped>0&&escaped<LocalReport::MaxLines);assert(report.json("b",1).size()<=LocalReport::MaxWireBytes);
  report.clear();assert(report.addAnalysis(3000,"{\"model\":\"device\",\"label\":\"heartbeat_ok\",\"accepted\":true}"));
  assert(!report.empty()&&report.count()==1);json=report.json("b",2);
  assert(json.find("\"sent_at_ms\":3000")!=std::string::npos&&json.find("\"analysis\":[{\"model\":\"device\"")!=std::string::npos);
  assert(!report.addAnalysis(1,"invalid"));assert(!report.addAnalysis(1,std::string(1001,'x')));
}
