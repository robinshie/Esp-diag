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
export function faultRule(fault){return rules[aliases[fault]||fault]||null;}
const value=input=>typeof input==='string'?input.slice(0,160):'';
function fields(text){
 const result=Object.create(null);
 for(const token of text.split(/\s+/)){const at=token.indexOf('=');if(at<=0)continue;const key=token.slice(0,at);if(Object.prototype.hasOwnProperty.call(result,key))return null;result[key]=token.slice(at+1);}
 return result;
}
export function normalizeRecord(input){
 const record=input&&typeof input==='object'&&!Array.isArray(input)?input:{};
 const text=value(record.text); // Full frames below are bounded separately, not truncated before parsing.
 const raw=typeof record.text==='string'?record.text.slice(0,2048):'';
 const accepted=/^(?:H3C (?:LOG|PONG) |\[BDI\]\[(?:CMD|ACK|RECEIPT|RECOVERED|STATE|ACTION|EVENT|WARN|STATUS|BOOT|READY|HEARTBEAT|OBSERVE|VERIFY|H3C|BLOCKED|COLLECT)\]|\[(?:STORAGE|NETWORK|SSH|CONSOLE|BOOT|MEMORY|LOG-AI)\])/.test(raw);
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
 else if(/^\[(?:SSH|CONSOLE)\]/.test(raw))phase='connection';
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
export function normalizeRecords(records){return correlate(records.map(normalizeRecord));}
export function explainRecords(records){
 const items=normalizeRecords(records);
 return items.map(item=>explain(item,items));
}
