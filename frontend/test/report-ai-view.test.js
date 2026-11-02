import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {renderReportAI} from '../shared/report-ai-view.js';
import {buildReportAnalysis} from '../shared/report-analysis.js';
import {modelDescriptions} from '../shared/model-descriptions.js';

test('native containment is displayed as isolation and never as verified repair',()=>{
 const report={schema_version:1,source:'esp_native',clock:'uptime',boot:'b1',report_id:'native',sent_at_ms:1000,
  lines:[{at_ms:1000,text:'[BDI][VERIFY] device=ethernet-switch port=GigabitEthernet1/0/2 state=contained reason=isolation_verified_physical_loop_not_declared_repaired'}]};
 const [incident]=buildReportAnalysis(report).incidents;
 assert.equal(incident.recovered,false);assert.match(incident.state,/隔离.*物理环路仍待处理/);assert.match(incident.meaning,/并未证明/);
});
class Element{constructor(tag){this.tag=tag;this.children=[];this.textContent='';}append(...items){this.children.push(...items);}}
const flatten=node=>[node,...node.children.flatMap(flatten)];
const packet=(id,lines,at=1000,boot='b1')=>({schema_version:1,report_id:id,sent_at_ms:at,boot,status:null,observations:[],lines:lines.map((text,index)=>({at_ms:at-100+index,text}))});
const command='[BDI][CMD] RECOVER_INTERFACE device=sw1 component=GE1/0/8 fault=LINK_DOWN incident=i1 request_id=r1 attempt=1';
const ack='[BDI][ACK] action=RECOVER_INTERFACE device=sw1 component=GE1/0/8 request_id=r1 result=SUCCESS';
const recovered='[BDI][RECOVERED] device=sw1 component=GE1/0/8 fault=LINK_DOWN incident=i1';
test('native reports retain uptime and cannot correlate across boots or epoch clocks',()=>{
 const current={...packet('native',[command,recovered],2000),clock:'uptime',source:'esp_native'};
 const otherBoot={...packet('other',[command],1000,'other'),clock:'uptime'};
 const epoch=packet('epoch',[command],1000);
 const analysis=buildReportAnalysis(current,[otherBoot,epoch],{recent:true});
 assert.equal(analysis.contextPackets,1);assert.equal(analysis.rawCount,2);
 assert.equal(analysis.incidents[0].items[0].timeBase,'uptime');
 assert.equal(analysis.incidents[0].timeline[0].timeBase,'uptime');
});
test('native SSH and ICMP observations remain distinct routine evidence',()=>{
 const current={...packet('native',['[SSH][STATUS] state=connected target=192.168.10.2','[BDI][HEARTBEAT] state=healthy rtt_ms=1 misses=0'],2000),clock:'uptime'};
 const analysis=buildReportAnalysis(current);
 assert.equal(analysis.routines.length,2);assert.equal(analysis.unknown.length,0);assert.equal(analysis.incidents.length,0);
});
async function withDocument(fn){const previous=globalThis.document;globalThis.document={createElement:tag=>new Element(tag)};try{await fn();}finally{globalThis.document=previous;}}

test('routine traffic is grouped and never sent one line at a time to the fault model',()=>withDocument(async()=>{
 const report=packet('normal',['[BDI][STATUS] reasoning_cycle=ACTIVE devices=1 sd_state=ready','[BDI][STATE] device=sw1 state=NORMAL active_faults=0 locked=false faults=none','[BDI][RECEIPT] device=sw1 result=ACCEPTED epoch=1 seq=1']);
 const root=new Element();const {analysis,ready}=renderReportAI(report,root,()=>assert.fail('routine logs must not call model'));await ready;
 assert.equal(analysis.incidents.length,0);assert.equal(analysis.routines.length,3);assert.equal(analysis.unknown.length,0);
 assert.ok(flatten(root).some(node=>node.textContent==='本包以常规运行记录为主'));
 assert.ok(flatten(root).some(node=>node.textContent==='常规运行记录 · 3 条'));
}));
test('uncertain model output keeps useful evidence and ACK SUCCESS never becomes recovery',()=>withDocument(async()=>{
 const report=packet('p1',[command,ack+' <script>raw</script>']);const root=new Element();let classified;
 const {analysis,ready}=renderReportAI(report,root,async(text,index,id)=>{classified={text,index,id};return{experimental:true,accepted:false,score:.7,threshold:.9};});await ready;
 assert.equal(analysis.incidents.length,1);assert.equal(analysis.incidents[0].recovered,false);assert.equal(analysis.incidents[0].state,'等待恢复验证');
 assert.deepEqual(classified,{text:command,index:0,id:'p1'});
 const nodes=flatten(root);assert.ok(nodes.some(node=>node.textContent.includes('匹配不足')));
 assert.ok(nodes.some(node=>node.tag==='pre'&&node.textContent.includes('<script>raw</script>')));
 assert.ok(nodes.some(node=>node.textContent==='下一步检查'));assert.ok(nodes.every(node=>node.tag!=='script'));
}));
test('one incident combines prior command, current ACK and verified recovery across saved packets',()=>{
 const previous=packet('before',[command],1000),current=packet('now',[ack,recovered],2000);
 const analysis=buildReportAnalysis(current,[previous,current]);assert.equal(analysis.incidents.length,1);
 const incident=analysis.incidents[0];assert.equal(incident.timeline.length,3);assert.equal(incident.recovered,true);assert.equal(incident.state,'已确认恢复');assert.equal(incident.anchor.evidence.report_id,'before');
});
test('different ports, incidents, boots and future packets never provide recovery for this event',()=>{
 const current=packet('p1',[command,ack],2000);
 const context=[packet('future',[recovered],3000),packet('old-boot',[recovered],1000,'other')];
 assert.equal(buildReportAnalysis(current,context).incidents[0].recovered,false);
 const mixed=buildReportAnalysis(packet('p2',[command,ack,recovered.replace('GE1/0/8','GE1/0/9')]));
 assert.equal(mixed.incidents.length,2);assert.equal(mixed.incidents.find(item=>item.component==='GE1/0/8').recovered,false);
});
test('a prior matching fault snapshot is background evidence and never invents an incident start',()=>{
 const before=packet('before',['[BDI][STATE] device=sw1 state=FAULT active_faults=1 faults=GE1/0/8:LINK_DOWN'],1000);
 const current=packet('now',[recovered],2000);const incident=buildReportAnalysis(current,[before]).incidents[0];
 assert.equal(incident.timeline.length,2);assert.match(incident.timeline[0].phase,/未绑定事件/);assert.equal(incident.recovered,true);assert.equal(incident.anchor.phase,'recovered');
 const alone=buildReportAnalysis(packet('snapshot',before.lines.map(line=>line.text),2000));assert.equal(alone.incidents[0].recovered,false);
});
test('unlinked ACK stays separate and the last successful ACK still waits for recovery',()=>{
 const unlinked=buildReportAnalysis(packet('p1',[ack]));assert.equal(unlinked.incidents[0].linked,false);assert.equal(unlinked.incidents[0].recovered,false);
 const retry=buildReportAnalysis(packet('p2',[command,ack.replace('SUCCESS','ATTEMPT_FAILED'),ack]));
 assert.equal(retry.incidents.length,1);assert.equal(retry.incidents[0].state,'等待恢复验证');
});
test('active fault snapshots get an explanation without inventing an incident or history',()=>{
 const report=packet('p1',['[BDI][STATE] device=sw1 state=FAULT active_faults=1 locked=false faults=fan1:FAN_FAILURE']);
 const item=buildReportAnalysis(report).incidents[0];assert.match(item.title,/风扇/);assert.equal(item.state,'期末仍跟踪告警');assert.equal(item.linked,false);assert.equal(item.incident,'');assert.equal(item.timeline[0].phase,'期末状态快照');
});
test('rejected input and storage trouble remain visible rather than disappearing into routine records',()=>{
 const report=packet('p1',['[BDI][RECEIPT] device=sw1 result=REJECTED','[BDI][STATUS] reasoning_cycle=ACTIVE sd_state=write_error']);
 const analysis=buildReportAnalysis(report);assert.equal(analysis.diagnostics.length,2);assert.match(analysis.diagnostics.join(' '),/输入帧被拒绝/);assert.match(analysis.diagnostics.join(' '),/write_error/);
});
test('high-confidence model output contradicting the protocol phase is rejected',()=>withDocument(async()=>{
 const root=new Element();const {ready}=renderReportAI(packet('p1',[command]),root,async()=>({experimental:true,accepted:true,truncated:false,label:'auth_lock',score:.99,threshold:.9}));await ready;
 assert.ok(flatten(root).some(node=>node.textContent.includes('分类与日志阶段不一致')));
}));
test('firmware embeds isolated shared incident view and accepts interval packets over 64 lines',()=>{
 const html=readFileSync(new URL('../device/archive.html',import.meta.url),'utf8');
 const scripts=['fault-explainer.js','model-descriptions.js','report-analysis.js','report-ai-view.js'].map(name=>readFileSync(new URL('../shared/'+name,import.meta.url),'utf8').replace(/\n$/,'').split('\n').filter(line=>!line.startsWith('import ')).join('\n').replaceAll('export function ','function ').replaceAll('export const ','const ')).join('\n');
 const expected=html.replace("import {renderReportAI} from '../shared/report-ai-view.js';",'const {renderReportAI}=(()=>{\n'+scripts+'\nreturn {renderReportAI};\n})();');
 const header=readFileSync(new URL('../../src/generated/archive_page.h',import.meta.url),'utf8');assert.equal(header.split('R"ESP_PAGE(')[1].split(')ESP_PAGE";')[0],expected);assert.doesNotMatch(expected,/import \{|https?:\/\/[^'\s]*\.js/);assert.match(expected,/lines.length>2048/);
 for(const[label,[title,meaning]]of Object.entries(modelDescriptions)){assert.ok(expected.includes(label));assert.ok(expected.includes(title));assert.ok(expected.includes(meaning));}
});
test('database timestamps do not correlate with uptime clocks',async()=>{
 const {explainRecords}=await import('../shared/fault-explainer.js');
 const cmd={boot_id:'b1',unix_ms:1000,text:command},response={boot_id:'b1',unix_ms:1100,text:ack};
 assert.match(explainRecords([cmd,response])[1].title,/端口/);
 assert.doesNotMatch(explainRecords([{...cmd,unix_ms:undefined,uptime_ms:1000},response])[1].title,/端口链路中断/);
});
test('one-click recent analysis retains earlier faults even when latest packet is normal',()=>{
 const before=packet('before',[command,ack,recovered],1000),latest=packet('latest',['[BDI][STATE] device=sw1 state=NORMAL active_faults=0 faults=none'],2000);
 assert.equal(buildReportAnalysis(latest,[before]).incidents.length,0);
 const recent=buildReportAnalysis(latest,[before],{recent:true});assert.equal(recent.incidents.length,1);assert.equal(recent.incidents[0].recovered,true);assert.equal(recent.rawCount,4);assert.equal(recent.contextPackets,2);assert.equal(recent.snapshots[0].state,'NORMAL');
});
test('recent window includes different boots without correlating commands and ACKs across reboots',()=>{
 const before=packet('old',[command],1000,'old-boot'),latest=packet('new',[ack],2000,'new-boot');
 const recent=buildReportAnalysis(latest,[before],{recent:true});assert.equal(recent.incidents.length,2);assert.equal(recent.incidents.filter(item=>item.recovered).length,0);assert.equal(recent.incidents.find(item=>!item.linked).items.length,1);
 const future=packet('future',[recovered],3000);assert.equal(buildReportAnalysis(latest,[before,future],{recent:true}).contextPackets,2);
});
test('explicit recent analysis invokes the model for routine evidence and reports real progress',()=>withDocument(async()=>{
 const root=new Element(),updates=[],calls=[];const report=packet('normal',['[BDI][STATE] device=sw1 state=NORMAL active_faults=0 faults=none','[BDI][STATUS] reasoning_cycle=ACTIVE sd_state=ready']);
 const {ready}=renderReportAI(report,root,async text=>{calls.push(text);return{experimental:true,accepted:true,truncated:false,label:text.includes('[STATE]')?'state_snapshot':'runtime_status',score:.99,threshold:.9,model:'tiny-log-v3-protocol'};},{recent:true,onProgress:value=>updates.push(value)});
 const result=await ready;assert.equal(calls.length,2);assert.equal(result.accepted,2);assert.equal(result.completed,2);assert.equal(updates[0].completed,0);assert.equal(updates.at(-1).completed,2);assert.ok(flatten(root).some(node=>node.className==='analysis-model-status'&&node.textContent.includes('采用 2 条')));
}));
test('recent model progress distinguishes abstention and interface failure from accepted output',()=>withDocument(async()=>{
 const root=new Element();let call=0;const report=packet('normal',['[BDI][STATE] device=sw1 state=NORMAL active_faults=0 faults=none','[BDI][STATUS] reasoning_cycle=ACTIVE sd_state=ready']);
 const {ready}=renderReportAI(report,root,async()=>{if(call++===0)return{experimental:true,accepted:false};throw Error('offline');},{recent:true});
 const result=await ready;assert.equal(result.accepted,0);assert.equal(result.rejected,1);assert.equal(result.failed,1);assert.ok(flatten(root).some(node=>node.textContent.includes('调用失败 1 条')));
}));
