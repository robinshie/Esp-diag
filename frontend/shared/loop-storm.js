// Display-only assessment of saved evidence. Never produces CLI capabilities.
// Comware's clock may be unset: align a login with the observed SSH reconnect,
// then require a matching logout and local ICMP loss before labeling suspicion.
const months=['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
function switchTime(text){
 const m=/^%?(\w{3})\s+(\d{1,2}) (\d{2}):(\d{2}):(\d{2}):(\d{3}) (\d{4}) H3C /.exec(text);
 if(!m)return null;
 const month=months.indexOf(m[1]),day=Number(m[2]),hour=Number(m[3]),minute=Number(m[4]),second=Number(m[5]);
 if(month<0||day<1||day>31||hour>23||minute>59||second>59)return null;
 const at=Date.UTC(Number(m[7]),month,day,hour,minute,second,Number(m[6]));
 return new Date(at).getUTCDate()===day?at:null;
}
const stormFields=text=>Object.fromEntries([...text.matchAll(/(?:^|\s)([a-z_]+)=([^\s]+)/g)].map(m=>[m[1],m[2]]));
export function assessLoopStorms(reports){
 const rows=[];
 for(const report of reports){
  if(report.source!=='esp_native'||report.transport!=='ETH'||report.clock!=='uptime'||!report.boot)continue;
  report.lines.forEach((line,index)=>{
   if(Number.isFinite(line.at_ms)&&line.at_ms>=0&&line.at_ms<=report.sent_at_ms)rows.push({boot:report.boot,at:line.at_ms,text:line.text,reportId:report.report_id,line:index});
  });
 }
 rows.sort((a,b)=>a.at-b.at);
 const statuses=[],connections=[],heartbeats=[],events=[],seen=new Set();
 for(const row of rows){
  const f=stormFields(row.text);
  if(row.text.startsWith('[SSH][STATUS] ')&&f.target&&f.ip){
   const target=/^([^:]+):\d+$/.exec(f.target)?.[1];
   if(target)statuses.push({...row,target,ip:f.ip});
  }
  if(/^\[SSH\] state=(connected|disconnected)$/.test(row.text)){
   const status=[...statuses].reverse().find(s=>s.boot===row.boot&&row.at-s.at<=10000);
   if(status)connections.push({...row,target:status.target,ip:status.ip,state:f.state});
  }
  if(row.text.startsWith('[BDI][HEARTBEAT] ')&&f.target&&f.state==='lost')heartbeats.push({...row,target:f.target});
  const output=/^\[SSH\]\[OUTPUT\] request_id=(\S+) command=logs target=(\S+) text=(.*)$/.exec(row.text);
  if(!output)continue;
  const [,requestId,target,text]=output,at=switchTime(text),key=JSON.stringify([row.boot,target,text]);
  if(at===null||seen.has(key))continue;
  seen.add(key);
  const loop=/ H3C LPDT\/\d\/LPDT_VLAN_LOOPED: A loop was detected on (GigabitEthernet\d+\/\d+\/\d+|Ten-GigabitEthernet\d+\/\d+\/\d+) in VLAN (\d+)\.$/.exec(text);
  const session=/ H3C SHELL\/\d\/SHELL_(LOGIN|LOGOUT): \S+ logged (?:in|out) from ([\d.]+)\.$/.exec(text);
  if(loop&&Number(loop[2])>=1&&Number(loop[2])<=4094)events.push({...row,target,requestId,switchAt:at,kind:'loop',port:loop[1],vlan:Number(loop[2])});
  else if(session)events.push({...row,target,requestId,switchAt:at,kind:session[1],ip:session[2]});
 }
 const groups=[];
 for(const loop of events.filter(e=>e.kind==='loop')){
  let group=groups.find(g=>g.boot===loop.boot&&g.target===loop.target&&g.vlan===loop.vlan&&Math.abs(g.switchAt-loop.switchAt)<=1000);
  if(!group){group={boot:loop.boot,target:loop.target,vlan:loop.vlan,switchAt:loop.switchAt,ports:[],loops:[]};groups.push(group);}
  if(!group.ports.includes(loop.port))group.ports.push(loop.port);
  group.loops.push(loop);
 }
 return groups.slice(-16).map(group=>{
  let match=null;
  for(const loop of group.loops){
   for(const login of events.filter(e=>e.kind==='LOGIN'&&e.boot===group.boot&&e.target===group.target&&e.requestId===loop.requestId)){
    for(const connected of connections.filter(c=>c.state==='connected'&&c.boot===group.boot&&c.target===group.target&&c.ip===login.ip&&c.at<=login.at&&login.at-c.at<=60000)){
     const estimate=loop.switchAt+connected.at-login.switchAt;
     const logout=events.find(e=>e.kind==='LOGOUT'&&e.boot===group.boot&&e.target===group.target&&e.ip===login.ip&&e.requestId===loop.requestId&&e.switchAt>=loop.switchAt&&e.switchAt-loop.switchAt<=60000);
     if(!logout||estimate<0||estimate>loop.at)continue;
     const disconnect=connections.find(c=>c.state==='disconnected'&&c.boot===group.boot&&c.target===group.target&&c.ip===login.ip&&Math.abs(c.at-(logout.switchAt+connected.at-login.switchAt))<=10000);
     const heartbeat=heartbeats.find(h=>h.boot===group.boot&&h.target===group.target&&h.at>=estimate&&h.at-estimate<=60000&&disconnect&&Math.abs(h.at-disconnect.at)<=15000);
     if(disconnect&&heartbeat){match={loop,login,connected,logout,disconnect,heartbeat,estimatedAt:estimate};break;}
    }
    if(match)break;
   }
   if(match)break;
  }
  return {boot:group.boot,target:group.target,vlan:group.vlan,ports:group.ports.sort(),
   state:match?'suspected':'loop_confirmed',historical:true,
   title:match?'疑似环路风暴':'已确认环路 · 风暴风险待核对',
   evidence:match?[...group.loops,match.logout,match.disconnect,match.heartbeat,match.login,match.connected]:group.loops,
   explanation:match?'交换机确认环路；经登录事件对齐的同一时段内，管理心跳丢失且 SSH 断开。缺少广播速率与丢包计数，尚不能确认风暴或唯一根因。':'交换机日志明确记录过环路，但当前分析范围缺少可关联的管理通信中断证据，不能据此认定风暴。',
   limitation:'这是已保存日志的历史标记，不代表当前仍有风暴，也不证明 ESP 已隔离端口或物理环路已修复。'};
 });
}
