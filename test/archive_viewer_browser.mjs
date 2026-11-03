// Run with a read-only fixture from the database. Exercises the exact firmware HTML,
// chunked SD replies and model responses in Chrome, without writing device data.
import {spawn} from 'node:child_process';
import {readFile,writeFile} from 'node:fs/promises';
import http from 'node:http';
import assert from 'node:assert/strict';
const root=new URL('../',import.meta.url),runtime=new URL('test/.runtime/',root);
const storm=process.env.ESP_STORM_FIXTURE==='1';
let fixture=JSON.parse(await readFile(new URL('fault-viewer-fixture.json',runtime),'utf8'));
if(storm){
 const packets=JSON.parse(await readFile(new URL('frontend/test/fixtures/h3c-loop-storm.json',root),'utf8'));
 fixture={normal:packets.at(-1),fault:{...packets.at(-2),report_id:'storm-context',lines:packets.slice(0,-1).flatMap(p=>p.lines)},predictions:{}};
}
const html=(await readFile(new URL('src/generated/archive_page.h',root),'utf8')).split('R"ESP_PAGE(')[1].split(')ESP_PAGE";')[0];
const latest=fixture.normal||fixture.fault;
const payloads={latest:Buffer.from(JSON.stringify(latest)),previous:Buffer.from(JSON.stringify(fixture.fault))};
const payload=payloads.latest;
let mode='fault',job=0;const reads=new Map();
const server=http.createServer((req,res)=>{
 const url=new URL(req.url,'http://localhost');res.setHeader('Cache-Control','no-store');
 if(url.pathname==='/'){res.setHeader('Content-Type','text/html; charset=utf-8');res.end(html);return;}
 res.setHeader('Content-Type','application/json');let reply;
 if(url.pathname==='/storage/files')reply=mode==='no_card'?{state:'no_card',files:[]}:{state:'ready',count:mode==='empty'?0:2,bytes:payload.length+payloads.previous.length,files:mode==='empty'?[]:[{name:'latest',sequence:2,bytes:payload.length},{name:'previous',sequence:1,bytes:payloads.previous.length}]};
 else if(url.pathname==='/storage/read'){
  if(url.searchParams.has('job')){const task=reads.get(url.searchParams.get('job'));reply=task.reply;if(!task.polled){task.polled=true;reply={state:'busy'};}}
  else {const payload=payloads[url.searchParams.get('file')];const start=Number(url.searchParams.get('offset')),next=Math.min(payload.length,start+3072),id=String(++job);reads.set(id,{polled:false,reply:{state:'ready',start,next,size:payload.length,eof:next===payload.length,base64:payload.subarray(start,next).toString('base64')}});reply={state:'pending',job:Number(id)};}
 }else if(url.pathname==='/device/status')reply={ai_on_device:true,ssh_state:'connected',heartbeat:'heartbeat_state=healthy',bdi:{state:'monitoring',discovery:{state:'suspected',reason:'persistent_mac_moves_with_corroboration',port:'GigabitEthernet1/0/3',peer:'GigabitEthernet1/0/4',vlan:10,rounds:2}},analysis:{model:'tiny-log-v3-protocol'},mqtt:{enabled:false,state:'disabled'}};
 else if(url.pathname==='/model/explain')reply=fixture.predictions[url.searchParams.get('log')]||(storm?{experimental:true,accepted:true,truncated:false,label:'loop',score:.99,threshold:.9,model:'tiny-log-v3-protocol'}:{experimental:true,accepted:false});
 else{res.statusCode=404;reply={error:'unexpected request'};}
 res.end(JSON.stringify(reply));
});await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const live=Boolean(process.env.ESP_VIEWER_URL),suffix=live?'hardware':'browser';
const base=process.env.ESP_VIEWER_URL||'http://127.0.0.1:'+server.address().port;
const sleep=ms=>new Promise(r=>setTimeout(r,ms));
const chrome=spawn('/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',['--headless','--disable-gpu','--no-first-run','--no-default-browser-check','--remote-debugging-port=9347','--user-data-dir=/tmp/esp-archive-browser-check','about:blank'],{stdio:'ignore'});
let ws;const errors=[];
try{
 let tabs;for(let i=0;i<100;i++){try{tabs=await(await fetch('http://127.0.0.1:9347/json/list')).json();if(tabs.some(t=>t.type==='page'))break;}catch{}await sleep(100);}
 ws=new WebSocket(tabs.find(t=>t.type==='page').webSocketDebuggerUrl);await new Promise((r,j)=>{ws.onopen=r;ws.onerror=j;});
 let id=0;const tasks=new Map();ws.onmessage=event=>{const message=JSON.parse(event.data);if(message.method==='Runtime.exceptionThrown')errors.push(message.params.exceptionDetails);if(message.id){const task=tasks.get(message.id);tasks.delete(message.id);message.error?task.reject(Error(message.error.message)):task.resolve(message.result);}};
 const call=(method,params={})=>new Promise((resolve,reject)=>{tasks.set(++id,{resolve,reject});ws.send(JSON.stringify({id,method,params}));});
 const evaluate=async expression=>{const r=await call('Runtime.evaluate',{expression,returnByValue:true,awaitPromise:true});if(r.exceptionDetails)throw Error(r.exceptionDetails.text);return r.result.value;};
 await call('Runtime.enable');await call('Page.enable');await call('Emulation.setDeviceMetricsOverride',{width:390,height:844,deviceScaleFactor:1,mobile:true});
 await call('Page.navigate',{url:base});
 for(let i=0;i<200;i++){if(await evaluate('Boolean(document.getElementById("analyze")&&!document.getElementById("analyze").disabled&&document.getElementById("status").textContent.includes("历史记录"))'))break;await sleep(100);}
 assert.equal(await evaluate('document.getElementById("analyze").textContent'),'一键分析最近日志');
 if(!live)assert.match(await evaluate('document.getElementById("device-discovery").textContent'),/疑似环路.*GigabitEthernet1\/0\/3.*GigabitEthernet1\/0\/4.*仅观察/);
 await evaluate('document.getElementById("analyze").click()');
 let summary;for(let i=0;i<600;i++){summary=await evaluate(`(()=>({status:document.getElementById('status')?.textContent||'',cards:document.querySelectorAll('.incident-card').length,risks:[...document.querySelectorAll('.loop-risk-badge')].map(n=>n.textContent),models:[...document.querySelectorAll('.model-result')].map(n=>n.textContent)}))()`);if(summary.status.includes('AI 分析完成')&&(live||storm||summary.cards)&&(storm||summary.models.length)&&summary.models.every(t=>!t.startsWith('正在辅助分类')))break;await sleep(100);}
 assert.match(summary.status,/AI 分析完成/);if(!live){assert.match(summary.status,/2 包/);if(!storm)assert.ok(summary.cards>0,JSON.stringify(summary));}assert.ok(summary.models.every(t=>!t.startsWith('正在辅助分类')));if(!storm)assert.ok(summary.models.some(t=>t.includes('tiny-log-v3-protocol')),JSON.stringify(summary));
 if(storm){assert.deepEqual(summary.risks,['疑似环路风暴']);assert.match(await evaluate('document.querySelector(".loop-risk").textContent'),/VLAN 10.*GigabitEthernet1\/0\/2.*GigabitEthernet1\/0\/3/);assert.match(await evaluate('document.querySelector(".loop-risk").textContent'),/不代表当前仍有风暴/);}
 assert.equal(await evaluate('document.getElementById("history").open'),false);assert.equal(await evaluate(`Boolean(document.querySelector('a[href="/journal"]'))`),false);
 assert.equal(await evaluate('document.documentElement.scrollWidth<=innerWidth'),true,'mobile overflow');assert.equal(errors.length,0,JSON.stringify(errors));
 await evaluate(storm?'document.querySelector(".loop-risk").scrollIntoView({block:"start"})':'scrollTo(0,0)');const screenshot=await call('Page.captureScreenshot',{format:'png'});await writeFile(new URL(live?'one-click-ai-hardware.png':storm?'loop-storm-mobile.png':'fault-viewer-mobile.png',runtime),Buffer.from(screenshot.data,'base64'));
 if(!live)for(const state of ['empty','no_card']){mode=state;await call('Page.navigate',{url:base});let text='';for(let i=0;i<50;i++){text=await evaluate('document.getElementById("status")?.textContent||""');if(state==='empty'?text.includes('暂无可分析记录'):text.includes('FAT32'))break;await sleep(100);}assert.match(text,state==='empty'?/暂无可分析记录/:/FAT32/);assert.equal(await evaluate('document.getElementById("refresh").disabled'),false);}
 const evidence={fixture_source:live?'actual ESP page, SD files and onboard model HTTP':storm?'actual H3C r5 evidence replay; mocked read-only SD/model HTTP for UI test':'stored database report; mocked read-only SD/model HTTP for UI test',embedded_payload:true,one_click:true,mobile_width:390,...summary,page_errors:errors.length,empty_and_no_card:!live,passed:true};await writeFile(new URL(live?'one-click-ai-hardware.json':storm?'loop-storm-browser.json':'fault-viewer-browser.json',runtime),JSON.stringify(evidence,null,2));console.log(JSON.stringify(evidence));
}finally{ws?.close();chrome.kill('SIGTERM');server.closeAllConnections();server.close();}
