import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {assessLoopStorms} from '../shared/loop-storm.js';
import {buildReportAnalysis} from '../shared/report-analysis.js';
import {renderReportAI} from '../shared/report-ai-view.js';

// Extracted unchanged from the r5 physical H3C test. Device clock is unset.
const fixture=JSON.parse(readFileSync(new URL('./fixtures/h3c-loop-storm.json',import.meta.url)));
const clone=()=>structuredClone(fixture);
const transform=fn=>clone().map(r=>({...r,lines:r.lines.map(l=>({...l,text:fn(l.text)}))}));
test('real H3C capture marks suspected storm with ports and raw evidence, never confirmed storm',()=>{
 const risks=assessLoopStorms(fixture);assert.equal(risks.length,1);
 const risk=risks[0];assert.equal(risk.state,'suspected');assert.equal(risk.historical,true);assert.equal(risk.vlan,10);
 assert.deepEqual(risk.ports,['GigabitEthernet1/0/2','GigabitEthernet1/0/3']);
 assert.match(risk.title,/疑似环路风暴/);assert.match(risk.explanation,/缺少广播速率/);
 assert.match(risk.limitation,/不代表当前.*不证明 ESP 已隔离/);
 assert.ok(risk.evidence.some(e=>e.text.includes('state=lost')));
 assert.ok(risk.evidence.some(e=>e.text==='[SSH] state=disconnected'));
});
test('SSH and ping loss alone do not mark a loop or storm',()=>{
 const reports=clone().map(r=>({...r,lines:r.lines.filter(l=>!l.text.includes('LPDT/'))}));
 assert.deepEqual(assessLoopStorms(reports),[]);
});
test('loop alone remains confirmed loop with storm evidence insufficient',()=>{
 const reports=clone().map(r=>({...r,lines:r.lines.filter(l=>!l.text.startsWith('[BDI][HEARTBEAT]'))}));
 assert.equal(assessLoopStorms(reports)[0].state,'loop_confirmed');
 assert.doesNotMatch(assessLoopStorms(reports)[0].title,/疑似环路风暴/);
});
test('old loop replay cannot be combined with a new management outage',()=>{
 const reports=transform(t=>t.includes('LPDT/')?t.replace('05:10:47','04:10:47'):t);
 assert.equal(assessLoopStorms(reports)[0].state,'loop_confirmed');
});
test('boot, target and local IP boundaries prevent unrelated loss correlation',()=>{
 for(const reports of [
  clone().map(r=>({...r,boot:r.lines.some(l=>l.text.includes('LPDT/'))?'different-boot':r.boot})),
  transform(t=>t.startsWith('[BDI][HEARTBEAT]')?t.replace('target=192.168.10.2','target=192.168.10.99'):t),
  transform(t=>t.includes('SHELL_LOGOUT:')?t.replace('from 192.168.10.1.','from 192.168.10.99.'):t),
 ])assert.equal(assessLoopStorms(reports)[0].state,'loop_confirmed');
});
test('unknown log source and mixed clock bases cannot provide storm evidence',()=>{
 for(const reports of [fixture.map(r=>({...r,source:'other'})),fixture.map(r=>({...r,clock:'epoch'}))])assert.deepEqual(assessLoopStorms(reports),[]);
});
test('duplicate logbuffer output does not multiply loop badges',()=>{
 const reports=[...fixture,...fixture.map(r=>({...r,report_id:r.report_id+'-repeat'}))];
 const [risk]=assessLoopStorms(reports);assert.equal(assessLoopStorms(reports).length,1);assert.equal(risk.ports.length,2);
 assert.equal(risk.evidence.filter(e=>e.text.includes('LPDT_VLAN_LOOPED:')).length,2);
});
test('analysis never uses future packets to add a historical risk',()=>{
 const latest=fixture[0],future=fixture.slice(1);
 assert.deepEqual(buildReportAnalysis(latest,future,{recent:true}).loopRisks,[]);
 assert.equal(buildReportAnalysis(fixture.at(-1),fixture,{recent:true}).loopRisks[0].state,'suspected');
});
test('page renders the historical badge, limitation and expandable raw evidence',async()=>{
 class Element{constructor(tag){this.tag=tag;this.children=[];this.textContent='';}append(...items){this.children.push(...items);}}
 const flatten=n=>[n,...n.children.flatMap(flatten)],before=globalThis.document;
 globalThis.document={createElement:tag=>new Element(tag)};
 try{
  const root=new Element('div');const {ready}=renderReportAI(fixture.at(-1),root,async()=>({accepted:false}),{recent:true,contextReports:fixture});await ready;
  const nodes=flatten(root),text=nodes.map(n=>n.textContent).join('\n');
  assert.ok(nodes.some(n=>n.className==='loop-risk-badge'&&n.textContent==='疑似环路风暴'));
  assert.match(text,/VLAN 10/);assert.match(text,/不代表当前仍有风暴/);assert.match(text,/判断依据与关联原文/);
  assert.doesNotMatch(text,/风暴已确认|已自动消除风暴/);
 }finally{globalThis.document=before;}
});
