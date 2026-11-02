// Generated from frontend/device/archive.html by tools/embed_network_page.py
#pragma once
static const char ARCHIVE_PAGE[] PROGMEM = R"ESP_PAGE(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>故障分析 · 离线助手</title>
<style>
body{font:16px/1.6 system-ui,sans-serif;color:#183047;background:#eef3f7;margin:0}main{max-width:960px;margin:auto;padding:24px 16px}section,details{background:white;border:1px solid #d6e0e8;border-radius:10px;padding:16px;margin:12px 0}h1{font-size:26px;margin-bottom:6px}button,select{font:inherit;padding:9px;border:1px solid #9fb6c6;border-radius:7px;color:inherit;background:white;margin:5px}select{max-width:100%}button{cursor:pointer}button:disabled{opacity:.5}summary{cursor:pointer;font-weight:600}pre{white-space:pre-wrap;overflow-wrap:anywhere;font-size:13px}p{overflow-wrap:anywhere}.muted{color:#526b7f}#analyze{display:block;width:100%;margin:16px 0;padding:16px;background:#155e82;color:white;border:0;font-size:18px;font-weight:700}#status{margin:12px 0}#status[data-error=true]{color:#a32e22}.report-analysis{--analysis-bg:#f2f7fa;--analysis-border:#d6e0e8;--analysis-muted:#526b7f}
</style></head><body><main>
<h1>故障分析</h1><p class="muted">查看故障现象、可能原因和处置过程。离线小模型辅助分类，原文证据随时可查。</p>
<section aria-label="设备当前状态"><h2>设备独立运行</h2><p id="device-ai">正在读取本机 AI 状态…</p><p id="device-bdi">正在读取 BDI 状态…</p><p id="device-discovery">正在读取环路探索状态…</p><p id="device-link">正在读取交换机连接…</p><p id="device-mqtt">正在读取可选后台连接…</p><small class="muted">本区显示当前设备状态；下方分析只依据已保存日志。环路探索综合 MAC 漂移、端口、VLAN、STP 和 CPU，疑似线索仅供观察，不代表环路已经确认。管理连接受保护，业务口处置须通过后续采集确认结果。</small></section>
<button id="analyze" type="button">一键分析最近日志</button><p class="muted">读取最近最多 10 包、10 分钟内的已保存日志，调用 ESP 离线小模型，汇总故障和处置过程。</p><p id="status" role="status" aria-live="polite">正在加载最近一次记录…</p>
<div id="report"></div>
<details id="history"><summary>其他时间的记录与存储信息</summary><p id="storage" class="muted">正在检查 TF 卡…</p><button id="refresh">刷新最新记录</button><button id="older" disabled>更早记录</button><button id="newer" disabled>返回较新记录</button><p><select id="file" aria-label="选择历史记录"><option value="">暂无记录</option></select></p><p class="muted">正式版记录由 ESP 本机采集并直接写入 TF 卡，无需电脑或 USB。约 12 MiB 循环保留，空间不足覆盖最早记录。历史记录不代表当前状态。</p></details>
<p class="muted">连按三下 BOOT可再次打开；查看入口十分钟后隐藏。</p><button id="close">关闭查看入口</button>
<script type="module">
const {renderReportAI}=(()=>{
// Offline, deterministic explanations. This module never sends data or executes actions.
const rules={
 AUTH_LOCKED:{title:'管理账户被锁定',meaning:'来源报告账户触发锁定，可能暂时无法登录管理界面。',causes:['重复登录失败、客户端仍使用旧密码，或账户锁定策略。'],steps:['核对登录失败记录、账户和锁定策略。','检查自动登录客户端是否保存旧密码；确认原因后再由管理员解锁。']},
 LINK_DOWN:{title:'端口链路中断',meaning:'来源报告端口链路不可用，可能影响经过该端口的通信。',causes:['线缆或光模块异常、对端供电中断、端口配置或保护策略。'],steps:['核对具体端口、指示灯、线缆或光模块和对端供电。','查看端口状态、错误计数与配置；链路中断本身不能证明存在环路。']},
 THERMAL:{title:'温度告警',meaning:'来源报告温度异常，需要核对实际温度与设备告警阈值。',causes:['环境温度偏高、散热通道堵塞、风扇异常、负载升高或传感器异常。'],steps:['核对温度传感器读数、告警阈值、风扇与通风情况。','观察温度和告警是否持续；不能仅凭一条告警认定设备已损坏。']},
 LOOP:{title:'来源报告二层环路',meaning:'日志明确报告环路类事件，仍需要核对拓扑和交换机证据。',causes:['重复接线、桥接拓扑或生成树保护未按预期工作。'],steps:['核对告警端口、拓扑、生成树状态、MAC 漂移和广播计数。','核对 ESP 的业务口处置策略与管理路径保护；自动隔离后仍需排除实际接线环路，不能仅凭命令回执确认恢复。']},
 PSU_FAILURE:{title:'电源模块告警',meaning:'来源报告电源模块异常，不代表整台设备已经断电。',causes:['供电输入、电源模块、冗余状态或检测信号异常。'],steps:['核对电源模块指示灯、供电输入和冗余状态。','对照设备告警记录确认影响范围。']},
 FAN_FAILURE:{title:'风扇告警',meaning:'来源报告风扇异常，需要核对转速和散热状态。',causes:['风扇停转、转速不足、接触或检测信号异常。'],steps:['核对风扇转速与温度，检查通风情况。','按设备维护要求安排检查或更换。']},
 CPU_HIGH:{title:'CPU 使用率告警',meaning:'来源报告 CPU 使用率超过告警阈值；原文未提供数值时不能估算占用百分比。',causes:['需要核对进程占用、当时负载及告警阈值，单条告警不能证明环路或硬件故障。'],steps:['查看实际 CPU 与各进程占用，并核对告警和恢复阈值。','比较告警持续时间、恢复记录及同期流量；按具体型号和版本手册检查。']},
 MEMORY_HIGH:{title:'内存使用告警',meaning:'来源报告内存资源越过告警阈值；需要核对实际可用内存与持续时间。',causes:['日志尚未指明占用来源，不能仅凭告警判断内存泄漏或硬件损坏。'],steps:['核对实际内存、进程资源占用和配置阈值。','对照告警前后的趋势与恢复记录。']},
 LOGIN_FAILURE:{title:'登录或认证失败',meaning:'来源报告登录或认证失败；失败本身不等于账户被锁定。',causes:['需核对日志原因、账户、认证服务和管理连接，不能直接断言密码错误。'],steps:['核对认证失败原因与对应账户、服务类型。','检查认证服务器与管理路径，并查看是否存在独立锁定记录。']},
 STP_PROTECTION:{title:'生成树保护事件',meaning:'来源报告生成树保护相关事件，需要区分 BPDU 缺失、异常或根桥变化。',causes:['保护事件不等于已经确认发生二层环路，需核对具体消息与拓扑。'],steps:['核对告警端口、原始消息和生成树状态。','结合拓扑与 MAC 迁移记录检查，确认管理路径后再处理。']},
 MAC_MOVE:{title:'MAC 地址迁移',meaning:'来源报告 MAC 学习位置变化，需要核对 VLAN、原端口、新端口与次数。',causes:['可能是预期位置变化；反复迁移需进一步核对拓扑，不能凭单条记录确定环路。'],steps:['核对 MAC 地址、VLAN、原端口、新端口和迁移次数。','对照实际接线与业务变更，结合生成树或明确环路告警核验。']}
};
const aliases={LOOP_DETECTED:'LOOP',LOOPBACK:'LOOP',L2_LOOP:'LOOP',CPU_USAGE_HIGH:'CPU_HIGH',STP_CHANGE:'STP_PROTECTION',MAC_FLAPPING:'MAC_MOVE'};
function faultRule(fault){return rules[aliases[fault]||fault]||null;}
const value=input=>typeof input==='string'?input.slice(0,160):'';
function fields(text){
 const result=Object.create(null);
 for(const token of text.split(/\s+/)){const at=token.indexOf('=');if(at<=0)continue;const key=token.slice(0,at);if(Object.prototype.hasOwnProperty.call(result,key))return null;result[key]=token.slice(at+1);}
 return result;
}
function normalizeRecord(input){
 const record=input&&typeof input==='object'&&!Array.isArray(input)?input:{};
 const text=value(record.text); // Full frames below are bounded separately, not truncated before parsing.
 const raw=typeof record.text==='string'?record.text.slice(0,2048):'';
 const accepted=/^(?:H3C (?:LOG|PONG) |\[BDI\]\[(?:CMD|ACK|RECEIPT|RECOVERED|STATE|ACTION|EVENT|WARN|STATUS|BOOT|READY|HEARTBEAT|OBSERVE|VERIFY|H3C|BLOCKED|COLLECT)\]|\[(?:STORAGE|NETWORK|SSH|BOOT|MEMORY|LOG-AI)\])/.test(raw);
 const f=accepted?fields(raw):Object.create(null);
 const get=key=>value(f?.[key]);
 const rawEvent=get('event'),event=value(record.event)||rawEvent||(/^\[BDI\]\[BOOT\]/.test(raw)?'BOOT':'');
 let phase='unknown';
 if(event==='DETECTED')phase='detected';
 else if(event==='RECOVERED')phase='recovered';
 else if(/^\[BDI\]\[OBSERVE\]/.test(raw))phase=get('state')==='loop_detected'?'detected':get('state')==='unknown'?'observation_unknown':'snapshot';
 else if(/^\[BDI\]\[VERIFY\]/.test(raw))phase=get('state')==='contained'?'contained':get('state')==='needs_review'?'blocked':'verifying';
 else if(/^\[BDI\]\[H3C\]\[BELIEF\]/.test(raw))phase='belief';
 else if(/^\[BDI\]\[H3C\]\[INTENTION\]/.test(raw))phase='planned';
 else if(/^\[BDI\](?:\[H3C\])?\[BLOCKED\]/.test(raw))phase='blocked';
 else if(/^\[BDI\]\[COLLECT\]/.test(raw))phase='collection';
 else if(/^\[BDI\]\[RECOVERED\]/.test(raw))phase='recovered';
 else if(/^\[BDI\]\[CMD\]/.test(raw))phase='requested';
 else if(/^\[BDI\]\[ACK\]/.test(raw))phase='acknowledged';
 else if(/^H3C LOG /.test(raw)&&['RECOVERY','LINK_UP'].includes(rawEvent)&&get('status')==='OK')phase='recovery_reported';
 else if(/^H3C LOG /.test(raw)&&(get('status')==='ALARM'||(rawEvent==='AUTH_LOCKED'&&get('status')==='LOCKED')))phase='reported';
 else if(/^H3C LOG /.test(raw)&&rawEvent==='HEARTBEAT'&&get('status')==='OK')phase='heartbeat';
 else if(/^\[BDI\]\[STATE\]/.test(raw))phase='snapshot';
 else if(/^\[BDI\]\[STATUS\]/.test(raw))phase='runtime';
 else if(/^\[BDI\]\[HEARTBEAT\]/.test(raw))phase='heartbeat';
 else if(/^\[SSH\]/.test(raw))phase='connection';
 else if(/^\[BOOT\]/.test(raw))phase='network';
 else if(/^\[(?:MEMORY|LOG-AI)\]/.test(raw))phase='runtime';
 else if(/^\[BDI\]\[RECEIPT\]/.test(raw))phase='receipt';
 else if(/^\[BDI\]\[EVENT\]/.test(raw)&&get('belief'))phase='belief';
 else if(/^\[BDI\]\[ACTION\]/.test(raw)&&get('decision'))phase='decision';
 else if(/^(?:H3C PONG |\[BDI\]\[READY\])/.test(raw))phase='connection';
 else if(/^\[STORAGE\]/.test(raw))phase='storage';
 else if(/^\[NETWORK\]/.test(raw))phase='network';
 else if(event)phase=event.toLowerCase();
 let fault=value(record.fault)||get('fault');
 if(!fault&&/^\[BDI\]\[OBSERVE\]/.test(raw)&&get('state')==='loop_detected')fault='LOOP';
 if(!fault&&(['reported','detected'].includes(phase)||rules[aliases[rawEvent]||rawEvent]))fault=rawEvent;
 const action=get('action')||value(raw.match(/^\[BDI\]\[CMD\]\s+(\S+)/)?.[1]);
 const validTime=n=>typeof n==='number'&&Number.isFinite(n)&&n>=0;
 const timeBase=validTime(record.uptime_ms)?'uptime':validTime(record.unix_ms)?'unix':null;
 const time=timeBase==='uptime'?record.uptime_ms:timeBase==='unix'?record.unix_ms:null;
 if(record.truncated===true&&phase==='recovered')phase='recovery_reported';
 return {record,text,event,fields:f||{},phase:f===null?'ambiguous':phase,fault,rule:aliases[fault]||fault,boot:value(record.boot_id),device:value(record.device)||get('device'),component:value(record.component)||get('component')||get('port'),incident:value(record.incident)||get('incident'),cause:value(record.reported_cause)||get('cause'),request:get('request_id'),result:get('result')||(/^\[BDI\]\[ACK\]/.test(raw)?get('state'):''),action,attempt:get('attempt'),temp:get('temp_c'),severity:value(record.severity)||get('level'),snapshot:get('state'),time,timeBase};
}
function complete(item){return Boolean(item.boot&&item.device&&item.component&&item.fault&&item.incident);}
function sameIncident(a,b){return complete(a)&&complete(b)&&['boot','device','component','fault','incident'].every(key=>a[key]===b[key]);}
function correlate(items){
 for(const item of items){
  if(item.phase!=='acknowledged'||!item.boot||!item.request||!item.device||!item.component)continue;
  const commands=items.filter(other=>other.phase==='requested'&&other.boot===item.boot&&other.request===item.request&&other.device===item.device&&other.component===item.component&&other.action===item.action&&complete(other)&&other.time!==null&&item.time!==null&&other.timeBase===item.timeBase&&other.time<=item.time);
  if(commands.length&&commands.every(command=>sameIncident(command,commands[0]))){
   item.fault=commands[0].fault;item.rule=commands[0].rule;item.incident=commands[0].incident;
  }
 }
 return items;
}
function explain(item,items){
 const rule=rules[item.rule];
 const info={title:rule?.title||'证据解释',meaning:rule?.meaning||'这条记录尚不足以解释具体故障。',evidence:[],causes:rule?[...rule.causes]:[],steps:rule?[...rule.steps]:['展开完整记录，核对设备、端口、事件类型和前后日志。'],progress:'该片段尚未提供对应故障的已验证恢复证据。',limits:['仅解释当前已加载片段及记录当时的情况，不代表设备当前实时状态。'],known:Boolean(rule),recovered:item.phase==='recovered',phase:item.phase};
 for(const [label,key] of [['设备','device'],['组件 / 端口','component'],['故障类型','fault'],['事件标识','incident'],['请求标识','request']])if(item[key])info.evidence.push(label+'：'+item[key]);
 if(item.cause)info.evidence.push('来源上报原因（未独立验证）：'+item.cause);
 if(item.severity)info.evidence.push('来源告警级别：'+item.severity);
 if(item.temp&&Number.isFinite(Number(item.temp)))info.evidence.push('日志温度读数：'+item.temp+' °C');
 if(item.rule==='LINK_DOWN'&&item.cause==='ADMIN_DOWN'){
  info.meaning='来源将端口中断归因为管理关闭，需要核对是谁或哪项策略关闭了端口。';
  info.causes=['人工维护、配置变更，或保护策略主动关闭端口。'];
  info.steps=['先核对维护记录、端口配置和保护告警。','确认关闭原因与管理路径后再决定是否恢复；不要直接恢复因环路等保护而关闭的端口。'];
 }
 if(!rule&&item.fault){info.title='未识别的故障类型';info.meaning='已看到故障字段 '+item.fault+'，当前没有匹配的解释规则。';info.limits.push('不根据未知事件猜测根因或修复命令。');}
 if(item.phase==='requested'){
  info.title='已发出处置请求'+(rule?'：'+rule.title:'');
  info.progress='固件记录已请求 '+(item.action||'处置动作')+'，尚不代表执行成功。'+(Number(item.attempt)>1?'这是第 '+item.attempt+' 次请求。':'');
 }
 if(item.phase==='acknowledged'){
  info.title='处置回执'+(rule?'：'+rule.title:'');
  info.evidence.push('回执结果：'+(item.result||'未提供'));
  info.progress=['FAILED','ATTEMPT_FAILED'].includes(item.result)?'回执报告失败或超时；应核对执行结果、管理连接与后续日志。':item.result==='COMMAND_ACCEPTED'?'命令已被接受，仍在等待故障恢复验证。':['SUCCESS','ok'].includes(item.result)?'处置流程返回命令成功；单凭这一回执仍不能认定故障恢复。':'回执含义未识别，不能据此判断成功或恢复。';
  if(!complete(item))info.limits.push('当前片段缺少匹配的命令或事件标识，无法将回执关联到具体故障。');
 }
 if(item.fields.reason)info.evidence.push('判断依据：'+item.fields.reason);
 if(item.phase==='collection'){
  info.known=true;info.title='交换机命令采集未完整完成';info.meaning='采集命令 '+(item.fields.command||'未提供')+' 返回 '+(item.fields.state||'未知')+(item.fields.truncated==='1'?'，输出已截断':'')+'。';info.progress='本条不能作为完整状态或故障恢复证据。';info.steps=['核对 SSH 状态、命令原文与后续完整采集周期。'];
 }
 if(item.phase==='contained'){
  info.known=true;info.title='端口隔离已验证，物理环路仍待处理';info.meaning='ESP 已通过后续采集验证该端口的隔离状态。隔离限制了影响，并未证明重复接线或物理环路已经消除。';
  info.progress='已验证隔离，尚未确认故障恢复。';info.recovered=false;info.causes=[];info.steps=['核对隔离端口与现场拓扑，处理实际环路或错误接线。','保留管理路径保护；后续恢复须结合独立采集与稳定性验证。'];
 }
 if(item.phase==='verifying'){
  info.known=true;info.title='正在核对处置后的交换机状态';info.meaning='ESP 正在读取处置后的现场证据，当前阶段尚不能认定恢复。';info.progress='验证状态：'+(item.fields.state||'未提供')+'。';
 }
 if(item.phase==='observation_unknown'){
  info.known=true;info.title='交换机采集证据不足';info.meaning='本轮观察不完整、已过期或来自不同采集周期，不能据此判断端口或环路状态。';info.progress='等待完整且新鲜的同轮观察，不等于业务正常。';info.steps=['核对 SSH 连接与命令原始返回，等待下一轮完整采集。'];
 }
 if(item.phase==='blocked'){
  info.known=true;info.title='BDI 已阻止自动动作';info.meaning='设备保护规则或证据条件未允许继续执行当前动作。';info.progress='该记录不代表动作成功，也不代表故障恢复。';info.steps=['核对判断依据、管理端口保护和采集是否完整，不要绕过保护直接关闭端口。'];
 }
 if(item.phase==='planned'){
  info.known=true;info.title='BDI 已形成处置意图';info.meaning='设备依据本轮信念选择了处置目标，后续仍须检查实际命令与独立验证。';info.progress='形成意图尚不等于已执行或已恢复。';
 }
 if(item.phase==='recovery_reported')info.progress='记录提及恢复，但这一条记录尚未提供完整的固件稳定性验证证据。';
 if(item.phase==='heartbeat'){
  info.known=true;info.title='收到来源心跳';info.meaning='日志记录来源当时仍能发送心跳。';info.progress='心跳到达不代表原有故障已恢复。';info.steps=['结合故障恢复记录核对状态。'];
 }
 if(item.phase==='snapshot'){
  info.known=true;info.title='设备状态快照';info.meaning='固件记录了该时刻的状态'+(item.snapshot?'：'+item.snapshot:'')+'。';info.progress='快照描述记录当时的模型状态，不保证当前状态。';info.steps=['结合前后告警、来源在线状态和恢复记录判断。'];
 }
 if(item.phase==='runtime'){
  info.known=true;info.title='ESP 运行状态';info.meaning='推理状态 '+(item.fields.reasoning_cycle||'未提供')+'，监测 '+(item.fields.devices||'未提供')+' 台，TF 卡状态 '+(item.fields.sd_state||'未提供')+'。';info.progress='这是运行指标，不是交换机故障告警。';info.steps=['需要排障时查看同一来源的告警与处置记录。'];
 }
 if(item.phase==='receipt'){
  info.known=true;info.title='输入收件回执';info.meaning='固件对输入帧返回 '+(item.result||'未提供结果')+'。';info.progress='ACCEPTED 只表示收到输入；DUPLICATE 表示重复帧；它们都不是修复成功。';info.steps=['如为 REJECTED，核对输入格式与序号；否则无需将其作为故障分析。'];
 }
 if(item.phase==='recovered'){
  info.title='记录当时已验证恢复'+(rule?'：'+rule.title:'');info.meaning='固件在该时刻记录了恢复稳定性验证完成；这不能保证现在仍正常。';info.progress='该条恢复记录已存在。';
  if(typeof item.record.duration_ms==='number'&&Number.isFinite(item.record.duration_ms)&&item.record.duration_ms>=0)info.evidence.push('模型跟踪时长：'+(item.record.duration_ms/1000).toFixed(1)+' 秒');
  info.steps=['核对后续是否出现同类告警；仅有历史恢复记录不能判断当前状态。'];
 }
 const special={
  SOURCE_LOST:['日志来源失联','固件记录来源超时或不可达，原因尚未确认。',['网线或管理路径、设备供电、采集进程或拥塞均可能影响日志到达。'],['依次核对物理链路、管理地址、来源心跳和采集进程。'],'失联不等于已确认交换机断电或出现环路。'],
  SOURCE_ONLINE:['日志来源通信恢复','固件再次观察到来源在线。',[],['查看该来源的故障恢复记录，不能只看心跳。'],'在线不代表原有故障已恢复。'],
  ETH_LINK_DOWN:['ESP 网口没有物理链路','固件记录 ESP 网口链路未连接或中断。',['未接线、线缆或对端端口异常、对端掉电等都有可能。'],['检查 ESP 网口与对端指示灯、线缆和对端供电。'],'单凭 link down 不能确认交换机宕机、SSH 失败或二层环路。'],
  ETH_LINK_UP:['ESP 网口物理链路已连接','固件观察到 ESP 网口链路接通。',[],['继续检查 IP 连通性和实际采集状态。'],'链路接通不代表 SSH 登录或 MQTT 连接成功。'],
  RECORD_GAP:['存在未保存记录','记录器报告部分日志没有写入 TF 卡。',['队列满、单条过长或写入失败等都可能造成缺口。'],['核对卡状态与未保存计数，并补查来源设备日志。'],'记录不完整，无法仅凭这些内容还原完整处置过程。'],
  SUMMARY_CAPACITY:['摘要跟踪容量已满','摘要未能继续跟踪全部活动故障。',[],['结合原始日志和处置过程检查；确认未纳入摘要的故障。'],'摘要不完整，不能用缺少恢复记录或缺少告警来判断完整状态。'],
  TRACKING_LOST:['故障跟踪中断','该故障的跟踪状态已丢失。',[],['查看前后开机标识、来源状态和原始日志。'],'跟踪中断不能认定为恢复。'],
  SUPERSEDED:['旧事件被新事件替代','固件用新一代故障替代了这条事件的跟踪。',[],['查看新事件与独立恢复记录。'],'旧事件的恢复情况尚未确认。'],
  BOOT:['ESP 启动记录','这是一个新的 ESP 开机周期。',[],['按开机标识区分记录；检查后续告警与日志来源。'],'不能将前一开机周期的命令或回执直接作为本周期开机后的恢复证据。']
 };
 const s=special[item.event];
 if(s){info.known=true;[info.title,info.meaning,info.causes,info.steps,info.progress]=s;}
 if(item.phase==='ambiguous'){
  info.known=false;info.title='日志字段存在歧义';info.meaning='这条日志包含重复字段，无法可靠解释。';info.progress='不据此判断故障或处置结果。';info.causes=[];info.steps=['核对原始日志中的重复字段和采集转换逻辑。'];
 }
 const recovery=items.find(other=>other!==item&&other.phase==='recovered'&&sameIncident(item,other)&&item.time!==null&&other.time!==null&&other.timeBase===item.timeBase&&other.time>=item.time);
 if(recovery&&item.phase!=='recovered'&&['detected','reported','requested','acknowledged','recovery_reported'].includes(item.phase)){
  info.recovered=true;info.progress+=' 当前片段后续还包含同一开机周期、设备、端口和事件的已验证恢复记录。';
 }
 if(/^sim(?:[-_]|$)|^switch-demo(?:[-_]|$)/i.test(item.device))info.limits.push('设备标识属于演示命名；请勿将演示记录作为真实交换机故障处置证据。');
 if(item.record.input_source==='usb_gateway')info.limits.push('日志经 USB 网关输入，需核对是实机采集还是演示数据。');
 if(item.record.truncated===true)info.limits.push('这条原始日志已被截断，解释可能缺少后续字段。');
 return info;
}
function normalizeRecords(records){return correlate(records.map(normalizeRecord));}
function explainRecords(records){
 const items=normalizeRecords(records);
 return items.map(item=>explain(item,items));
}
// Shared display taxonomy for database and ESP model output.
const modelDescriptions = {
  "auth_lock": [
    "认证或账户锁定",
    "日志涉及认证锁定。请核对具体账户、登录失败记录和锁定策略，尚不能确认是谁触发了锁定。"
  ],
  "link_down": [
    "链路中断",
    "日志涉及链路不可用。线缆、对端供电、端口配置或保护策略均需核对；这条日志不能证明存在环路。"
  ],
  "thermal": [
    "温度告警",
    "日志涉及温度异常。请核对温度读数、阈值、风扇和通风情况；根因尚未确定。"
  ],
  "loop": [
    "环路类告警",
    "日志可能涉及二层环路。请核对告警端口、拓扑、生成树和 MAC 漂移证据；这不代表环路已经排除。"
  ],
  "power": [
    "电源模块告警",
    "日志涉及电源模块异常。请核对供电输入、模块和冗余状态；不能据此认定整台交换机已断电。"
  ],
  "fan": [
    "风扇告警",
    "日志涉及风扇异常。请核对转速、温度和模块状态，确认是否影响散热。"
  ],
  "source_lost": [
    "日志来源失联",
    "日志涉及心跳超时或来源不可达。请检查设备供电、链路和采集进程；失联本身不能证明设备宕机。"
  ],
  "action_request": [
    "处置请求",
    "日志涉及已发出或正在派发的命令。发出请求尚不代表执行成功，更不能据此判断故障恢复。"
  ],
  "action_ack": [
    "处置回执",
    "日志涉及命令回执。请查看原文中的 SUCCESS、FAILED 或超时结果，并核对后续恢复证据；回执到达不等于恢复。"
  ],
  "recovery_report": [
    "恢复相关记录",
    "日志涉及恢复报告。是否已通过固件稳定性验证，请看本条记录的证据解释；历史恢复记录不代表当前状态。"
  ],
  "normal": [
    "运行或审计信息",
    "日志涉及启动、在线、登录或命令审计等信息。单条运行或审计日志不能证明全部业务正常、命令执行成功或原有故障已恢复。"
  ],
  "record_gap": [
    "记录或跟踪缺口",
    "日志可能涉及记录丢失、存储空间不足或跟踪不完整。请补查原始日志，不能从不完整片段还原全部过程。"
  ],
  "login_failure": [
    "登录或 AAA 请求失败",
    "日志涉及登录认证、授权失败或认证超时。请核对账号、服务类型、认证服务器连通性和日志原因；失败本身不能证明账户已被锁定，也不能直接认定密码错误。"
  ],
  "stp_protection": [
    "生成树保护或根桥变化",
    "日志涉及 STP 保护、BPDU 异常或根桥角色变化。请核对原文属于哪种事件，再检查上游 STP 状态和拓扑；LOOP_PROTECTION 缺少 BPDU 本身不能证明已经形成二层环路。"
  ],
  "mac_move": [
    "MAC 地址迁移",
    "日志涉及某个 VLAN 中的 MAC 学习位置改变。请核对迁移源端口、新端口、次数与拓扑，判断是否为预期迁移；单条 MAC_FLAPPING 不能证明存在环路。"
  ],
  "resource_pressure": [
    "CPU 或内存资源告警",
    "日志涉及 CPU、CPU 核心或内存使用率越过阈值。请核对实际占用、持续时间、进程及阈值；这一条日志不能直接确定是攻击、环路或硬件损坏。"
  ],
  "runtime_status": [
    "ESP 运行状态",
    "日志记录推理周期、设备数量、内存和存储状态，属于运行指标；不能据此认定交换机故障或全部业务正常。"
  ],
  "state_snapshot": [
    "设备状态快照",
    "日志记录固件当时跟踪的设备状态、故障数和锁定标记；请结合告警、回执与恢复记录核对，不代表现在仍是该状态。"
  ],
  "delivery_receipt": [
    "输入收件回执",
    "日志仅确认输入帧被接受、重复或拒绝。ACCEPTED 不是处置成功，也不是故障恢复；REJECTED 需检查输入协议。"
  ]
};

const faultLabels={AUTH_LOCKED:'auth_lock',LINK_DOWN:'link_down',THERMAL:'thermal',FAN_FAILURE:'fan',PSU_FAILURE:'power',
 LOOP:'loop',LOOP_DETECTED:'loop',LOGIN_FAILURE:'login_failure',STP_PROTECTION:'stp_protection',STP_CHANGE:'stp_protection',
 MAC_MOVE:'mac_move',MAC_FLAPPING:'mac_move',CPU_HIGH:'resource_pressure',CPU_USAGE_HIGH:'resource_pressure',MEMORY_HIGH:'resource_pressure'};
const phases={detected:'发现告警',reported:'来源上报告警',requested:'请求处置',acknowledged:'收到处置回执',
 recovery_reported:'来源报告恢复',recovered:'固件确认恢复',contained:'隔离已验证',verifying:'正在验证',blocked:'自动动作已阻止',planned:'形成处置意图',observation_unknown:'采集证据不足'};
const routinePhases=new Set(['snapshot','runtime','receipt','heartbeat','belief','decision','connection','boot','storage','network','collection']);
function buildReportAnalysis(report, contextReports=[], options={}) {
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
  diagnostics,
  snapshots:[...snapshots].map(([device,fields])=>({device,...fields})),contextPackets:reports.length,
  simulated:items.some(item=>/^sim(?:[-_]|$)|^switch-demo(?:[-_]|$)/i.test(item.device)),
  rawCount:recent?reports.reduce((count,packet)=>count+packet.lines.length,0):report.lines.length};
}

const analysisStyle=`
.report-analysis{font-size:14px;line-height:1.65;white-space:normal;min-width:260px;color:inherit}
.report-analysis h3,.report-analysis h4{margin:0 0 8px;font-size:16px}.report-analysis p{margin:7px 0}
.report-analysis .analysis-overview,.report-analysis .incident-card{border:1px solid var(--analysis-border,#314047);border-radius:12px;padding:18px;margin:12px 0}
.report-analysis .analysis-overview{background:var(--analysis-bg,#142025)}
.report-analysis .analysis-stats{display:flex;gap:9px;flex-wrap:wrap;margin:12px 0}.report-analysis .analysis-stats span,.report-analysis .incident-state{font-size:12px;padding:3px 9px;border:1px solid var(--analysis-border,#314047);border-radius:6px}
.report-analysis .incident-state{display:inline-block;color:#e7ad64;margin-bottom:10px}.report-analysis .incident-state.resolved{color:#54b997}
.report-analysis .analysis-muted{color:var(--analysis-muted,#9cafb4);font-size:12px}.report-analysis .analysis-notice{padding:9px 12px;border-left:3px solid #d0a05e}
.report-analysis .incident-grid{display:grid;grid-template-columns:1fr 1fr;gap:16px;margin:16px 0}.report-analysis ul,.report-analysis ol{padding-left:20px;margin:6px 0}
.report-analysis .incident-timeline{list-style:none;padding:0;border-left:2px solid var(--analysis-border,#314047);margin-left:5px}
.report-analysis .incident-timeline li{padding:2px 0 14px 15px}.report-analysis .incident-timeline time{font-size:12px;color:var(--analysis-muted,#9cafb4);margin-right:10px}
.report-analysis details{background:transparent;border:0;border-top:1px solid var(--analysis-border,#314047);padding:10px 0;margin:8px 0}.report-analysis summary{cursor:pointer;font-weight:500}
.report-analysis pre{max-height:260px;overflow:auto;max-width:100%;font-size:12px;white-space:pre-wrap;overflow-wrap:anywhere}
.report-analysis .model-result{padding:10px 12px;background:var(--analysis-bg,#142025);border-radius:8px;font-size:13px}
.report-analysis .routine-row{padding:5px 0}.report-analysis .model-button{font-size:12px;padding:5px 10px;margin-top:8px}
@media(max-width:700px){.report-analysis .incident-grid{grid-template-columns:1fr}.report-analysis .analysis-overview,.report-analysis .incident-card{padding:12px}}
`;
function node(tag,text='',className=''){const value=document.createElement(tag);value.textContent=text;if(className)value.className=className;return value;}
function list(title,values){const box=node('div');box.append(node('h4',title));const ul=node('ul');for(const value of values)ul.append(node('li',value));box.append(ul);return box;}
function folded(title,content){const box=node('details');box.append(node('summary',title),content);return box;}
const timeText=(time,basis)=>!Number.isFinite(time)?'时间缺失':basis==='uptime'?`开机 +${(time/1000).toFixed(1)} 秒`:new Date(time).toLocaleTimeString('zh-CN',{hour12:false});

// Database and offline SD share this view; interpretation never alters evidence.
function renderReportAI(report,root,classify,options={}){
 const analysis=buildReportAnalysis(report,options.contextReports||[],options),view=node('div','','report-analysis');
 view.append(node('style',analysisStyle));root.append(view);
 const overview=node('section','','analysis-overview'),unresolved=analysis.incidents.filter(item=>!item.recovered).length;
 overview.append(node('h3',analysis.incidents.length?`本次涉及 ${analysis.incidents.length} 组故障或处置记录`:
  analysis.snapshots.some(item=>Number(item.active_faults)>0)?'期末仍有活动故障，需要核对':analysis.diagnostics.length?'本包包含采集或存储状态提醒':analysis.unknown.length?'本包包含待核对记录':'本包以常规运行记录为主'));
 const stats=node('div','','analysis-stats');
 for(const text of [`原文 ${analysis.rawCount} 条`,`常规记录 ${analysis.routines.length} 条`,`已确认恢复 ${analysis.incidents.filter(item=>item.recovered).length} 组`,`待核对 ${unresolved} 组`])stats.append(node('span',text));
 overview.append(stats);
 for(const item of analysis.snapshots)overview.append(node('p',`${item.device} · 期末状态 ${item.state||'未知'} · 活动故障 ${item.active_faults??'未提供'} · 锁定 ${item.locked==='true'?'是':item.locked==='false'?'否':'未知'}`));
 if(analysis.snapshots.some(item=>Number(item.active_faults)>0))overview.append(node('p','期末仍跟踪：'+analysis.snapshots.filter(item=>Number(item.active_faults)>0).map(item=>`${item.device} / ${item.faults||'类型未提供'}`).join('；')));
 overview.append(node('p',options.recent?`已分析最近 ${analysis.contextPackets} 包日志，跨开机周期分别核对，不混合事件。`:analysis.contextPackets>1?`已关联 ${analysis.contextPackets} 包历史，按设备、端口、事件标识核对过程。`:'当前仅有本包证据；跨包的起因或处置可能不在这个片段内。','analysis-muted'));
 if(options.contextError)overview.append(node('p','历史上下文暂不可用，仅分析本包：'+options.contextError,'analysis-notice'));
 if(options.contextLimited)overview.append(node('p','历史上下文达到读取范围，不能据此认为过程完整。','analysis-muted'));
 for(const diagnostic of analysis.diagnostics)overview.append(node('p',diagnostic,'analysis-notice'));
 if(analysis.simulated)overview.append(node('p','当前含虚拟设备演示数据，不能作为真实交换机处置验收。','analysis-notice'));
 overview.append(node('p','说明区分日志事实与可能原因；处置回执成功不等于故障恢复，历史状态不代表当前状态。','analysis-muted'));view.append(overview);
 const jobs=[];
 const modelProgress=options.recent?node('p','正在准备离线小模型分析…','analysis-model-status'):null;
 if(modelProgress){overview.append(node('h4','AI 分析结果'),modelProgress,node('p','小模型识别日志类型，证据规则整理故障解释与检查建议；可能原因仍需现场核对。','analysis-muted'));}

 function modelPanel(parent,item,automatic=true){
  const status=node('p',automatic?'正在辅助分类…':'格式尚未识别，可点击辅助分类；不要仅凭这一条判断根因。','model-result');
  const button=node('button',automatic?'重新辅助分类':'小模型辅助分类','button button-quiet model-button');button.type='button';
  const run=async()=>{
   button.disabled=true;
   try{
    if(new TextEncoder().encode(item.evidence.text).length>1024)throw Error('原文超过模型长度上限');
    const reply=await classify(item.evidence.text,item.evidence.line_index,item.evidence.report_id);
    const description=Object.hasOwn(modelDescriptions,reply.label)?modelDescriptions[reply.label]:null;
    const expected={requested:'action_request',acknowledged:'action_ack',recovered:'recovery_report',snapshot:'state_snapshot'}[item.phase];
    if(reply.experimental!==true||reply.accepted!==true||reply.truncated===true||!Number.isFinite(reply.score)||
       !Number.isFinite(reply.threshold)||reply.threshold<.9||reply.threshold>1||reply.score<reply.threshold||reply.score>1||!description){
     status.textContent='小模型：匹配不足，未给出分类结论。上面的说明来自原文字段和证据规则，仍可用于核对。';return 'rejected';
    }else if(expected&&reply.label!==expected){
     status.textContent='小模型分类与日志阶段不一致，本次不采用。以处置时间线和原文为准。';return 'rejected';
    }else status.textContent=`小模型辅助分类：${description[0]}。${description[1]} 匹配分 ${(reply.score*100).toFixed(1)}%（不是根因正确率）；${reply.model||'版本未提供'}。`;
    return 'accepted';
   }catch(error){status.textContent='小模型暂未完成分类：'+error.message+'。原文证据分析仍可查看。';return 'failed';}
   finally{button.disabled=false;}
  };
  button.onclick=run;parent.append(status,button);return run;
 }
 for(const incident of analysis.incidents){
  const card=node('section','','incident-card');
  card.append(node('span',incident.state,'incident-state'+(incident.recovered?' resolved':'')),node('h3',incident.title),
   node('p',[incident.device||'设备未提供',incident.component||'端口 / 组件未提供',incident.incident?'事件 '+incident.incident:'事件标识缺失'].join(' · '),'analysis-muted'),node('p',incident.meaning));
  if(!incident.linked)card.append(node('p','缺少完整事件标识，这条证据单独展示，未与其他故障强行合并。','analysis-notice'));
  const grid=node('div','','incident-grid');grid.append(list('可能原因 · 待核对',incident.causes.length?incident.causes:['日志没有直接证明根因，需结合原始告警和现场状态。']),list('下一步检查',incident.steps));card.append(grid,node('h4','处置时间线'));
  const timeline=node('ol','','incident-timeline');
  for(const step of incident.timeline){const li=node('li');li.append(node('time',timeText(step.time,step.timeBase)),node('strong',step.phase),node('p',step.detail));timeline.append(li);}card.append(timeline);
  card.append(folded(`关联原文 · ${incident.items.length} 条`,node('pre',incident.timeline.map(step=>`${timeText(step.time,step.timeBase)} [${step.reportId} #${step.line+1}]\n${step.text}`).join('\n\n'))));
  if(incident.anchor&&jobs.length<8)jobs.push(modelPanel(card,incident.anchor));else card.append(node('p',incident.anchor?'本页已完成前 8 组自动辅助分类；此组仍显示完整证据分析。':'此项来自数据库状态字段，没有可单独送入模型的原文行。','analysis-muted'));
  view.append(card);
 }
 const counts=new Map();for(const item of analysis.routines)counts.set(item.phase,(counts.get(item.phase)||0)+1);
 const names={runtime:'ESP 运行状态',snapshot:'设备状态快照',receipt:'输入收件回执',heartbeat:'来源心跳',belief:'判断字段更新',decision:'决策记录',connection:'通信握手',boot:'启动信息',storage:'存储状态',network:'网络状态',collection:'命令采集状态'};
 const routine=node('div');for(const [phase,count] of counts)routine.append(node('p',`${names[phase]||phase} · ${count} 条`,'routine-row'));
 routine.append(node('p','常规记录已合并；输入 ACCEPTED 仅表示收件，不是处置成功。无需逐条调用故障模型。','analysis-muted'));view.append(folded(`常规运行记录 · ${analysis.routines.length} 条`,routine));
 if(analysis.unknown.length){const unknown=node('div');for(const item of analysis.unknown.slice(0,8)){const box=node('div');box.append(node('p',item.verdict.meaning),node('pre',item.evidence.text));if(options.recent&&jobs.length<8)jobs.push(modelPanel(box,item));else modelPanel(box,item,false);unknown.append(box);}if(analysis.unknown.length>8)unknown.append(node('p','其余待识别记录保留在完整原文中。'));view.append(folded(`待识别记录 · ${analysis.unknown.length} 条`,unknown));}
 if(options.recent&&jobs.length<8){
  const samples=[];for(const phase of ['snapshot','runtime']){const item=analysis.routines.filter(item=>item.phase===phase&&item.evidence.report_id===report.report_id).at(-1);if(item&&!analysis.incidents.some(incident=>incident.anchor===item))samples.push(item);}
  if(samples.length){const panel=node('section','','analysis-overview');panel.append(node('h4','最近运行日志 · AI 核对'));for(const item of samples){panel.append(node('p',item.phase==='snapshot'?'最近设备状态快照':'最近 ESP 运行状态','analysis-muted'));if(jobs.length<8)jobs.push(modelPanel(panel,item));}view.append(panel);}
 }
 view.append(folded(`本包完整原文 · ${report.lines.length} 条`,node('pre',report.lines.map(line=>`${timeText(line.at_ms,report.clock)} ${line.text}`).join('\n'))));
 const ready=(async()=>{
  const progress={total:jobs.length,completed:0,accepted:0,rejected:0,failed:0};
  const update=()=>{if(modelProgress)modelProgress.textContent=progress.completed<progress.total?`正在运行 ESP 离线小模型：${progress.completed} / ${progress.total} 条…`:
   progress.total?`分析完成 · 小模型已核对 ${progress.total} 条代表日志，采用 ${progress.accepted} 条，未采用 ${progress.rejected} 条，调用失败 ${progress.failed} 条。`:'分析完成 · 没有可送入模型的日志；仅展示已保存字段的证据解释。';if(options.onProgress)options.onProgress({...progress});};
  update();for(const job of jobs){const result=await job();progress.completed++;progress[result]++;update();}return progress;
 })();
 return {analysis,ready};
}
return {renderReportAI};
})();
const $=id=>document.getElementById(id);
let busy=false,closed=false,files=[],pages=[0],hasOlder=false;
const reportTime=(at,clock)=>clock==='uptime'?'本次开机 +'+(at/1000).toFixed(1)+' 秒':new Date(at).toLocaleString('zh-CN');
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function request(url,options={}){
 const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),6000);
 try{
  const response=await fetch(url,{...options,cache:'no-store',signal:controller.signal});
  if(!response.ok)throw Error('入口已关闭或请求失败，请连按三下 BOOT 后连接 ESP-Logs 热点。');
  // Consume the body before clearing the timeout, including slow or interrupted transfers.
  const body=await response.text();return {json:()=>JSON.parse(body),text:()=>body};
 }catch(error){if(error.name==='AbortError')throw Error('设备响应超时，请确认仍连接 ESP-Logs 热点，再点一键分析最近日志。');throw error;}
 finally{clearTimeout(timer);}
}
const bdiNames={idle:'等待观察',monitoring:'持续监测',unknown:'证据不足',loop_detected:'观察到环路',verifying:'处置后验证中',contained:'隔离已验证，物理环路仍待处理',needs_review:'需要现场核对',recovered:'已验证恢复',disabled:'未启用'};
const heartbeatNames={disabled:'未启用',waiting_ssh:'等待 SSH 首次登录',unknown:'未知',healthy:'正常',suspect:'等待失败确认',lost:'丢失',recovering:'恢复验证中'};
const discoveryNames={unknown:'证据不足',monitoring:'持续探索',watch:'线索待核对',suspected:'疑似环路',confirmed:'交换机已提供明确环路证据'};
const discoveryReasons={waiting_for_observation:'等待采集',incomplete_stale_or_mixed_observation:'本轮证据不完整或已过期',contradictory_loop_and_port_evidence:'环路与端口证据冲突',explicit_switch_loop_evidence:'当前环路表和端口状态相符',switch_loopback_protection_active:'端口已被交换机环路保护关闭',mac_move_evidence_unavailable:'MAC 漂移记录不可用，不能完成间接研判',baseline_established_history_not_replayed:'已建立基线，历史累计次数不作为当前故障',mac_counter_reset_rebaseline:'漂移计数重置，重新建立基线',no_current_multi_signal_loop_evidence:'未发现当前多条件线索，不代表没有环路',lpdt_disabled_no_current_multi_signal_evidence:'LPDT 未配置，暂未发现多条件线索，不代表没有环路',mac_moves_need_independent_corroboration:'双向 MAC 漂移，仍需独立证据',multi_signal_waiting_second_round:'多条件相符，等待下一轮确认',persistent_mac_moves_with_corroboration:'连续两轮双向 MAC 漂移，伴随高 CPU 或近期 STP 变化',discovery_observation_stale:'探索结果已过期，等待新采集'};
async function loadDeviceStatus(){
 if(closed)return;
 try{
  const value=(await request('/device/status')).json();
  const analysis=value.analysis||{},bdi=value.bdi||{},mqtt=value.mqtt||{};
  $('device-ai').textContent=value.ai_on_device?'本机 AI：运行于 ESP32'+(analysis.model?' · '+analysis.model:'')+(analysis.label?' · 最近分类 '+analysis.label+(analysis.accepted?'（已采用）':'（未采用）'):''):'本机 AI：当前不可用';
  $('device-bdi').textContent='BDI：'+(bdiNames[bdi.state]||bdi.state||'等待状态')+(bdi.enabled===false?'（自动处置未启用）':'')+(bdi.reason?' · '+bdi.reason:'')+(bdi.port?' · 端口 '+bdi.port:'')+(bdi.action?' · 动作 '+bdi.action:'');
  const discovery=bdi.discovery;
  $('device-discovery').textContent=discovery?'环路探索：'+(discoveryNames[discovery.state]||discovery.state||'未知')+' · '+(discoveryReasons[discovery.reason]||discovery.reason||'等待证据')+(discovery.port?' · '+discovery.port+(discovery.peer?' ↔ '+discovery.peer:''):'')+(discovery.vlan?' · VLAN '+discovery.vlan:'')+(discovery.rounds?' · 连续 '+discovery.rounds+' 轮':'')+' · 仅观察':'环路探索：当前固件尚未提供状态';
  const heartbeat=typeof value.heartbeat==='string'?new URLSearchParams(value.heartbeat).get('heartbeat_state'):value.heartbeat?.state;
  $('device-link').textContent='SSH：'+(value.ssh_state==='connected'?'已登录':value.ssh_state||'未知')+' · ping 心跳：'+(heartbeatNames[heartbeat]||heartbeat||'未知');
  $('device-mqtt').textContent='后台 MQTT：'+(mqtt.enabled===false?'未启用（不影响本机工作）':mqtt.state||'未知');
 }catch(error){
  for(const [id,label] of [['device-ai','本机 AI'],['device-bdi','BDI'],['device-discovery','环路探索'],['device-link','交换机连接'],['device-mqtt','后台 MQTT']])$(id).textContent=label+'：当前状态暂不可读。';
 }
}
function controls(){for(const id of ['analyze','refresh','file','older','newer'])$(id).disabled=busy||closed||(id==='older'&&!hasOlder)||(id==='newer'&&pages.length<2);}
async function loadReports(showLatest=true){
 $('status').textContent='正在检查 TF 卡中的记录…';
 const url='/storage/files?'+new URLSearchParams({before:String(pages.at(-1))});let reply;
 for(let retry=0;retry<40;retry++){
  reply=(await request(url)).json();if(reply.state!=='busy'&&reply.state!=='starting')break;
  await pause(200);
 }
 if(reply.state==='no_card')throw Error('未检测到 TF 卡，请插入 FAT32 卡后重试。');
 if(reply.state!=='ready')throw Error('记录存储尚未就绪（'+reply.state+'），请检查 FAT32 卡后重试。');
 files=reply.files||[];hasOlder=reply.has_older===true;$('file').replaceChildren();
 for(const file of files){const option=document.createElement('option');option.value=file.name;option.textContent='记录 #'+file.sequence+' · '+(file.bytes/1024).toFixed(1)+' KiB';$('file').append(option);}
 if(!files.length){const option=document.createElement('option');option.value='';option.textContent='等待设备保存日志';$('file').append(option);}
 $('file').value=files[0]?.name||'';
 $('storage').textContent='已保留 '+reply.count+' 包，占用 '+(reply.bytes/1048576).toFixed(2)+' MiB。'+(reply.source==='esp_native'?'记录由 ESP 本机采集并保存。':'历史数据库副本。');
 if(showLatest)await loadAnalysis();
}
async function readStoredReport(file,label=''){
 let offset=0,total=0;const chunks=[],started=Date.now();
 $('status').textContent=label+'正在读取日志，0 KiB…';
 for(let page=0;page<700;page++){
  if(Date.now()-started>120000)throw Error('读取时间过长，请重新连接热点并重新分析。');
  let reply=(await request('/storage/read?'+new URLSearchParams({file,offset:String(offset)}))).json();let job=reply.job;
  for(let retry=0;retry<40&&(reply.state==='pending'||reply.state==='busy');retry++){
   await pause(50);
   reply=(await request('/storage/read?'+new URLSearchParams(job?{job:String(job)}:{file,offset:String(offset)}))).json();job=reply.job||job;
  }
  if(reply.state!=='ready')throw Error(reply.message||'日志读取未完成，请点一键分析最近日志重试。');
  const bytes=Uint8Array.from(atob(reply.base64),c=>c.charCodeAt(0));
  if(reply.start!==offset||reply.next!==offset+bytes.length||reply.size>2097152||reply.next>reply.size||(!bytes.length&&!reply.eof))throw Error('日志读取位置异常，请刷新重试。');
  chunks.push(bytes);total+=bytes.length;offset=reply.next;
  $('status').textContent=label+'正在读取日志 '+Math.round(offset/Math.max(reply.size,1)*100)+'% · '+(offset/1024).toFixed(1)+' KiB';
  if(reply.eof)break;if(page===699)throw Error('日志超过读取上限。');
 }
 const bytes=new Uint8Array(total);let cursor=0;for(const chunk of chunks){bytes.set(chunk,cursor);cursor+=chunk.length;}
 const report=JSON.parse(new TextDecoder('utf-8',{fatal:true}).decode(bytes));
 if(report.schema_version!==1||!Array.isArray(report.lines)||report.lines.length>2048)throw Error('日志包格式无效。');
 return report;
}
function showReport(report,options={}){
 $('report').replaceChildren();
 const view=renderReportAI(report,$('report'),async text=>(await request('/model/explain?'+new URLSearchParams({log:text}))).json(),options);
 const raw=document.createElement('details'),summary=document.createElement('summary'),pre=document.createElement('pre');summary.textContent='上报信息与完整 JSON';pre.textContent=JSON.stringify(options.contextReports||[report],null,2);raw.append(summary,pre);$('report').append(raw);
 return view;
}
async function loadAnalysis(){
 $('report').replaceChildren();const file=$('file').value;
 if(!file){$('status').textContent='暂无可分析记录。等待 ESP 保存下一批现场记录后，点一键分析最近日志。';return;}
 const report=await readStoredReport(file);showReport(report);
 $('status').textContent=reportTime(report.sent_at_ms,report.clock)+' · '+report.lines.length+' 条日志 · 历史记录';
 for(const option of $('file').children)if(option.value===file)option.textContent=reportTime(report.sent_at_ms,report.clock)+' · '+report.lines.length+' 条';
}
async function analyzeRecent(){
 pages=[0];await loadReports(false);$('report').replaceChildren();
 if(!files.length){$('status').textContent='暂无可分析记录。等待 ESP 保存下一批现场记录后，再点一键分析最近日志。';return;}
 const reports=[],latest=await readStoredReport(files[0].name,'第 1 包 · ');reports.push(latest);
 const cutoff=latest.sent_at_ms-10*60000;let contextBytes=0,contextError='',limited=false;
 for(const file of files.slice(1,10)){
  if(contextBytes+file.bytes>524288){limited=true;break;}
  try{const report=await readStoredReport(file.name,'第 '+(reports.length+1)+' 包 · ');
   if((latest.clock==='uptime'||report.clock==='uptime')&&(latest.clock!==report.clock||latest.boot!==report.boot))break;
   if(report.sent_at_ms<cutoff)break;
   if(report.sent_at_ms>latest.sent_at_ms){contextError='部分记录时间异常，未纳入分析';continue;}
   contextBytes+=file.bytes;reports.push(report);
  }catch(error){contextError='部分历史记录读取失败：'+error.message;break;}
 }
 const start=Math.min(...reports.map(report=>report.sent_at_ms)),end=latest.sent_at_ms;
 const range=reportTime(start,latest.clock)+'—'+reportTime(end,latest.clock);
 const {analysis,ready}=showReport(latest,{recent:true,contextReports:reports,contextError,contextLimited:limited||files.length>10||hasOlder,
  onProgress:progress=>{$('status').textContent='AI 分析中 · 已读取 '+reports.length+' 包 · 模型核对 '+progress.completed+'/'+progress.total+' 条…';}});
 const progress=await ready;
 $('status').textContent='AI 分析完成 · '+range+' · '+reports.length+' 包 / '+analysis.rawCount+' 条日志 · '+analysis.incidents.length+' 组故障或处置记录 · 小模型采用 '+progress.accepted+'/'+progress.total+' 条。'+(contextError?' 部分历史未读取，详见分析范围。':'');
}

async function run(action){if(busy||closed)return;busy=true;$('status').dataset.error='false';controls();try{await action();}catch(error){$('status').textContent=error.message;$('status').dataset.error='true';}finally{busy=false;controls();}}
$('analyze').onclick=()=>run(analyzeRecent);
$('refresh').onclick=()=>run(async()=>{pages=[0];await loadReports();});
$('older').onclick=()=>run(async()=>{if(files.length){pages.push(files.at(-1).sequence);await loadReports();}});
$('newer').onclick=()=>run(async()=>{if(pages.length>1){pages.pop();await loadReports();}});
$('file').onchange=()=>run(loadAnalysis);
$('close').onclick=()=>run(async()=>{$('status').textContent=(await request('/storage/close',{method:'POST'})).text();closed=true;});
function startPage(){loadDeviceStatus();run(loadReports);setInterval(loadDeviceStatus,5000);}
startPage();
</script></main></body></html>
)ESP_PAGE";
