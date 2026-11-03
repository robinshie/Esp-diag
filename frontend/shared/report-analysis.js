import {normalizeRecords, explainRecords, faultRule} from './fault-explainer.js';
import {modelDescriptions} from './model-descriptions.js';
import {assessLoopStorms} from './loop-storm.js';

const faultLabels={AUTH_LOCKED:'auth_lock',LINK_DOWN:'link_down',THERMAL:'thermal',FAN_FAILURE:'fan',PSU_FAILURE:'power',
 LOOP:'loop',LOOP_DETECTED:'loop',LOGIN_FAILURE:'login_failure',STP_PROTECTION:'stp_protection',STP_CHANGE:'stp_protection',
 MAC_MOVE:'mac_move',MAC_FLAPPING:'mac_move',CPU_HIGH:'resource_pressure',CPU_USAGE_HIGH:'resource_pressure',MEMORY_HIGH:'resource_pressure'};
const phases={detected:'发现告警',reported:'来源上报告警',requested:'请求处置',acknowledged:'收到处置回执',
 recovery_reported:'来源报告恢复',recovered:'固件确认恢复',contained:'隔离已验证',verifying:'正在验证',blocked:'自动动作已阻止',planned:'形成处置意图',observation_unknown:'采集证据不足'};
const routinePhases=new Set(['snapshot','runtime','receipt','heartbeat','belief','decision','connection','boot','storage','network','collection']);
export function buildReportAnalysis(report, contextReports=[], options={}) {
 const recent=options.recent===true;
 const reports=[...contextReports.filter(value=>value.report_id!==report.report_id),report]
  .filter(value=>(recent||value.boot===report.boot)&&value.sent_at_ms<=report.sent_at_ms&&
   ((report.clock==='uptime'||value.clock==='uptime')?value.clock===report.clock&&value.boot===report.boot:true)).sort((a,b)=>a.sent_at_ms-b.sent_at_ms);
 const records=[];
 for(const packet of reports)packet.lines.forEach((line,index)=>records.push({boot_id:packet.boot,...(packet.clock==='uptime'?{uptime_ms:line.at_ms}:{unix_ms:line.at_ms}),
  text:line.text,report_id:packet.report_id,line_index:index,current:packet.report_id===report.report_id}));
 records.sort((a,b)=>(a.uptime_ms??a.unix_ms)-(b.uptime_ms??b.unix_ms));
 const items=normalizeRecords(records),verdicts=explainRecords(records),groups=new Map(),routines=[],unknown=[];
 items.forEach((item,index)=>{
  item.verdict=verdicts[index];item.evidence=records[index];
  if(routinePhases.has(item.phase)){routines.push(item);return;}
  if(!phases[item.phase]&&!item.fault){unknown.push(item);return;}
  // Never merge different incidents or guess an ACK's missing incident fields.
  const complete=item.boot&&item.device&&item.component&&item.fault&&item.incident;
  const key=complete?JSON.stringify([item.boot,item.device,item.component,item.fault,item.incident]):'unlinked:'+index;
  if(!groups.has(key))groups.set(key,{key,items:[],boot:item.boot,device:item.device,component:item.component,fault:item.fault,incident:item.incident,linked:Boolean(complete)});
  groups.get(key).items.push(item);
 });
 const incidents=[...groups.values()].filter(group=>(recent||group.items.some(item=>item.evidence.current))).map(group=>{
  const first=group.items[0],last=group.items.at(-1);
  const recovered=group.linked&&group.items.some(item=>item.phase==='recovered');
  const failed=group.items.some(item=>item.phase==='acknowledged'&&['FAILED','ATTEMPT_FAILED'].includes(item.result));
  const label=faultLabels[group.fault],description=modelDescriptions[label];
  const title=description?.[0]||((group.fault&&first.verdict.known)?first.verdict.title:group.fault||first.verdict.title||'未关联的处置记录');
  const state=last.phase==='contained'?'已验证隔离，物理环路仍待处理':last.phase==='blocked'?'动作已阻止，待核对':last.phase==='observation_unknown'?'证据不足，等待采集':last.phase==='verifying'?'处置后验证中':last.phase==='planned'?'已形成意图，等待执行':recovered?'已确认恢复':last.phase==='recovered'?'已记录恢复，关联不完整':last.phase==='acknowledged'&&['FAILED','ATTEMPT_FAILED'].includes(last.result)?'处置失败，待核对':
   group.items.some(item=>item.phase==='acknowledged')?'等待恢复验证':group.items.some(item=>item.phase==='requested')?'已请求处置':'待核对';
  const anchor=group.items.find(item=>item.phase==='detected'||item.phase==='reported')||group.items.find(item=>item.phase==='requested')||first;
  const related=routines.filter(item=>item.phase==='snapshot'&&item.boot===first.boot&&item.device===group.device&&item.time<=last.time&&
   typeof item.fields.faults==='string'&&item.fields.faults.split(',').includes(group.component+':'+group.fault)).slice(-2);
  const timeline=group.items.map(item=>({time:item.time,timeBase:item.timeBase,phase:phases[item.phase]||'故障证据',detail:item.phase==='acknowledged'?
    '回执 '+(item.result||'结果缺失')+(item.attempt?' · 第 '+item.attempt+' 次':''):item.phase==='requested'?
    (item.action||'处置动作')+(item.attempt?' · 第 '+item.attempt+' 次':''):item.verdict.progress,
    text:item.evidence.text,reportId:item.evidence.report_id,line:item.evidence.line_index}));
  for(const item of related)timeline.push({time:item.time,timeBase:item.timeBase,phase:'同对象告警快照（未绑定事件）',detail:'快照中设备、组件和故障类型一致，但没有事件标识；仅作背景证据，不用于确认本事件的起点或处置结果。',text:item.evidence.text,reportId:item.evidence.report_id,line:item.evidence.line_index});
  timeline.sort((a,b)=>a.time-b.time);
  const rule=faultRule(group.fault);
  return {...group,items:[...group.items,...related],title,state,recovered,failed,anchor,meaning:rule?.meaning||description?.[1]||first.verdict.meaning,
   causes:rule?.causes||first.verdict.causes,steps:rule?.steps||first.verdict.steps,firstAt:first.time,lastAt:last.time,timeline};
 });
 const snapshots=new Map();
 for(const item of routines)if(item.phase==='snapshot'&&(recent||item.evidence.current))snapshots.set(item.device,item.fields);
 for(const observation of report.observations||[])snapshots.set(observation.device,observation);
 for(const [device,fields] of snapshots){
  if(!(Number(fields.active_faults)>0)||typeof fields.faults!=='string')continue;
  for(const token of fields.faults.split(',').slice(0,128)){
   const match=/^([^,:]+):([A-Z][A-Z0-9_]*)$/.exec(token);if(!match)continue;
   const [,component,fault]=match;
   if(incidents.some(item=>item.device===device&&item.component===component&&item.fault===fault&&!item.recovered))continue;
   const rule=faultRule(fault),description=modelDescriptions[faultLabels[fault]];
   const source=[...routines].reverse().find(item=>item.phase==='snapshot'&&item.device===device&&(recent||item.evidence.current));
   incidents.push({key:'snapshot:'+device+':'+component+':'+fault,boot:source?.boot||report.boot,device,component,fault,incident:'',linked:false,snapshotOnly:true,
    title:description?.[0]||rule?.title||fault,state:'期末仍跟踪告警',recovered:false,failed:false,
    meaning:rule?.meaning||description?.[1]||'固件期末快照仍跟踪这个故障，当前没有对应的解释规则。',
    causes:rule?.causes||[],steps:rule?.steps||['核对该设备原始告警、具体端口或组件及前后记录。'],
    items:source?[source]:[],anchor:source||null,timeline:source?[{time:source.time,timeBase:source.timeBase,phase:'期末状态快照',
     detail:'仍跟踪此故障；快照缺少事件标识，未将其他事件的处置记录并入。',text:source.evidence.text,
     reportId:source.evidence.report_id,line:source.evidence.line_index}]:[]});
  }
 }
 // A later normal snapshot must not erase an earlier alarm from a requested
 // recent-log analysis. Missing incident IDs still cannot prove recovery.
 if(recent)for(const source of [...routines].reverse()){
  if(source.phase!=='snapshot'||source.evidence.current||!(Number(source.fields.active_faults)>0)||typeof source.fields.faults!=='string')continue;
  for(const token of source.fields.faults.split(',').slice(0,128)){
   const match=/^([^,:]+):([A-Z][A-Z0-9_]*)$/.exec(token);if(!match)continue;
   const [,component,fault]=match,device=source.device;
   if(incidents.some(item=>item.boot===source.boot&&item.device===device&&item.component===component&&item.fault===fault))continue;
   const rule=faultRule(fault),description=modelDescriptions[faultLabels[fault]];
   incidents.push({key:JSON.stringify(['past-snapshot',source.boot,device,component,fault]),boot:source.boot,device,component,fault,incident:'',linked:false,snapshotOnly:true,
    title:description?.[0]||rule?.title||fault,state:'历史告警快照，待核对后续结果',recovered:false,failed:false,
    meaning:(rule?.meaning||description?.[1]||'历史快照记录过此告警。')+' 这条快照没有事件标识，后续正常状态不能单独证明同一事件已恢复。',
    causes:rule?.causes||[],steps:rule?.steps||['核对该设备、端口或组件的告警原文及对应恢复记录。'],items:[source],anchor:source,
    timeline:[{time:source.time,timeBase:source.timeBase,phase:'历史告警快照（未绑定事件）',detail:'本时段曾跟踪此故障；仅说明当时状态，未据此判断当前仍有故障或已确认恢复。',text:source.evidence.text,reportId:source.evidence.report_id,line:source.evidence.line_index}]});
  }
 }
 const diagnostics=[];
 const seenDiagnostics=new Set();
 for(const item of routines.filter(value=>recent||value.evidence.current)){
  let message='';
  if(item.phase==='collection')message='命令采集未完整完成：'+(item.fields.command||'未知命令')+' / '+(item.fields.state||'未知状态')+(item.fields.truncated==='1'?'（已截断）':'')+'；不用于动作或恢复判断。';
  if(item.phase==='receipt'&&item.result==='REJECTED')message='输入帧被拒绝：需要核对输入格式和序号；这不是处置失败回执。';
  if(item.phase==='runtime'&&item.fields.sd_state&&item.fields.sd_state!=='ready')message='期间 TF 卡状态为 '+item.fields.sd_state+'，需核对后续是否回到 ready。';
  if(item.phase==='storage'&&(Number(item.fields.dropped)>0||(item.fields.state&&item.fields.state!=='ready')))message='存储记录显示 '+(item.fields.state||'未知状态')+'，未保存计数 '+(item.fields.dropped||'未提供')+'。';
  if(item.phase==='network'&&item.fields.link==='down')message='ESP 网口物理链路未连接；请核对线缆和接入端口，不能据此认定交换机故障。';
  if(message&&!seenDiagnostics.has(message)){seenDiagnostics.add(message);diagnostics.push(message);}
 }
 return {report,incidents,routines:routines.filter(item=>recent||item.evidence.current),unknown:unknown.filter(item=>recent||item.evidence.current),
  diagnostics,loopRisks:assessLoopStorms(reports),
  snapshots:[...snapshots].map(([device,fields])=>({device,...fields})),contextPackets:reports.length,
  simulated:items.some(item=>/^sim(?:[-_]|$)|^switch-demo(?:[-_]|$)/i.test(item.device)),
  rawCount:recent?reports.reduce((count,packet)=>count+packet.lines.length,0):report.lines.length};
}
