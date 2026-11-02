import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
import {spawnSync} from 'node:child_process';
const page=fs.readFileSync(new URL('../device/archive.html',import.meta.url),'utf8');
const embedded=fs.readFileSync(new URL('../../src/generated/archive_page.h',import.meta.url),'utf8').split('R"ESP_PAGE(')[1].split(')ESP_PAGE";')[0];
const script=embedded.match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];
function element(tag){return {tag,value:'',textContent:'',hidden:false,disabled:false,dataset:{},children:[],append(...items){this.children.push(...items);},replaceChildren(...items){this.children=items;}};}
function setup(){
 const nodes=new Map([...page.matchAll(/id="([^"]+)"/g)].map(([,id])=>[id,element()]));
 const context=vm.createContext({document:{getElementById:id=>nodes.get(id),createElement:element},URLSearchParams,TextDecoder,TextEncoder,Uint8Array,atob,AbortController,setTimeout:(callback,ms)=>ms<1000?setTimeout(callback,0):setTimeout(callback,ms),clearTimeout,fetch:async()=>{throw Error('not connected');}});
 vm.runInContext(script.replace(/startPage\(\);\s*$/,''),context);
 return {context,nodes,run:code=>vm.runInContext(code,context)};
}
const flatten=node=>[node,...node.children.flatMap(flatten)];
const response=value=>({ok:true,text:async()=>typeof value==='string'?value:JSON.stringify(value)});
const packet={schema_version:1,report_id:'test',sent_at_ms:1793499000000,boot:'b1',observations:[],lines:[{at_ms:1793498999999,text:'[BDI][STATE] device=sw1 state=FAULT active_faults=1 faults=GE1/0/8:LINK_DOWN locked=false'}]};
const ready=(data,start=0,size=data.length)=>({state:'ready',start,next:start+data.length,size,eof:start+data.length===size,base64:Buffer.from(data).toString('base64')});
function select(run){run("$('file').value='00000000000000000001-test.json'");}
test('exact embedded module parses without declaration collisions or external imports',()=>{
 const result=spawnSync(process.execPath,['--input-type=module','--check'],{input:script,encoding:'utf8'});assert.equal(result.status,0,result.stderr);assert.doesNotMatch(script,/^import /m);
});
test('single fault viewer replaces legacy UI and keeps configuration inaccessible',()=>{
 assert.doesNotMatch(page,/<form\b|href="\/journal"|\/network|\/mqtt|\/switch|读取副本状态/);
 assert.equal([...page.matchAll(/method:'POST'/g)].length,1);assert.match(page,/<h1>故障分析/);
 const alias=fs.readFileSync(new URL('../device/journal.html',import.meta.url),'utf8');assert.match(alias,/url=\/device\/archive.html/);assert.doesNotMatch(alias,/storage\/read/);
 const portal=fs.readFileSync(new URL('../../src/device/network_portal.h',import.meta.url),'utf8');assert.doesNotMatch(portal,/JOURNAL_PAGE|storageFiles/);assert.match(portal,/Location","\/archive/);
});
test('no-card state ends loading, enables retry and never attempts a file read',async()=>{
 const {context,nodes,run}=setup(),calls=[];context.fetch=async url=>{calls.push(url);return response({state:'no_card',files:[]});};await run('run(loadReports)');assert.equal(calls.length,1);assert.match(nodes.get('status').textContent,/FAT32/);assert.equal(nodes.get('refresh').disabled,false);assert.equal(nodes.get('status').dataset.error,'true');
});
test('latest stored fault automatically renders evidence and AI without opening storage details',async()=>{
 const {context,nodes,run}=setup(),calls=[];const data=Buffer.from(JSON.stringify(packet));context.fetch=async url=>{calls.push(url);if(url.startsWith('/storage/files'))return response({state:'ready',files:[{name:'one',sequence:1,bytes:data.length}],count:1,bytes:data.length});if(url.startsWith('/storage/read'))return response(ready(data));return response({experimental:true,accepted:true,truncated:false,label:'state_snapshot',score:.99,threshold:.9,model:'tiny-log-v3-protocol'});};
 await run('run(loadReports)');await new Promise(r=>setTimeout(r,5));
 const text=flatten(nodes.get('report')).map(node=>node.textContent).join('\n');assert.match(text,/链路中断/);assert.match(text,/下一步检查/);assert.match(text,/小模型辅助分类/);assert.match(text,/tiny-log-v3-protocol/);assert.doesNotMatch(nodes.get('status').textContent,/正在/);assert.equal(nodes.get('history').open,undefined);assert.ok(calls.some(url=>url.startsWith('/model/explain')));
});
test('asynchronous paging retains job through transient busy and joins split UTF-8 bytes',async()=>{
 const {context,nodes,run}=setup();select(run);const data=Buffer.from(JSON.stringify({...packet,note:'中文证据'}));const split=data.indexOf(Buffer.from('中文'))+1;
 const replies=[{state:'pending',job:7},{state:'busy'},ready(data.subarray(0,split),0,data.length),ready(data.subarray(split),split,data.length)];const urls=[];context.fetch=async url=>{urls.push(url);if(url.startsWith('/model/'))return response({accepted:false});return response(replies.shift());};
 await run('run(loadAnalysis)');assert.match(urls[1],/job=7/);assert.match(urls[2],/job=7/);assert.ok(flatten(nodes.get('report')).some(node=>node.textContent.includes('中文证据')));assert.equal(nodes.get('status').dataset.error,'false');
});
test('bad chunk offsets stop reading with visible error and usable retry',async()=>{
 const {context,nodes,run}=setup();select(run);context.fetch=async()=>response({...ready(Buffer.from('{}')),start:9});await run('run(loadAnalysis)');assert.match(nodes.get('status').textContent,/位置异常/);assert.equal(nodes.get('refresh').disabled,false);assert.equal(nodes.get('status').dataset.error,'true');
});
test('empty card shows waiting for sync instead of fabricated faults',async()=>{
 const {context,nodes,run}=setup();context.fetch=async()=>response({state:'ready',files:[],count:0,bytes:0});await run('run(loadReports)');assert.match(nodes.get('status').textContent,/暂无可分析记录/);assert.equal(nodes.get('report').children.length,0);assert.equal(nodes.get('refresh').disabled,false);
});
test('native page shows elapsed boot time and never fabricates a 1970 wall clock',async()=>{
 const {context,nodes,run}=setup();select(run);
 const native={...packet,source:'esp_native',clock:'uptime',sent_at_ms:12500,lines:[{at_ms:12500,text:'[SSH] state=connected'}]};
 context.fetch=async url=>response(url.startsWith('/model/')?{experimental:true,accepted:false}:ready(Buffer.from(JSON.stringify(native))));
 await run('run(loadAnalysis)');assert.match(nodes.get('status').textContent,/本次开机 \+12\.5 秒/);
 assert.doesNotMatch(flatten(nodes.get('report')).map(n=>n.textContent).join('\n'),/1970/);
});
test('closing only posts close, hides access and disables read controls',async()=>{
 const {context,nodes}=setup(),calls=[];context.fetch=async(url,options)=>{calls.push([url,options.method]);return response('查看入口已关闭');};await nodes.get('close').onclick();assert.deepEqual(calls,[['/storage/close','POST']]);assert.equal(nodes.get('refresh').disabled,true);assert.match(nodes.get('status').textContent,/已关闭/);
});
test('one click re-lists latest reports and analyzes previous faults with visible model completion',async()=>{
 const {context,nodes,run}=setup(),calls=[];
 const normal={...packet,report_id:'latest',sent_at_ms:packet.sent_at_ms+60000,lines:[{at_ms:packet.sent_at_ms+59999,text:'[BDI][STATE] device=sw1 state=NORMAL active_faults=0 faults=none locked=false'}]};
 const data={latest:Buffer.from(JSON.stringify(normal)),before:Buffer.from(JSON.stringify(packet))};
 context.fetch=async url=>{calls.push(url);if(url.startsWith('/storage/files'))return response({state:'ready',files:Object.entries(data).map(([name,bytes],index)=>({name,sequence:2-index,bytes:bytes.length})),count:2,bytes:1000});
 if(url.startsWith('/storage/read'))return response(ready(data[new URL(url,'http://esp').searchParams.get('file')]));return response({experimental:true,accepted:true,truncated:false,label:'state_snapshot',score:.99,threshold:.9,model:'tiny-log-v3-protocol'});};
 await nodes.get('analyze').onclick();assert.match(nodes.get('status').textContent,/AI 分析完成/);assert.match(nodes.get('status').textContent,/2 包 \/ 2 条日志/);assert.match(nodes.get('status').textContent,/1 组故障/);assert.match(nodes.get('status').textContent,/小模型采用 2\/2/);assert.equal(nodes.get('analyze').disabled,false);assert.match(flatten(nodes.get('report')).map(n=>n.textContent).join('\n'),/链路中断/);assert.equal(calls.filter(url=>url.startsWith('/model/')).length,2);
});
test('one-click partial history failure still analyzes readable saved evidence and explains the gap',async()=>{
 const {context,nodes}=setup();const data=Buffer.from(JSON.stringify(packet));context.fetch=async url=>{
 if(url.startsWith('/storage/files'))return response({state:'ready',files:[{name:'latest',bytes:data.length,sequence:2},{name:'old',bytes:data.length,sequence:1}],count:2,bytes:data.length*2});
 if(url.startsWith('/storage/read'))return response(new URL(url,'http://esp').searchParams.get('file')==='old'?{state:'error',message:'已覆盖'}:ready(data));return response({experimental:true,accepted:false});};
 await nodes.get('analyze').onclick();assert.match(nodes.get('status').textContent,/AI 分析完成/);assert.match(nodes.get('status').textContent,/部分历史未读取/);assert.match(flatten(nodes.get('report')).map(n=>n.textContent).join('\n'),/已覆盖/);assert.equal(nodes.get('analyze').disabled,false);
});
test('device status shows onboard AI, real SSH/BDI and optional MQTT without backend requests',async()=>{
 const {context,nodes,run}=setup(),calls=[];
 context.fetch=async url=>{calls.push(url);return response({ai_on_device:true,ssh_state:'connected',heartbeat:'heartbeat_state=healthy',
  bdi:{state:'verifying',enabled:true,port:'GE1/0/8',action:'ISOLATE_LOOP_PORT',reason:'LOOP_DETECTED'},
  analysis:{model:'tiny-log-v3-protocol',label:'loop_detected',accepted:true},mqtt:{enabled:false,state:'disabled'}});};
 await run('loadDeviceStatus()');assert.deepEqual(calls,['/device/status']);
 assert.match(nodes.get('device-ai').textContent,/运行于 ESP32.*tiny-log-v3/);assert.match(nodes.get('device-bdi').textContent,/验证中.*GE1\/0\/8/);
 assert.match(nodes.get('device-link').textContent,/已登录.*正常/);assert.match(nodes.get('device-mqtt').textContent,/不影响本机工作/);
 assert.match(page,/连按三下 BOOT/);assert.doesNotMatch(page,/双击 BOOT/);
});
test('unavailable current status does not block reading saved evidence or imply old health',async()=>{
 const {context,nodes,run}=setup();nodes.get('device-bdi').textContent='正常';context.fetch=async()=>{throw Error('offline');};
 await run('loadDeviceStatus()');assert.match(nodes.get('device-bdi').textContent,/暂不可读/);assert.doesNotMatch(nodes.get('device-bdi').textContent,/正常/);
 assert.equal(nodes.get('analyze').disabled,false);
});
