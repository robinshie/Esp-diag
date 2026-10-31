import {parseFields} from './telemetry.js';
export const LOOP_PHASES = [
  ['detected','发现环路'],['assessed','BDI 判断'],['isolating','端口隔离'],['verifying','恢复验证'],['resolved','处理完成']
];
const loopFaults=new Set(['LOOP','LOOP_DETECTED','LOOPBACK','L2_LOOP']);
const loopActions=new Set(['ISOLATE_LOOP_PORT','ISOLATE_LOOP']);

// Only explicit loop evidence is accepted. Generic LINK_DOWN / interface recovery is unrelated.
export function loopIncidents(reports) {
 const incidents=new Map(), requests=new Map(), seen=new Set();
 const lines=reports.flatMap(report=>report.lines.map((line,index)=>({...line,report:report.report_id,index})))
  .sort((a,b)=>a.at_ms-b.at_ms);
 for(const line of lines){
  const f=parseFields(line.text), source=f.device, port=f.port||f.component;
  if(!source||!port)continue;
  const duplicate=`${line.at_ms}/${line.text}`;if(seen.has(duplicate))continue;seen.add(duplicate);
  const detected=f.event==='LOOP_DETECTED'||(f.event==='FAULT'&&loopFaults.has(f.fault));
  const explicit=loopFaults.has(f.fault)||f.event==='LOOP_DETECTED'||f.event==='LOOP_RECOVERED';
  const action=loopActions.has(f.action)||/^\[BDI\]\[CMD\] ISOLATE_LOOP(?:_PORT)?\b/.test(line.text);
  const requested=f.request_id&&requests.get(f.request_id);
  // Recovery / ACK without a matching incident or request cannot resolve a fault.
  const key=f.incident?`${source}/${port}/${f.incident}`:requested;
  let incident=key&&incidents.get(key);
  if(requested && (requested!==key || !incident || incident.source!==source || incident.port!==port))continue;
  if(!incident){
   if(!explicit&&!action)continue;
   if(!detected && !f.incident)continue;
   const id=key||`${source}/${port}/unidentified-${line.at_ms}`;
   incident={id,source,port,incident:f.incident||'未提供事件 ID',phase:'detected',uncertain:!detected,request:null,timeline:[],lastAt:line.at_ms};
   incidents.set(id,incident);
  }
  if(!explicit&&!action&&!requested)continue;
  if(incident.phase==='resolved')continue;
  let phase,label;
  if(detected){phase='detected';label='日志报告环路';}
  else if(line.text.startsWith('[BDI][LOOP]')&&f.stage==='ASSESS'){phase='assessed';label=f.allowed==='true'?'BDI 判定允许隔离':'BDI 保护规则阻止自动隔离';if(f.allowed!=='true')phase='blocked';}
  else if(line.text.startsWith('[BDI][CMD]')&&action){
   if(!f.request_id)continue;
   incident.request=f.request_id;requests.set(f.request_id,incident.id);phase='isolating';label='已请求隔离端口，等待回执';
  }
  else if(line.text.startsWith('[BDI][ACK]')&&requested&&f.request_id===incident.request){
   if(f.result==='SUCCESS'||f.result==='COMMAND_ACCEPTED'){phase='verifying';label='隔离命令收到成功回执，等待恢复证据';}
   else if(f.result==='FAILED'||f.result==='ATTEMPT_FAILED'){phase='failed';label='隔离执行失败 / 超时，需人工检查';}
  }
  else if((line.text.startsWith('[BDI][RECOVERED]')||f.event==='LOOP_RECOVERED')&&explicit&&f.verified==='true'&&f.incident){phase='resolved';label='环路恢复已验证';}
  if(!phase)continue;
  // Replayed detection must not roll a completed incident back to the beginning.
  if(phase==='detected'&&incident.timeline.length)continue;
  incident.phase=phase;incident.lastAt=line.at_ms;
  incident.timeline.push({phase,label,at:line.at_ms,text:line.text,request:f.request_id||null});
 }
 return [...incidents.values()].filter(item=>item.timeline.length).sort((a,b)=>b.lastAt-a.lastAt);
}

export function demoLoopReports(now=Date.now()) {
 const common='device=switch-demo port=GigabitEthernet1/0/8 component=GigabitEthernet1/0/8 fault=LOOP incident=demo-loop-1';
 const texts=[
  `H3C LOG ${common} event=LOOP_DETECTED`,
  `[BDI][LOOP] ${common} stage=ASSESS allowed=true reason=ALLOWLIST`,
  `[BDI][CMD] ISOLATE_LOOP_PORT ${common} action=ISOLATE_LOOP_PORT request_id=demo-req-1`,
  `[BDI][ACK] ${common} action=ISOLATE_LOOP_PORT request_id=demo-req-1 result=COMMAND_ACCEPTED`,
  `[BDI][RECOVERED] ${common} verified=true`
 ];
 return [{report_id:'demo-only',lines:texts.map((text,i)=>({text,at_ms:now-20000+i*4000}))}];
}
