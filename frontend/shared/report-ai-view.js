import {buildReportAnalysis} from './report-analysis.js';
import {modelDescriptions} from './model-descriptions.js';

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
export function renderReportAI(report,root,classify,options={}){
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
