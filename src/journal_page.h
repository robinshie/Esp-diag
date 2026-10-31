// Generated from frontend/journal.html by tools/embed_network_page.py
#pragma once
static const char JOURNAL_PAGE[] PROGMEM = R"ESP_PAGE(<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>TF 故障记录</title>
<style>
:root{font:16px/1.6 system-ui,sans-serif;color:#183047;background:#eef3f7}*{box-sizing:border-box}body{margin:0}main{max-width:900px;margin:auto;padding:24px 16px}h1{margin:0;font-size:26px}h2{font-size:18px;margin:0 0 8px}.badge{background:#d7f3ef;color:#145c52;border-radius:16px;padding:4px 12px;font-size:13px}header,nav,.row{display:flex;gap:12px;align-items:center;flex-wrap:wrap}header{justify-content:space-between}section,article{background:white;border:1px solid #d6e0e8;border-radius:12px;padding:18px;margin:16px 0}label{display:block;flex:1;min-width:180px}select,button{font:inherit;border:1px solid #a9bdcc;border-radius:8px;padding:10px 12px;background:white;color:inherit}select{display:block;width:100%;margin-top:4px}button{cursor:pointer}button:disabled{opacity:.45;cursor:default}#refresh{background:#155e82;color:white}small,.muted{color:#526b7f}#status{white-space:pre-wrap}pre{white-space:pre-wrap;overflow-wrap:anywhere;font:13px/1.6 ui-monospace,monospace;margin:8px 0 0}summary{cursor:pointer}#records:empty{display:none}.event{font-weight:650;color:#165f7c}.range{margin:12px 0;color:#526b7f;font-size:14px}a{color:#155e82}
.interpretation{background:#f0f8fa;border-left:3px solid #238b9c;border-radius:8px;padding:14px;margin:14px 0}.interpretation h3{font-size:16px;margin:0 0 8px}.interpretation h4{font-size:14px;margin:14px 0 4px}.interpretation p{margin:8px 0}.interpretation li{margin:5px 0}.progress{font-weight:600}.assistant-toggle{margin:10px 0;min-width:0;font-size:15px}.assistant-toggle input{width:auto}
</style></head><body><main>
<header><h1>TF 故障记录</h1><span class="badge">只读查看</span></header>
<p>双击 BOOT 打开查看入口。这里不修改网络配置，查看窗口 10 分钟后自动关闭。</p>
<section><h2>存储状态</h2><p id="status" role="status" aria-live="polite">正在连接设备…</p><small>记录最多占用约 12 MiB；卡剩余不足 1 MiB 时也会清理最早创建的记录文件。只清理本程序记录，其他文件保留。</small></section>
<section><div class="row"><label>记录类型<select id="kind"><option value="summary">故障摘要</option><option value="raw">原始日志</option><option value="process">处置过程</option></select></label><label>记录文件（最新创建的在前）<select id="file"><option value="">暂无记录</option></select></label></div>
<p class="muted">仅显示已保存的证据。未同步时间时显示开机时长；通信恢复、命令回执与故障恢复分别记录。</p>
<nav><button id="refresh" type="button">刷新记录</button><button id="earlier" type="button" disabled>更早内容</button><button id="later" type="button" disabled>后续内容</button><button id="latest" type="button" disabled>文件末尾</button></nav><p id="range" class="range"></p></section>
<section><div class="row"><h2>离线故障助手</h2><span class="badge">日志解释 · 无需联网</span></div><p id="assistant-state" role="status">读取记录后，自动解释故障现象、可能原因、处置进展与排查建议。</p><label class="assistant-toggle"><input id="assistant-enabled" type="checkbox" checked> 显示每条记录的解释</label><small>实验固件中可点击“小模型解释这条日志”，由 ESP 本地识别日志含义并显示中文说明。训练样本含 H3C 官方资料推导的改写及合成日志，结果仅供参考。下面的证据核对与恢复判断仍按已记录事实显示。</small></section>
<div id="records" aria-live="polite"></div>
<button id="close" type="button" hidden>关闭查看入口</button><p><small>查看模式：青灯连续闪两下。下次双击 BOOT 后重新连接 ESP-Logs 热点即可查看；如需修改配置，长按 BOOT 3 秒。</small></p>
<script type="module">
// Offline, deterministic explanations. This module never sends data or executes actions.
const rules={
 AUTH_LOCKED:{title:'管理账户被锁定',meaning:'来源报告账户触发锁定，可能暂时无法登录管理界面。',causes:['重复登录失败、客户端仍使用旧密码，或账户锁定策略。'],steps:['核对登录失败记录、账户和锁定策略。','检查自动登录客户端是否保存旧密码；确认原因后再由管理员解锁。']},
 LINK_DOWN:{title:'端口链路中断',meaning:'来源报告端口链路不可用，可能影响经过该端口的通信。',causes:['线缆或光模块异常、对端供电中断、端口配置或保护策略。'],steps:['核对具体端口、指示灯、线缆或光模块和对端供电。','查看端口状态、错误计数与配置；链路中断本身不能证明存在环路。']},
 THERMAL:{title:'温度告警',meaning:'来源报告温度异常，需要核对实际温度与设备告警阈值。',causes:['环境温度偏高、散热通道堵塞、风扇异常、负载升高或传感器异常。'],steps:['核对温度传感器读数、告警阈值、风扇与通风情况。','观察温度和告警是否持续；不能仅凭一条告警认定设备已损坏。']},
 LOOP:{title:'来源报告二层环路',meaning:'日志明确报告环路类事件，仍需要核对拓扑和交换机证据。',causes:['重复接线、桥接拓扑或生成树保护未按预期工作。'],steps:['核对告警端口、拓扑、生成树状态、MAC 漂移和广播计数。','确认隔离对象与管理路径后再处理，避免误关上联或管理端口。当前固件尚未实现自动环路隔离。']},
 PSU_FAILURE:{title:'电源模块告警',meaning:'来源报告电源模块异常，不代表整台设备已经断电。',causes:['供电输入、电源模块、冗余状态或检测信号异常。'],steps:['核对电源模块指示灯、供电输入和冗余状态。','对照设备告警记录确认影响范围。']},
 FAN_FAILURE:{title:'风扇告警',meaning:'来源报告风扇异常，需要核对转速和散热状态。',causes:['风扇停转、转速不足、接触或检测信号异常。'],steps:['核对风扇转速与温度，检查通风情况。','按设备维护要求安排检查或更换。']}
};
const aliases={LOOP_DETECTED:'LOOP',LOOPBACK:'LOOP',L2_LOOP:'LOOP'};
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
 const accepted=/^(?:H3C LOG |\[BDI\]\[(?:CMD|ACK|RECOVERED|STATE|ACTION|EVENT|WARN|STATUS|BOOT|READY)\])/.test(raw);
 const f=accepted?fields(raw):Object.create(null);
 const get=key=>value(f?.[key]);
 const event=value(record.event),rawEvent=get('event');
 let phase='unknown';
 if(event==='DETECTED')phase='detected';
 else if(event==='RECOVERED')phase='recovered';
 else if(/^\[BDI\]\[RECOVERED\]/.test(raw))phase='recovered';
 else if(/^\[BDI\]\[CMD\]/.test(raw))phase='requested';
 else if(/^\[BDI\]\[ACK\]/.test(raw))phase='acknowledged';
 else if(/^H3C LOG /.test(raw)&&['RECOVERY','LINK_UP'].includes(rawEvent)&&get('status')==='OK')phase='recovery_reported';
 else if(/^H3C LOG /.test(raw)&&(get('status')==='ALARM'||(rawEvent==='AUTH_LOCKED'&&get('status')==='LOCKED')))phase='reported';
 else if(/^H3C LOG /.test(raw)&&rawEvent==='HEARTBEAT'&&get('status')==='OK')phase='heartbeat';
 else if(/^\[BDI\]\[STATE\]/.test(raw))phase='snapshot';
 else if(event)phase=event.toLowerCase();
 let fault=value(record.fault)||get('fault');
 if(!fault&&['reported','detected'].includes(phase))fault=rawEvent;
 const action=get('action')||value(raw.match(/^\[BDI\]\[CMD\]\s+(\S+)/)?.[1]);
 const time=typeof record.uptime_ms==='number'&&Number.isFinite(record.uptime_ms)&&record.uptime_ms>=0?record.uptime_ms:null;
 if(record.truncated===true&&phase==='recovered')phase='recovery_reported';
 return {record,text,event,phase:f===null?'ambiguous':phase,fault,rule:aliases[fault]||fault,boot:value(record.boot_id),device:value(record.device)||get('device'),component:value(record.component)||get('component')||get('port'),incident:value(record.incident)||get('incident'),cause:value(record.reported_cause)||get('cause'),request:get('request_id'),result:get('result'),action,attempt:get('attempt'),temp:get('temp_c'),severity:value(record.severity)||get('level'),snapshot:get('state'),time};
}
function complete(item){return Boolean(item.boot&&item.device&&item.component&&item.fault&&item.incident);}
function sameIncident(a,b){return complete(a)&&complete(b)&&['boot','device','component','fault','incident'].every(key=>a[key]===b[key]);}
function correlate(items){
 for(const item of items){
  if(item.phase!=='acknowledged'||!item.boot||!item.request||!item.device||!item.component)continue;
  const commands=items.filter(other=>other.phase==='requested'&&other.boot===item.boot&&other.request===item.request&&other.device===item.device&&other.component===item.component&&other.action===item.action&&complete(other)&&other.time!==null&&item.time!==null&&other.time<=item.time);
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
  info.progress=['FAILED','ATTEMPT_FAILED'].includes(item.result)?'回执报告失败或超时；应核对执行结果、管理连接与后续日志。':item.result==='COMMAND_ACCEPTED'?'命令已被接受，仍在等待故障恢复验证。':item.result==='SUCCESS'?'处置流程返回 SUCCESS；单凭这一回执仍不能认定故障恢复。':'回执含义未识别，不能据此判断成功或恢复。';
  if(!complete(item))info.limits.push('当前片段缺少匹配的命令或事件标识，无法将回执关联到具体故障。');
 }
 if(item.phase==='recovery_reported')info.progress='记录提及恢复，但这一条记录尚未提供完整的固件稳定性验证证据。';
 if(item.phase==='heartbeat'){
  info.known=true;info.title='收到来源心跳';info.meaning='日志记录来源当时仍能发送心跳。';info.progress='心跳到达不代表原有故障已恢复。';info.steps=['结合故障恢复记录核对状态。'];
 }
 if(item.phase==='snapshot'){
  info.known=true;info.title='设备状态快照';info.meaning='固件记录了该时刻的状态'+(item.snapshot?'：'+item.snapshot:'')+'。';info.progress='快照描述记录当时的模型状态，不保证当前状态。';info.steps=['结合前后告警、来源在线状态和恢复记录判断。'];
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
 const recovery=items.find(other=>other!==item&&other.phase==='recovered'&&sameIncident(item,other)&&item.time!==null&&other.time!==null&&other.time>=item.time);
 if(recovery&&item.phase!=='recovered'&&['detected','reported','requested','acknowledged','recovery_reported'].includes(item.phase)){
  info.recovered=true;info.progress+=' 当前片段后续还包含同一开机周期、设备、端口和事件的已验证恢复记录。';
 }
 if(/^sim(?:[-_]|$)|^switch-demo(?:[-_]|$)/i.test(item.device))info.limits.push('设备标识属于演示命名；请勿将演示记录作为真实交换机故障处置证据。');
 if(item.record.input_source==='usb_gateway')info.limits.push('日志经 USB 网关输入，需核对是实机采集还是演示数据。');
 if(item.record.truncated===true)info.limits.push('这条原始日志已被截断，解释可能缺少后续字段。');
 return info;
}
function explainRecords(records){
 const items=correlate(records.map(normalizeRecord));
 return items.map(item=>explain(item,items));
}

const $=id=>document.getElementById(id);
let files=[],busy=false,selected='',pageStart=0,pageNext=0,pageSize=0,closed=false,loadedText='';
const stateLabels={ready:'正常记录',no_card:'未检测到可用卡，请插入 FAT32 TF 卡。',write_error:'写卡失败，请检查卡空间、格式和接触情况。',unavailable:'记录任务不可用。',starting:'正在初始化 TF 卡。'};
const eventLabels={BOOT:'设备启动',DETECTED:'发现故障',RECOVERED:'故障恢复',SOURCE_LOST:'日志来源失联',SOURCE_ONLINE:'日志来源在线',ETH_LINK_DOWN:'网口链路断开',ETH_LINK_UP:'网口链路恢复',ACTION_EVIDENCE:'处置证据',RECORD_GAP:'记录缺口',TRACKING_LOST:'跟踪中断',SUPERSEDED:'事件已替换',SUMMARY_CAPACITY:'摘要容量已满'};
function size(bytes){const value=Number(bytes||0);return value>=1073741824?(value/1073741824).toFixed(1)+' GiB':value>=1048576?(value/1048576).toFixed(1)+' MiB':(value/1024).toFixed(1)+' KiB';}
async function request(url,options={}){
 const controller=new AbortController();const timer=setTimeout(()=>controller.abort(),6000);
 try{const response=await fetch(url,{...options,cache:'no-store',signal:controller.signal});if(!response.ok){if(url.startsWith('/model/explain?')&&response.status===503)throw Error('当前固件未安装实验模型，下面的日志证据解释仍可使用。');throw Error('入口已关闭或请求失败。请双击 BOOT 后重新连接设备热点。');}return response;}finally{clearTimeout(timer);}
}
function controls(){for(const id of ['kind','file','refresh'])$(id).disabled=busy||closed;
 $('earlier').disabled=busy||closed||!selected||pageStart===0;$('later').disabled=busy||closed||!selected||pageNext>=pageSize;$('latest').disabled=busy||closed||!selected;}
async function storage(){
 const cfg=new URLSearchParams(await (await request('/storage')).text());
 const text=stateLabels[cfg.get('state')]||'存储状态未知';
 $('status').textContent=text+'\n本次开机已保存 '+(cfg.get('written')||0)+' 条；未保存 '+(cfg.get('dropped')||0)+' 条；已循环清理 '+(cfg.get('evicted')||0)+' 个文件。'+(cfg.get('summary_incomplete')==='1'?' 摘要跟踪容量已满，部分故障未纳入摘要。':'');
 $('close').hidden=cfg.get('viewer_only')!=='1';
 return cfg;
}
function fileOptions(){
 const previous=selected;const matching=files.filter(file=>file.kind===$('kind').value);
 $('file').replaceChildren();
 for(const file of matching){const option=document.createElement('option');option.value=file.name;option.textContent='记录 #'+file.name.split('-')[0].replace(/^0+/,'')+' · '+size(file.bytes);option.title=file.name;$('file').append(option);}
 if(!matching.length){const option=document.createElement('option');option.value='';option.textContent='暂无此类记录';$('file').append(option);}
 selected=matching.some(file=>file.name===previous)?previous:matching[0]?.name||'';$('file').value=selected;
}
const modelDescriptions={
 auth_lock:['认证或账户锁定','日志涉及认证锁定。请核对具体账户、登录失败记录和锁定策略，尚不能确认是谁触发了锁定。'],
 link_down:['链路中断','日志涉及链路不可用。线缆、对端供电、端口配置或保护策略均需核对；这条日志不能证明存在环路。'],
 thermal:['温度告警','日志涉及温度异常。请核对温度读数、阈值、风扇和通风情况；根因尚未确定。'],
 loop:['环路类告警','日志可能涉及二层环路。请核对告警端口、拓扑、生成树和 MAC 漂移证据；这不代表环路已经排除。'],
 power:['电源模块告警','日志涉及电源模块异常。请核对供电输入、模块和冗余状态；不能据此认定整台交换机已断电。'],
 fan:['风扇告警','日志涉及风扇异常。请核对转速、温度和模块状态，确认是否影响散热。'],
 source_lost:['日志来源失联','日志涉及心跳超时或来源不可达。请检查设备供电、链路和采集进程；失联本身不能证明设备宕机。'],
 action_request:['处置请求','日志涉及已发出或正在派发的命令。发出请求尚不代表执行成功，更不能据此判断故障恢复。'],
 action_ack:['处置回执','日志涉及命令回执。请查看原文中的 SUCCESS、FAILED 或超时结果，并核对后续恢复证据；回执到达不等于恢复。'],
 recovery_report:['恢复相关记录','日志涉及恢复报告。是否已通过固件稳定性验证，请看本条记录的证据解释；历史恢复记录不代表当前状态。'],
 normal:['运行或审计信息','日志涉及启动、在线、登录或命令审计等信息。单条运行或审计日志不能证明全部业务正常、命令执行成功或原有故障已恢复。'],
 record_gap:['记录或跟踪缺口','日志可能涉及记录丢失、存储空间不足或跟踪不完整。请补查原始日志，不能从不完整片段还原全部过程。'],
 login_failure:['登录或 AAA 请求失败','日志涉及登录认证、授权失败或认证超时。请核对账号、服务类型、认证服务器连通性和日志原因；失败本身不能证明账户已被锁定，也不能直接认定密码错误。'],
 stp_protection:['生成树保护或根桥变化','日志涉及 STP 保护、BPDU 异常或根桥角色变化。请核对原文属于哪种事件，再检查上游 STP 状态和拓扑；LOOP_PROTECTION 缺少 BPDU 本身不能证明已经形成二层环路。'],
 mac_move:['MAC 地址迁移','日志涉及某个 VLAN 中的 MAC 学习位置改变。请核对迁移源端口、新端口、次数与拓扑，判断是否为预期迁移；单条 MAC_FLAPPING 不能证明存在环路。'],
 resource_pressure:['CPU 或内存资源告警','日志涉及 CPU、CPU 核心或内存使用率越过阈值。请核对实际占用、持续时间、进程及阈值；这一条日志不能直接确定是攻击、环路或硬件损坏。']
};
function modelPanel(record){
 const box=document.createElement('div');const button=document.createElement('button');button.type='button';button.textContent='小模型解释这条日志';
 const result=document.createElement('p');result.className='muted';result.textContent='实验模型在 ESP 上运行，只解释当前这条日志。';
 button.onclick=async()=>{
  if(closed)return;button.disabled=true;result.textContent='ESP 正在解释…';
  try{
   const text=typeof record.text==='string'?record.text:['event','fault','reported_cause','severity'].filter(key=>typeof record[key]==='string').map(key=>key+'='+record[key]).join(' ');
   if(!text||encodeURIComponent(text).replace(/%[\da-f]{2}/gi,'x').length>1024)throw Error('日志为空或超过模型的 1024 字节上限，请查看完整原文。');
   const reply=await (await request('/model/explain?'+new URLSearchParams({log:text}))).json();
   const description=Object.prototype.hasOwnProperty.call(modelDescriptions,reply.label)?modelDescriptions[reply.label]:null;
   if(reply.experimental!==true||reply.accepted!==true||reply.truncated===true||!Number.isFinite(reply.score)||!Number.isFinite(reply.threshold)||reply.threshold<0.9||reply.threshold>1||reply.score<reply.threshold||reply.score>1||!description){result.textContent='小模型暂时不能可靠解释这条日志，请以本条原始证据为准。';return;}
   result.textContent='实验模型识别：'+description[0]+'。'+description[1]+' 匹配分 '+(reply.score*100).toFixed(1)+'%（不是根因正确率）。仅供参考，以原文和证据核对为准。'+(typeof reply.model==='string'?' 模型：'+reply.model.slice(0,64)+'。':'');
  }catch(error){result.textContent=error.name==='AbortError'?'解释超时，请稍后重试。':error.message;}finally{button.disabled=closed;}
 };
 box.append(button,result);return box;
}
function interpretation(verdict,record){
 const panel=document.createElement('div');panel.className='interpretation';panel.hidden=!$('assistant-enabled').checked;
 const heading=document.createElement('h3');heading.textContent='离线解释：'+verdict.title;
 const meaning=document.createElement('p');meaning.textContent=verdict.meaning;
 const progress=document.createElement('p');progress.className='progress';progress.textContent='处置进展：'+verdict.progress;panel.append(heading,meaning,progress);
 const more=document.createElement('details');const summary=document.createElement('summary');summary.textContent='可能原因、排查建议与依据';more.append(summary);
 for(const [label,items,ordered] of [['可能原因（待核对）',verdict.causes,false],['建议排查',verdict.steps,true],['日志依据',verdict.evidence,false],['解释范围',verdict.limits,false]]){
  if(!items.length)continue;const title=document.createElement('h4');title.textContent=label;const list=document.createElement(ordered?'ol':'ul');
  for(const text of items){const item=document.createElement('li');item.textContent=text;list.append(item);}more.append(title,list);
 }
 panel.append(more,modelPanel(record));return panel;
}
function render(text){
 loadedText=text;
 $('records').replaceChildren();
 const lines=text.split('\n').filter(line=>line.trim());
 const parsed=lines.map(line=>{try{const data=JSON.parse(line);return data&&typeof data==='object'&&!Array.isArray(data)?data:null;}catch{return null;}});
 const valid=parsed.filter(Boolean);const explanations=explainRecords(valid);let index=0;
 $('assistant-state').textContent=valid.length?'当前片段 '+valid.length+' 条完整记录，'+explanations.filter(item=>item.known).length+' 条有匹配规则，'+explanations.filter(item=>item.phase==='recovered').length+' 条为恢复记录。'+(lines.length>valid.length?' 另有 '+(lines.length-valid.length)+' 条不完整或异常记录。':''):'当前片段没有可解释的完整记录。';
 for(let row=0;row<lines.length;row++){
  const line=lines[row];
  const card=document.createElement('article');let record;
  record=parsed[row];if(!record){const note=document.createElement('p');note.textContent='不完整或异常记录（可能在掉电时中断）';const pre=document.createElement('pre');pre.textContent=line;card.append(note,pre);$('records').append(card);continue;}
  const title=document.createElement('div');title.className='event';title.textContent=eventLabels[record.event]||record.event||($('kind').value==='raw'?'原始日志':'处置过程');
  const meta=document.createElement('small');meta.textContent=[record.device,record.component,record.unix_ms?new Date(record.unix_ms).toLocaleString():record.uptime_ms!=null?'开机 '+(record.uptime_ms/1000).toFixed(1)+' 秒':'时间未知',record.boot_id?'开机标识 '+record.boot_id.slice(-8):''].filter(Boolean).join(' · ');
  card.append(title,meta);
  if(record.message){const message=document.createElement('p');message.textContent=record.message;card.append(message);}
  if(record.text){const pre=document.createElement('pre');pre.textContent=record.text;card.append(pre);}
  card.append(interpretation(explanations[index++],record));
  const details=document.createElement('details');const summary=document.createElement('summary');summary.textContent='完整记录';const json=document.createElement('pre');json.textContent=JSON.stringify(record,null,2);details.append(summary,json);card.append(details);$('records').append(card);
 }
 if(!$('records').childElementCount){const note=document.createElement('p');note.textContent='当前文件片段暂无完整记录。';$('records').append(note);}
}
async function readPage(offset){
 if(!selected){render('');$('range').textContent='插卡并产生记录后，可在这里查看。';pageStart=pageNext=pageSize=0;return;}
 let job=0;let reply=await (await request('/storage/read?'+new URLSearchParams({file:selected,offset:String(offset)}))).json();
 for(let retry=0;retry<40&&(reply.state==='pending'||reply.state==='busy');retry++){
  await new Promise(resolve=>setTimeout(resolve,100));
  job=reply.job||job;
  reply=await (await request('/storage/read?'+new URLSearchParams(job?{job:String(job)}:{file:selected,offset:String(offset)}))).json();
 }
 if(reply.state!=='ready')throw Error(reply.message||'记录读取繁忙或已被覆盖，请稍后刷新。');
 const bytes=Uint8Array.from(atob(reply.base64),c=>c.charCodeAt(0));render(new TextDecoder().decode(bytes));
 pageStart=reply.start;pageNext=reply.next;pageSize=reply.size;
 $('range').textContent='文件位置 '+size(pageStart)+' – '+size(pageNext)+' / '+size(pageSize)+'。';
}
async function run(action){if(busy||closed)return;busy=true;controls();try{await action();}catch(error){$('status').textContent=error.name==='AbortError'?'设备响应超时，请双击 BOOT 并重新连接 ESP-Logs 热点。':error.message;}finally{busy=false;controls();}}
async function refresh(){
 await storage();const result=await (await request('/storage/files')).json();
 if(result.state==='busy')throw Error('存储正在更新，请稍后刷新。');
 files=result.files||[];fileOptions();
 const file=files.find(file=>file.name===selected);await readPage(Math.max(0,(file?.bytes||0)-3072));
 if(result.state==='ready')$('status').textContent+='\n卡剩余 '+size(result.free_bytes)+'；记录占用 '+size(result.log_bytes)+'。';
}
$('refresh').onclick=()=>run(refresh);
$('kind').onchange=()=>run(async()=>{selected='';fileOptions();const file=files.find(file=>file.name===selected);await readPage(Math.max(0,(file?.bytes||0)-3072));});
$('file').onchange=()=>run(async()=>{selected=$('file').value;const file=files.find(file=>file.name===selected);await readPage(Math.max(0,(file?.bytes||0)-3072));});
$('earlier').onclick=()=>run(()=>readPage(Math.max(0,pageStart-3072)));
$('later').onclick=()=>run(()=>readPage(pageNext));
$('latest').onclick=()=>run(refresh);
$('assistant-enabled').onchange=()=>render(loadedText);
$('close').onclick=()=>run(async()=>{$('status').textContent=await (await request('/storage/close',{method:'POST'})).text();closed=true;$('close').disabled=true;});
run(refresh);
</script></main></body></html>
)ESP_PAGE";
