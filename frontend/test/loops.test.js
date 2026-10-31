import test from 'node:test';
import assert from 'node:assert/strict';
import {loopIncidents,demoLoopReports} from '../src/loops.js';
const common='device=sw1 port=GE1/0/8 fault=LOOP incident=loop-1';
const detection=`H3C LOG ${common} event=LOOP_DETECTED`;
const command=`[BDI][CMD] ISOLATE_LOOP_PORT ${common} request_id=boot-1`;
const ack=`[BDI][ACK] ${common} request_id=boot-1 result=SUCCESS`;
const recovered=`[BDI][RECOVERED] ${common} verified=true`;
const reports=(...texts)=>[{report_id:'test',lines:texts.map((text,i)=>({text,at_ms:1000+i}))}];
test('explicit evidence advances through isolation and verification; ACK is not recovery',()=>{
 let [incident]=loopIncidents(reports(detection,command,ack));
 assert.equal(incident.phase,'verifying');assert.equal(incident.timeline.length,3);
 [incident]=loopIncidents(reports(detection,command,ack,recovered));assert.equal(incident.phase,'resolved');
});
test('ordinary port faults, unlocks and heartbeat never become loop incidents',()=>{
 assert.deepEqual(loopIncidents(reports('H3C LOG device=sw1 component=GE1 fault=LINK_DOWN event=FAULT',
 '[BDI][CMD] RECOVER_INTERFACE device=sw1 component=GE1 request_id=x','[BDI][ACK] device=sw1 component=GE1 result=SUCCESS')),[]);
});
test('wrong device, port, request or incident cannot advance the current incident',()=>{
 for(const bad of [ack.replace('sw1','sw2'),ack.replace('GE1/0/8','GE1/0/9'),ack.replace('boot-1','boot-old'),ack.replace('loop-1','loop-other')]){
  const list=loopIncidents(reports(detection,command,bad));assert.equal(list.length,1);assert.equal(list[0].phase,'isolating');
 }
});
test('no implicit recovery from healthy heartbeats or generic RECOVERED',()=>{
 const [incident]=loopIncidents(reports(detection,command,ack,recovered.replace(' verified=true',''),'H3C LOG device=sw1 component=system event=HEARTBEAT status=OK'));
 assert.equal(incident.phase,'verifying');
});
test('completed incident cannot regress on a late ACK or repeated detection',()=>{
 const [incident]=loopIncidents(reports(detection,command,ack,recovered,ack,detection));assert.equal(incident.phase,'resolved');assert.equal(incident.timeline.length,4);
});
test('failure and protection blocks are visible; duplicate archive lines are deduplicated',()=>{
 let [incident]=loopIncidents(reports(detection,command,ack.replace('SUCCESS','FAILED')));assert.equal(incident.phase,'failed');
 [incident]=loopIncidents(reports(detection,`[BDI][LOOP] ${common} stage=ASSESS allowed=false`));assert.equal(incident.phase,'blocked');
 const source=reports(detection,command,ack);assert.equal(loopIncidents([...source,...source])[0].timeline.length,3);
});
test('out-of-order reports are sorted by original evidence time',()=>{
 const source=reports(detection,command,ack,recovered)[0];
 const parts=source.lines.map((line,i)=>({report_id:String(i),lines:[line]})).reverse();assert.equal(loopIncidents(parts)[0].phase,'resolved');
});
test('demo is explicitly synthetic and isolated from real empty state',()=>{
 const demo=loopIncidents(demoLoopReports(50000));assert.equal(demo[0].source,'switch-demo');assert.equal(demo[0].phase,'resolved');assert.deepEqual(loopIncidents([]),[]);
});
