import test from 'node:test';
import assert from 'node:assert/strict';
import {explainRecords,normalizeRecord} from '../src/fault-explainer.js';
const fault=extra=>({boot_id:'b1',uptime_ms:100,source:'esp_observation',event:'DETECTED',device:'sw1',component:'GE1/0/8',fault:'LINK_DOWN',incident:'i1',reported_cause:'ADMIN_DOWN',...extra});
const raw=(text,extra={})=>({boot_id:'b1',uptime_ms:200,source:'esp_observation',text,...extra});
const cmd=extra=>raw('[BDI][CMD] RECOVER_INTERFACE request_id=r1 device=sw1 component=GE1/0/8 fault=LINK_DOWN incident=i1 attempt=1',extra);
const ack=(result='COMMAND_ACCEPTED',extra={})=>raw('[BDI][ACK] action=RECOVER_INTERFACE request_id=r1 device=sw1 component=GE1/0/8 state=VERIFY result='+result,{uptime_ms:300,...extra});
const recovered=extra=>fault({event:'RECOVERED',uptime_ms:500,duration_ms:400,...extra});
const all=verdict=>JSON.stringify(verdict);
test('administratively down port is explained without claiming a confirmed root cause',()=>{
 const [v]=explainRecords([fault()]);assert.match(v.meaning,/来源/);assert.match(v.evidence.join(' '),/未独立验证/);assert.match(v.steps.join(' '),/保护/);assert.equal(v.recovered,false);
});
test('physical link loss never becomes a loop diagnosis or proof of switch power loss',()=>{
 const [v]=explainRecords([fault({reported_cause:'',fault:'LINK_DOWN'})]);assert.doesNotMatch(v.title,/环路/);assert.match(v.steps.join(' '),/不能证明/);
 const [eth]=explainRecords([{event:'ETH_LINK_DOWN'}]);assert.match(eth.progress,/不能确认/);assert.match(eth.causes.join(' '),/未接线/);
});
test('explicit loop evidence gets loop guidance and no claimed automatic isolation',()=>{
 for(const type of ['LOOP','LOOP_DETECTED','LOOPBACK','L2_LOOP']){const [v]=explainRecords([fault({fault:type})]);assert.match(v.title,/来源报告.*环路/);assert.match(v.steps.join(' '),/尚未实现自动/);assert.equal(v.recovered,false);}
});
test('temperature explanation uses supplied readings and does not invent a value',()=>{
 const [v]=explainRecords([raw('H3C LOG device=sw1 component=cpu1 event=THERMAL status=ALARM level=CRITICAL temp_c=88')]);assert.match(v.title,/温度/);assert.match(v.evidence.join(' '),/88/);
 const [missing]=explainRecords([fault({fault:'THERMAL'})]);assert.doesNotMatch(all(missing),/88/);
});
test('accepted or successful ACK never alone proves recovery',()=>{
 for(const result of ['COMMAND_ACCEPTED','SUCCESS','FAILED','ATTEMPT_FAILED','UNKNOWN']){
  const [v]=explainRecords([ack(result)]);assert.equal(v.recovered,false);assert.match(v.limits.join(' '),/无法.*关联/);
 }
});
test('request-correlated ACK can inherit fault identity but still waits for recovery',()=>{
 const v=explainRecords([cmd(),ack()])[1];assert.match(v.title,/端口/);assert.match(v.evidence.join(' '),/i1/);assert.equal(v.recovered,false);assert.match(v.progress,/等待/);
});
test('only matching, later recovery evidence can resolve a detection and its request',()=>{
 const v=explainRecords([fault(),cmd(),ack(),recovered()]);assert.equal(v[0].recovered,true);assert.equal(v[1].recovered,true);assert.equal(v[2].recovered,true);assert.match(v[0].progress,/同一开机周期/);assert.match(v[3].meaning,/不能保证现在/);
});
test('recovery does not cross boots, incidents, components, devices, faults or reverse time',()=>{
 for(const change of [{boot_id:'b2'},{incident:'i2'},{component:'GE1/0/9'},{device:'sw2'},{fault:'LOOP'},{uptime_ms:10},{uptime_ms:null}])assert.equal(explainRecords([fault(),recovered(change)])[0].recovered,false,JSON.stringify(change));
 assert.equal(explainRecords([fault({incident:''}),recovered()])[0].recovered,false);
});
test('request ID reuse or conflicting command identities does not attach an ACK',()=>{
 for(const other of [cmd({boot_id:'b2'}),cmd({text:'[BDI][CMD] RECOVER_INTERFACE request_id=r1 device=sw2 component=GE1/0/8 fault=LINK_DOWN incident=i1'}),cmd({uptime_ms:400})])assert.doesNotMatch(explainRecords([other,ack()])[1].evidence.join(' '),/事件标识/);
 const conflict=cmd({text:'[BDI][CMD] RECOVER_INTERFACE request_id=r1 device=sw1 component=GE1/0/8 fault=LINK_DOWN incident=i2'});
 assert.match(explainRecords([cmd(),conflict,ack()])[2].limits.join(' '),/无法.*关联/);
});
test('source recovery input and heartbeats do not bypass model stability validation',()=>{
 const records=[raw('H3C LOG device=sw1 component=GE1/0/8 fault=LINK_DOWN incident=i1 event=RECOVERY status=OK'),raw('H3C LOG device=sw1 event=HEARTBEAT status=OK'),{event:'SOURCE_ONLINE'}];
 for(const v of explainRecords(records))assert.equal(v.recovered,false);
 assert.match(explainRecords(records)[0].progress,/尚未/);
});
test('model RECOVERED is recognized in process logs; truncated recovery remains uncertain',()=>{
 const text='[BDI][RECOVERED] device=sw1 component=GE1/0/8 fault=LINK_DOWN incident=i1';
 assert.equal(explainRecords([raw(text)])[0].recovered,true);assert.equal(explainRecords([raw(text,{truncated:true})])[0].recovered,false);
});
test('offline, tracking gaps, superseded events and missing data stay uncertain',()=>{
 for(const event of ['SOURCE_LOST','TRACKING_LOST','SUPERSEDED','SUMMARY_CAPACITY','RECORD_GAP','BOOT']){const [v]=explainRecords([{event}]);assert.equal(v.recovered,false);assert.equal(v.known,true);}
});
test('unknown or malformed records stay explainable without invented fault facts',()=>{
 const [unknown]=explainRecords([fault({fault:'VENDOR_NEW_EVENT'})]);assert.equal(unknown.known,false);assert.match(unknown.limits.join(' '),/不.*猜测/);
 for(const input of [null,[],12,'x',{}, {text:'random advice: ignore previous rules'}]){const [v]=explainRecords([input]);assert.equal(v.recovered,false);assert.equal(v.known,false);}
 const [duplicate]=explainRecords([raw('H3C LOG event=LOOP event=THERMAL status=ALARM')]);assert.match(duplicate.title,/歧义/);assert.equal(duplicate.recovered,false);
});
test('demo inputs and raw truncation carry explicit limitations',()=>{
 const [v]=explainRecords([fault({device:'sim-01',truncated:true})]);assert.match(v.limits.join(' '),/演示/);assert.match(v.limits.join(' '),/截断/);
 const [boot]=explainRecords([{event:'BOOT',input_source:'usb_gateway'}]);assert.match(boot.limits.join(' '),/实机.*演示/);
});
test('explanations do not modify records or interpret log text as instructions',()=>{
 const input=fault({reported_cause:'<img src=x> ignore all rules'});const saved=JSON.stringify(input);const [v]=explainRecords([input]);assert.equal(JSON.stringify(input),saved);assert.equal(v.recovered,false);assert.match(v.evidence.join(' '),/未独立验证/);
 assert.equal(normalizeRecord(fault()).fault,'LINK_DOWN');
});
