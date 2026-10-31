import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const page=fs.readFileSync(new URL('../journal.html',import.meta.url),'utf8');
const explainer=fs.readFileSync(new URL('../src/fault-explainer.js',import.meta.url),'utf8').replaceAll('export function ','function ');
const bundled=page.replace("import {explainRecords} from './src/fault-explainer.js';",explainer);
function element(){return {value:'',textContent:'',hidden:false,disabled:false,children:[],append(...items){this.children.push(...items);},replaceChildren(...items){this.children=items;},get childElementCount(){return this.children.length;}};}
function setup(){
 const nodes=new Map([...page.matchAll(/id="([^"]+)"/g)].map(([,id])=>[id,element()]));nodes.get('kind').value='summary';
 nodes.get('assistant-enabled').checked=true;
 const context=vm.createContext({document:{getElementById:id=>nodes.get(id),createElement:element},URLSearchParams,TextDecoder,Uint8Array,atob,AbortController,setTimeout,clearTimeout,fetch:async()=>{throw Error('not connected');}});
 vm.runInContext(bundled.match(/<script[^>]*>([\s\S]*?)<\/script>/)[1].replace(/run\(refresh\);\s*$/,''),context);
 return {context,nodes,run:code=>vm.runInContext(code,context)};
}
const response=value=>({ok:true,json:async()=>value,text:async()=>typeof value==='string'?value:new URLSearchParams(value).toString()});
test('viewer contains no configuration form or write requests other than closing itself',()=>{
 assert.doesNotMatch(page,/<form\b|\/network|\/mqtt|\/switch/);
 assert.equal([...page.matchAll(/method:'POST'/g)].length,1);assert.match(page,/\/storage\/close/);
 assert.match(page,/双击 BOOT/);assert.match(page,/青灯/);
});
test('no-card view stays usable, explains state and does not attempt file reads',async()=>{
 const {context,nodes,run}=setup();const calls=[];
 context.fetch=async url=>{calls.push(url);return url==='/storage'?response({state:'no_card',written:'0',dropped:'12',viewer_only:'1'}):response({state:'no_card',files:[]});};
 await run('run(refresh)');assert.equal(calls.length,2);assert.match(nodes.get('status').textContent,/FAT32/);assert.match(nodes.get('status').textContent,/12/);assert.equal(nodes.get('close').hidden,false);assert.equal(nodes.get('refresh').disabled,false);assert.equal(nodes.get('earlier').disabled,true);
});
test('stored text and malformed JSON render as text, preventing log-injected HTML',()=>{
 const {nodes,context,run}=setup();context.log=JSON.stringify({event:'DETECTED',message:'<img src=x onerror=alert(1)>',device:'s1',text:'<script>bad</script>',uptime_ms:1000})+'\n{partial';
 run('render(log)');const cards=nodes.get('records').children;assert.equal(cards.length,2);assert.equal(cards[0].children[2].textContent,'<img src=x onerror=alert(1)>');assert.match(cards[1].children[0].textContent,/不完整/);assert.doesNotMatch(page,/innerHTML/);
});
test('file selection filters evidence type and reads bounded paginated data',async()=>{
 const {context,nodes,run}=setup();context.filesInput=[{name:'00000000000000000003-raw.jsonl',kind:'raw',bytes:9000},{name:'00000000000000000002-summary.jsonl',kind:'summary',bytes:8000}];
 run('files=filesInput;fileOptions()');assert.equal(nodes.get('file').value,context.filesInput[1].name);
 const urls=[];context.fetch=async url=>{urls.push(url);return response({state:'ready',start:4000,next:7000,size:8000,base64:Buffer.from('{"event":"RECOVERED"}\n').toString('base64')});};
 await run('run(()=>readPage(3904))');assert.match(urls[0],/offset=3904/);assert.equal(nodes.get('records').children[0].children[0].textContent,'故障恢复');assert.equal(nodes.get('earlier').disabled,false);assert.equal(nodes.get('later').disabled,false);
 nodes.get('kind').value='raw';run("selected='';fileOptions()");assert.equal(nodes.get('file').value,context.filesInput[0].name);
});
test('read failure keeps controls usable and never overwrites settings',async()=>{
 const {context,nodes,run}=setup();run("selected='00000000000000000001-summary.jsonl'");
 context.fetch=async()=>response({state:'error',message:'记录已被覆盖，请刷新'});await run('run(()=>readPage(0))');assert.match(nodes.get('status').textContent,/覆盖/);assert.equal(nodes.get('refresh').disabled,false);
});
test('asynchronous reads retain the job ID through a transient busy snapshot',async()=>{
 const {context,nodes,run}=setup();run("selected='00000000000000000001-summary.jsonl'");
 context.setTimeout=(callback,ms)=>{if(ms===100)callback();return 1;};context.clearTimeout=()=>{};
 const urls=[];const replies=[{state:'pending',job:7},{state:'busy'},{state:'ready',start:0,next:20,size:20,base64:Buffer.from('{"event":"BOOT"}\n').toString('base64')}];
 context.fetch=async url=>{urls.push(url);return response(replies.shift());};
 await run('run(()=>readPage(0))');assert.match(urls[1],/job=7/);assert.match(urls[2],/job=7/);assert.equal(nodes.get('records').children[0].children[0].textContent,'设备启动');
});
test('firmware embeds the current standalone viewer source exactly',()=>{
 const header=fs.readFileSync(new URL('../../src/journal_page.h',import.meta.url),'utf8');
 assert.equal(header.split('R"ESP_PAGE(')[1].split(')ESP_PAGE";')[0],bundled);
 assert.doesNotMatch(bundled,/import \{|https?:\/\/[^'\s]*\.js/);
});
test('closing a view hides access and disables further read controls',async()=>{
 const {context,nodes}=setup();const calls=[];context.fetch=async(url,options)=>{calls.push([url,options.method]);return response('查看入口已关闭');};
 await nodes.get('close').onclick();assert.deepEqual(calls,[['/storage/close','POST']]);assert.equal(nodes.get('refresh').disabled,true);assert.match(nodes.get('status').textContent,/已关闭/);
});
test('offline explanations appear on records, with evidence and explicitly uncertain causes',()=>{
 const {context,nodes,run}=setup();context.log=JSON.stringify({boot_id:'b1',uptime_ms:20,event:'DETECTED',device:'sw1',component:'GE1/0/8',incident:'i1',fault:'LINK_DOWN',reported_cause:'ADMIN_DOWN'});
 run('render(log)');const panel=nodes.get('records').children[0].children.find(child=>child.className==='interpretation');
 assert.ok(panel);assert.equal(panel.hidden,false);assert.match(panel.children[0].textContent,/离线解释/);assert.match(panel.children[1].textContent,/来源/);assert.match(nodes.get('assistant-state').textContent,/1 条有匹配规则/);
 assert.equal(panel.children[3].children[1].textContent,'可能原因（待核对）');
});
test('explanations can be hidden and shown without additional network requests',()=>{
 const {context,nodes,run}=setup();context.log=JSON.stringify({event:'SOURCE_LOST'});let requests=0;context.fetch=()=>{requests++;throw Error('unexpected');};
 run('render(log)');nodes.get('assistant-enabled').checked=false;nodes.get('assistant-enabled').onchange();
 assert.equal(nodes.get('records').children[0].children.find(child=>child.className==='interpretation').hidden,true);
 nodes.get('assistant-enabled').checked=true;nodes.get('assistant-enabled').onchange();assert.equal(requests,0);
});
test('valid JSON primitives and null are shown as abnormal records without blocking other explanations',()=>{
 const {context,nodes,run}=setup();context.log='null\n[]\n42\n'+JSON.stringify({event:'SOURCE_ONLINE'});run('render(log)');
 assert.equal(nodes.get('records').children.length,4);assert.match(nodes.get('assistant-state').textContent,/3 条不完整或异常/);
});
function modelButton(nodes){
 const panel=nodes.get('records').children[0].children.find(child=>child.className==='interpretation');
 return {button:panel.children[4].children[0],result:panel.children[4].children[1],panel};
}
test('trained model receives one log via a read-only local request and never changes recovery evidence',async()=>{
 const {context,nodes,run}=setup();context.log=JSON.stringify({event:'ACTION_EVIDENCE',text:'[BDI][ACK] device=sw1 result=SUCCESS action=UNLOCK'});run('render(log)');
 const {button,result,panel}=modelButton(nodes);const progress=panel.children[2].textContent;const calls=[];
 context.fetch=async(url,options)=>{calls.push([url,options.method]);return response({experimental:true,accepted:true,truncated:false,label:'action_ack',score:.99,threshold:.9});};
 await button.onclick();assert.equal(calls.length,1);assert.match(calls[0][0],/^\/model\/explain\?log=/);assert.equal(calls[0][1],undefined);
 assert.match(result.textContent,/实验模型识别：处置回执/);assert.match(result.textContent,/回执到达不等于恢复/);assert.match(result.textContent,/不是根因正确率/);assert.equal(panel.children[2].textContent,progress);
});
test('low confidence, malformed labels and truncated model replies explicitly abstain',async()=>{
 for(const reply of [{accepted:false,label:'thermal',score:.89},{accepted:true,label:'<script>x</script>',score:.99},{accepted:true,label:'thermal',score:.99,truncated:true},{accepted:true,label:'thermal',score:.99,threshold:undefined}]){
  const {context,nodes,run}=setup();context.log=JSON.stringify({event:'DETECTED',fault:'THERMAL'});run('render(log)');
  context.fetch=async()=>response({experimental:true,threshold:.9,...reply});const {button,result}=modelButton(nodes);await button.onclick();assert.match(result.textContent,/不能可靠解释/);
 }
});
test('model absence and timeouts leave deterministic evidence available and allow retry',async()=>{
 const {context,nodes,run}=setup();context.log=JSON.stringify({event:'DETECTED',fault:'AUTH_LOCKED'});run('render(log)');
 const {button,result,panel}=modelButton(nodes);context.fetch=async()=>({ok:false,status:503});await button.onclick();assert.match(result.textContent,/未安装实验模型/);assert.equal(button.disabled,false);assert.match(panel.children[0].textContent,/离线解释/);
 context.fetch=async()=>{const error=Error();error.name='AbortError';throw error;};await button.onclick();assert.match(result.textContent,/超时/);assert.equal(button.disabled,false);
});
test('model input omits unrelated configuration and refuses oversized records',async()=>{
 const {context,nodes,run}=setup();context.log=JSON.stringify({event:'DETECTED',fault:'AUTH_LOCKED',password:'secret',mqtt_password:'secret'});run('render(log)');
 let url;context.fetch=async value=>{url=value;return response({experimental:true,accepted:false});};await modelButton(nodes).button.onclick();assert.doesNotMatch(url,/secret|password/);
 context.log=JSON.stringify({text:'温'.repeat(400)});run('render(log)');context.fetch=async()=>{throw Error('must not fetch');};const {button,result}=modelButton(nodes);await button.onclick();assert.match(result.textContent,/1024 字节/);
});
test('H3C protections, MAC moves and login failures have distinct cautious explanations',async()=>{
 for(const [label,expected] of [['stp_protection',/缺少 BPDU 本身不能证明/],['mac_move',/MAC_FLAPPING 不能证明/],['login_failure',/失败本身不能证明账户已被锁定/],['resource_pressure',/不能直接确定是攻击、环路或硬件损坏/]]){
  const {context,nodes,run}=setup();context.log=JSON.stringify({text:'H3C test log'});run('render(log)');
  context.fetch=async()=>response({experimental:true,accepted:true,truncated:false,label,score:.99,threshold:.9,model:'tiny-log-v2-h3c'});
  const {button,result}=modelButton(nodes);await button.onclick();assert.match(result.textContent,expected);assert.match(result.textContent,/tiny-log-v2-h3c/);
 }
});
