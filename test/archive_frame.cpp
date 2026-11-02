#include "../src/legacy/archive_frame.h"
#include <cassert>
int main(){
  const auto prefix=std::string("H3C COPY req=")+std::string(16,'a')+" key="+std::string(64,'b')+" ";
  archive::Frame frame;
  assert(frame.parse(prefix+"op=status")&&frame.op=='S');
  assert(frame.parse(prefix+"op=begin seq=123 size=2097152")&&frame.size==2097152&&frame.sequence==123);
  assert(!frame.parse(prefix+"op=begin seq=1 size=2097153"));
  assert(!frame.parse(prefix+"op=begin seq=184467440737095516160 size=2"));
  assert(!frame.parse(prefix+"op=begin seq=0 size=2"));
  assert(frame.parse(prefix+"op=data offset=0 data=5rip")&&frame.count==3&&frame.data[0]==0xe6);
  assert(frame.parse(prefix+"op=data offset=3 data=AA==")&&frame.count==1&&frame.data[0]==0);
  assert(!frame.parse(prefix+"op=data offset=3 data=AB=="));
  assert(!frame.parse(prefix+"op=data offset=3 data=A===")&&!frame.parse(prefix+"op=data offset=3 data=AAAA="));
  assert(!frame.parse(prefix+"op=status op=end")&&!frame.parse(prefix+"op=status password=secret"));
  assert(frame.parse(prefix+"op=end")&&frame.op=='E');
  archive::FileInfo file;file.sequence=123;file.key=std::string(64,'a');archive::FileInfo parsed;
  assert(archive::FileInfo::parse(file.name(),parsed)&&parsed.sequence==123&&parsed.key==file.key);
  assert(!archive::FileInfo::parse("../"+file.name(),parsed));
  assert(!archive::FileInfo::parse("pending.part",parsed));
}
