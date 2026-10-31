import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const page=fs.readFileSync(new URL('../network.html',import.meta.url),'utf8');
function setup(){
  const nodes=new Map([...page.matchAll(/id="([^"]+)"/g)].map(([,id])=>[id,{value:'',checked:false,disabled:false,hidden:false,dataset:{}}]));
  const context=vm.createContext({document:{getElementById:id=>nodes.get(id)},location:{pathname:'/network.html'},navigator:{},URLSearchParams,TextEncoder,TextDecoder});
  vm.runInContext(page.match(/<script>([\s\S]*?)<\/script>/)[1],context);
  return {nodes,context,run:code=>vm.runInContext(code,context)};
}
test('SSH fingerprint verification is optional; Telnet still requires confirmation',()=>{
  const {nodes,run}=setup();
  nodes.get('sw_protocol').value='ssh';run('updateSwitch(true)');
  assert.equal(nodes.get('sw_port').value,'22');assert.equal(nodes.get('sw_fingerprint').required,false);
  assert.equal(nodes.get('sw_fingerprint').disabled,true);
  nodes.get('sw_verify_fingerprint').checked=true;run('updateSwitch()');
  assert.equal(nodes.get('sw_fingerprint').required,true);assert.equal(nodes.get('sw_fingerprint').disabled,false);
  assert.equal(nodes.get('telnet-warning').hidden,true);
  nodes.get('sw_protocol').value='telnet';run('updateSwitch(true)');
  assert.equal(nodes.get('sw_port').value,'23');assert.equal(nodes.get('sw_fingerprint').required,false);
  assert.equal(nodes.get('sw_telnet_ack').required,true);
  nodes.get('sw_protocol').value='none';run('updateSwitch()');assert.equal(nodes.get('switch-fields').disabled,true);
});
test('skipping fingerprint verification clears the saved pin while selecting verification includes it',()=>{
  const {nodes,run}=setup();run('switchLoaded=true;mqttLoaded=true');
  nodes.get('sw_protocol').value='ssh';nodes.get('sw_fingerprint').value='SHA256:'+ 'A'.repeat(43);
  let body=run("setupBody(['a','b','c','d'])");assert.equal(body.get('sw_verify_fingerprint'),'0');assert.equal(body.get('sw_fingerprint'),'');
  nodes.get('sw_verify_fingerprint').checked=true;
  body=run("setupBody(['a','b','c','d'])");assert.equal(body.get('sw_verify_fingerprint'),'1');assert.equal(body.get('sw_fingerprint'),nodes.get('sw_fingerprint').value);
});
test('unified payload preserves exact password, encodes special characters and clear flags',()=>{
  const {nodes,run}=setup();run('switchLoaded=true;mqttLoaded=true');
  nodes.get('sw_password').value='  a&b=+|  ';nodes.get('sw_enable_password').value='enable secret';
  nodes.get('sw_clear_password').checked=true;nodes.get('sw_telnet_ack').checked=true;
  nodes.get('mqtt-host').value='192.168.23.10';nodes.get('mqtt-password').value='  mqtt|&=+  ';
  nodes.get('mqtt-clear').checked=true;nodes.get('mqtt-enabled').checked=true;
  const body=run("setupBody(['192.168.23.6','255.255.255.0','192.168.23.1','192.168.23.1'])");
  assert.equal(body.get('sw_password'),'  a&b=+|  ');assert.equal(body.get('sw_enable_password'),'enable secret');
  assert.equal(body.get('sw_clear_password'),'1');assert.equal(body.get('sw_telnet_ack'),'1');
  assert.equal(body.get('ip'),'192.168.23.6');assert.equal(new URLSearchParams(body.toString()).get('sw_password'),'  a&b=+|  ');
  assert.equal(body.get('settings_version'),'2');assert.equal(body.get('mqtt_host'),'192.168.23.10');
  assert.equal(body.get('mqtt_password'),'  mqtt|&=+  ');assert.equal(body.get('mqtt_clear_password'),'1');assert.equal(body.get('mqtt_enabled'),'1');
});
test('one form and one save button submits every setting in a single request',async()=>{
  assert.equal([...page.matchAll(/<form\b/g)].length,1);
  assert.equal([...page.matchAll(/<button\b[^>]*>保存/g)].length,1);
  const {context,nodes,run}=setup();run('wired=true;switchLoaded=true;mqttLoaded=true');
  for(const [id,value] of Object.entries({ip:'192.168.23.6',mask:'255.255.255.0',gateway:'192.168.23.1',dns:'192.168.23.1'}))nodes.get(id).value=value;
  const calls=[];context.fetch=async(url,opts)=>{calls.push({url,opts});return {ok:true,text:async()=>'全部配置已保存'};};
  await nodes.get('form').onsubmit({preventDefault(){}});
  assert.equal(calls.length,1);assert.equal(calls[0].url,'/network');
  assert.equal(calls[0].opts.body.get('settings_version'),'2');assert.equal(nodes.get('save').disabled,true);
});
test('validation failure keeps page available for correction without a second write',async()=>{
  const {context,nodes,run}=setup();run('wired=true;switchLoaded=true;mqttLoaded=true');
  for(const [id,value] of Object.entries({ip:'192.168.23.6',mask:'255.255.255.0',gateway:'192.168.23.1',dns:'192.168.23.1'}))nodes.get(id).value=value;
  let count=0;context.fetch=async()=>{count++;return {ok:false,text:async()=>'MQTT port invalid'};};
  await nodes.get('form').onsubmit({preventDefault(){}});
  assert.equal(count,1);assert.equal(nodes.get('save').disabled,false);assert.match(nodes.get('status').textContent,/MQTT port invalid/);
});
test('MQTT loading must complete and firmware must support unified saving',async()=>{
  const {context,nodes,run}=setup();run('switchLoaded=true');
  assert.throws(()=>run("setupBody(['a','b','c','d'])"),/全部参数/);
  context.fetch=async()=>({ok:true,text:async()=>new URLSearchParams({enabled:'0',port:'1883',prefix:'esp-lab'}).toString()});
  await assert.rejects(run('loadMqtt()'),/更新固件/);assert.equal(nodes.get('mqtt-fields').disabled,true);
  context.fetch=async()=>({ok:true,text:async()=>new URLSearchParams({setup_version:'2',enabled:'0',compiled:'0',port:'1883',prefix:'esp-lab',has_password:'1'}).toString()});
  await run('loadMqtt()');assert.equal(nodes.get('mqtt-fields').disabled,false);assert.equal(nodes.get('mqtt-password').value,'');
  assert.match(nodes.get('mqtt-build').textContent,/不会连接/);
  assert.equal(run("setupBody(['a','b','c','d'])").get('mqtt_password'),'');
});
test('TF card state is advisory and a missing card endpoint does not disable saving',async()=>{
  const {context,nodes,run}=setup();nodes.get('save').disabled=false;
  context.fetch=async()=>({ok:false});await run('loadStorage()');
  assert.equal(nodes.get('save').disabled,false);assert.match(nodes.get('storage-state').textContent,/仍可保存/);
  context.fetch=async()=>({ok:true,text:async()=>new URLSearchParams({state:'no_card',written:'0',dropped:'12'}).toString()});
  await run('loadStorage()');assert.match(nodes.get('storage-state').textContent,/FAT32/);assert.match(nodes.get('storage-state').textContent,/12/);
});
test('failed profile read prevents a destructive empty overwrite',async()=>{
  const {context,nodes,run}=setup();context.fetch=async()=>({ok:false});
  await assert.rejects(run('loadSwitch()'),/读取失败/);
  assert.match(nodes.get('switch-state').textContent,/旧固件/);
  assert.equal(nodes.get('sw_protocol').disabled,true);
  assert.throws(()=>run("setupBody(['a','b','c','d'])"),/等待/);
});
test('SSH section stays visible before connecting or after an unsupported response',async()=>{
  assert.match(page,/<section id="switch-section">/);
  const {context,nodes,run}=setup();context.fetch=async()=>({ok:true,text:async()=>'<html>old page</html>'});
  await assert.rejects(run('loadSwitch()'),/未提供/);
  assert.equal(nodes.get('sw_protocol').disabled,true);
  assert.equal(nodes.get('switch-fields').disabled,true);
  assert.match(nodes.get('switch-state').textContent,/更新固件/);
});
test('profile read displays secret presence without inserting password values',async()=>{
  const {context,nodes,run}=setup();
  context.fetch=async()=>({ok:true,text:async()=>new URLSearchParams({protocol:'ssh',host:'192.168.23.2',port:'22',username:'admin',transport:'shell',connect_timeout:'5',command_timeout:'5',has_password:'1',has_enable_password:'0'}).toString()});
  await run('loadSwitch()');
  assert.equal(nodes.get('sw_host').value,'192.168.23.2');assert.equal(nodes.get('sw_password').value,'');
  assert.match(nodes.get('sw_password').placeholder,/已保存/);assert.equal(nodes.get('switch-section').hidden,false);
  assert.equal(nodes.get('sw_protocol').disabled,false);
});
test('saved fingerprint preference reloads and old pinned profiles retain verification',async()=>{
  const {context,nodes,run}=setup();
  const cfg={protocol:'ssh',port:'22',transport:'shell',connect_timeout:'5',command_timeout:'5',fingerprint:'SHA256:'+'A'.repeat(43)};
  context.fetch=async()=>({ok:true,text:async()=>new URLSearchParams(cfg).toString()});
  await run('loadSwitch()');assert.equal(nodes.get('sw_verify_fingerprint').checked,true);assert.equal(nodes.get('sw_fingerprint').required,true);
  cfg.verify_fingerprint='0';cfg.fingerprint='';await run('loadSwitch()');
  assert.equal(nodes.get('sw_verify_fingerprint').checked,false);assert.equal(nodes.get('sw_fingerprint').required,false);
});
