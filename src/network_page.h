// Generated from frontend/network.html by tools/embed_network_page.py
#pragma once
static const char NETWORK_PAGE[] PROGMEM = R"ESP_PAGE(<!doctype html><html lang="zh-CN"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>设备网口配置</title>
<style>[hidden]{display:none!important}body{font:16px system-ui;background:#f2f5f8;color:#172b40;margin:0;padding:24px}main{max-width:620px;margin:30px auto;background:white;padding:28px;border-radius:18px}h1{font-size:25px}label{display:block;margin:18px 0}input,button,select{box-sizing:border-box;width:100%;padding:12px;font:inherit;border:1px solid #ccd5df;border-radius:8px}button{background:#176ac6;color:white;cursor:pointer;margin:8px 0}input[type=checkbox]{width:auto}button:disabled{opacity:.5}p{line-height:1.6}#status{white-space:pre-wrap}small{color:#526578}</style>
<main><h1>设备网口配置</h1><p id="intro">连接设备，填写网口参数，保存后自动生效。</p><button id="connect">蓝牙连接设备</button><form id="form"><label>设备 IP<input id="ip" required placeholder="192.168.4.1"></label><label>子网掩码<input id="mask" required placeholder="255.255.255.0"></label><label>网关<input id="gateway" required placeholder="不用可填 0.0.0.0"></label><label>DNS<input id="dns" required placeholder="不用可填 0.0.0.0"></label><section id="switch-section">
<h2>交换机管理连接 · SSH / Telnet</h2><p id="switch-state" role="status">此栏用于配置交换机 SSH / Telnet。请连接 ESP 配置热点或通过网口打开设备 IP；电脑上的蓝牙入口目前仅保存网口 IP。</p><p>网线接入后，填写交换机的管理地址；此地址与 ESP 网口 IP 不同。连接参数与网口参数一起保存。</p>
<p role="note">当前固件仅保存连接配置，尚未接入 ESP 直连 SSH / Telnet 客户端。保存不会登录交换机或自动处理环路。</p>
<label>连接协议<select id="sw_protocol" disabled><option value="none">暂不配置</option><option value="ssh">SSH（加密，推荐）</option><option value="telnet">Telnet（明文）</option></select></label>
<fieldset id="switch-fields" disabled><legend>交换机连接参数</legend>
<label>交换机管理 IPv4<input id="sw_host" maxlength="15" placeholder="例如 192.168.23.2"></label>
<label>管理端口<input id="sw_port" type="number" min="1" max="65535" value="22" required></label>
<label>用户名<input id="sw_username" maxlength="64" autocomplete="off"></label>
<label>登录密码<input id="sw_password" type="password" maxlength="128" autocomplete="new-password" placeholder="留空保留原密码"></label><label><input id="sw_clear_password" type="checkbox"> 清空已保存的登录密码</label>
<label>特权密码（可选）<input id="sw_enable_password" type="password" maxlength="128" autocomplete="new-password" placeholder="留空保留原密码"></label><label><input id="sw_clear_enable_password" type="checkbox"> 清空已保存的特权密码</label>
<div id="ssh-fields"><label><input id="sw_verify_fingerprint" type="checkbox"> 验证 SSH 主机指纹（可选）</label><label>SSH 主机指纹<input id="sw_fingerprint" maxlength="50" placeholder="选择验证时填写 SHA256:…" autocomplete="off" disabled></label><small>不勾选即跳过指纹验证，无法确认交换机身份。选择验证时，请从管理员或可信控制台取得指纹。</small>
<label>SSH 会话<select id="sw_transport"><option value="shell">交互终端（交换机常用）</option><option value="exec">单命令执行</option></select></label></div>
<label>连接超时（秒）<input id="sw_connect_timeout" type="number" min="1" max="30" value="5" required></label>
<label>命令超时（秒）<input id="sw_command_timeout" type="number" min="1" max="30" value="5" required></label>
<label id="telnet-warning" hidden><input id="sw_telnet_ack" type="checkbox"> 我了解 Telnet 会明文传输账号、密码和命令，仅用于可信的隔离管理网。</label>
</fieldset><small>密码保存在设备 NVS 中，不通过读取接口回显。当前配置热点与 HTTP 未加密，请在可信现场配置。尚不支持密钥认证、SNMP 或串口管理，页面不提供这些选项。</small>
</section><section id="mqtt-section"><h2>网口 MQTT 实验</h2><p id="mqtt-build">连接设备热点或网口后读取参数，与其他配置一起保存。</p><fieldset id="mqtt-fields" disabled><legend>MQTT 实验参数</legend><label><input type="checkbox" id="mqtt-enabled"> 启用实验连接（默认关闭）</label><label>MQTT 服务器 IPv4<input id="mqtt-host" placeholder="例如 192.168.23.10" maxlength="15"></label><label>端口<input id="mqtt-port" type="number" min="1" max="65535" value="1883" required></label><label>用户名<input id="mqtt-username" maxlength="64" autocomplete="off"></label><label>密码<input id="mqtt-password" type="password" maxlength="128" autocomplete="new-password" placeholder="留空保留原密码"></label><label><input type="checkbox" id="mqtt-clear"> 清空已保存的密码</label><label>实验主题前缀<input id="mqtt-prefix" value="esp-lab" maxlength="48" required></label><p>随全部配置一次保存，无需验证码、二次确认或先测试连接。仅局域网 TCP 实验；不支持 TLS。使用独立实验主题，不会接入正式上报历史。</p></fieldset></section><section><h2>TF 卡故障记录</h2><p id="storage-state" role="status">插入 TF 卡后，设备可记录故障摘要、原始日志和处置过程。连接设备配置页可查看卡状态。</p><p><a href="/journal">查看 TF 卡记录</a>（无需保存配置）</p><small>双击 BOOT 可单独打开只读查看入口。记录位于 esp-journal 文件夹，按容量循环保留。未插卡或卡异常不会中断监测；记录可能丢失，状态会显示丢弃数量。仅记录已观察到的证据，不推断断电或环路根因。请勿在写入时拔卡。</small></section><button id="save" disabled>保存全部配置</button></form><p id="status" role="status">尚未连接</p><small>蓝灯慢闪：等待连接；蓝灯快闪：手机已接入；绿灯亮 3 秒：保存成功，随后自动关闭配置；红灯快闪：配置失败。配置断电保留。热点不提供互联网，手机提示时请选择继续连接。</small></main>
<script>
const serviceId='68b10001-7d85-4f37-b71b-58d52ce12000', charId='68b10002-7d85-4f37-b71b-58d52ce12000';
const fields=['ip','mask','gateway','dns'], status=document.getElementById('status'), save=document.getElementById('save'), connect=document.getElementById('connect');
let characteristic, device, wired=false, switchLoaded=false, mqttLoaded=false, saving=false;
const switchKeys=['protocol','host','port','username','password','enable_password','fingerprint','transport','connect_timeout','command_timeout'];
const sw=id=>document.getElementById('sw_'+id);
function updateSwitch(defaultPort=false){const protocol=sw('protocol').value;document.getElementById('switch-fields').disabled=protocol==='none';document.getElementById('ssh-fields').hidden=protocol!=='ssh';document.getElementById('telnet-warning').hidden=protocol!=='telnet';sw('telnet_ack').required=protocol==='telnet';sw('host').required=sw('username').required=protocol!=='none';sw('fingerprint').required=protocol==='ssh'&&sw('verify_fingerprint').checked;sw('fingerprint').disabled=!sw('fingerprint').required;if(defaultPort)sw('port').value=protocol==='telnet'?'23':'22';}
sw('protocol').onchange=()=>updateSwitch(true);
sw('verify_fingerprint').onchange=()=>updateSwitch();
async function loadSwitch(){
 const state=document.getElementById('switch-state');switchLoaded=false;sw('protocol').disabled=true;document.getElementById('switch-fields').disabled=true;
 state.textContent='正在读取交换机连接配置…';
 try{const r=await fetch('/switch',{cache:'no-store'});if(!r.ok)throw Error('交换机配置读取失败：设备可能仍是旧固件，或配置窗口已关闭。请更新固件并长按 BOOT 三秒重新进入。');
 const cfg=new URLSearchParams(await r.text());if(!['none','ssh','telnet'].includes(cfg.get('protocol')))throw Error('当前设备未提供 SSH / Telnet 配置接口，请更新固件。');
 for(const k of switchKeys.filter(k=>!k.includes('password')))sw(k).value=cfg.get(k)||'';
 for(const k of ['password','enable_password'])sw(k).placeholder=cfg.get('has_'+k)==='1'?'已保存，留空保留':'尚未设置';
 sw('verify_fingerprint').checked=cfg.get('verify_fingerprint')==='1'||(cfg.get('verify_fingerprint')===null&&Boolean(cfg.get('fingerprint')));
 switchLoaded=true;sw('protocol').disabled=false;state.textContent='已读取配置。选择 SSH 或 Telnet 后填写下方参数，与网口参数一起保存。';updateSwitch();
 }catch(error){state.textContent=error.message;throw error;}
}
function setupBody(values){if(!switchLoaded||!mqttLoaded)throw Error('请等待全部参数读取完成');const body=new URLSearchParams(Object.fromEntries(fields.map((k,i)=>[k,values[i]])));for(const k of switchKeys)body.set('sw_'+k,sw(k).value.trim());for(const k of ['password','enable_password'])body.set('sw_'+k,sw(k).value);for(const k of ['clear_password','clear_enable_password','telnet_ack'])body.set('sw_'+k,sw(k).checked?'1':'0');const verify=sw('protocol').value==='ssh'&&sw('verify_fingerprint').checked;body.set('sw_verify_fingerprint',verify?'1':'0');if(!verify)body.set('sw_fingerprint','');body.set('settings_version','2');for(const key of ['host','port','username','prefix'])body.set('mqtt_'+key,document.getElementById('mqtt-'+key).value.trim());body.set('mqtt_password',document.getElementById('mqtt-password').value);body.set('mqtt_enabled',document.getElementById('mqtt-enabled').checked?'1':'0');body.set('mqtt_clear_password',document.getElementById('mqtt-clear').checked?'1':'0');return body;}
const fill=s=>{const v=s.split('|');if(v.length!==4)throw Error(s);fields.forEach((k,i)=>document.getElementById(k).value=v[i]);};
const show=e=>{status.textContent=e.message||String(e);};
async function readBLE(){return new TextDecoder().decode(await characteristic.readValue());}
connect.onclick=async()=>{try{if(!navigator.bluetooth)throw Error('请使用支持 Web Bluetooth 的 Chrome / Edge，通过 localhost 或 HTTPS 打开本页。');device=await navigator.bluetooth.requestDevice({filters:[{services:[serviceId]}]});device.addEventListener('gattserverdisconnected',()=>{save.disabled=true;show('蓝牙已断开。保存成功后配置入口会自动关闭；重新配置请按住 BOOT 3 秒。');});const server=await device.gatt.connect();characteristic=await(await server.getPrimaryService(serviceId)).getCharacteristic(charId);fill(await readBLE());wired=false;save.dataset.saved='';save.disabled=false;save.textContent='通过蓝牙保存网口 IP';show('已连接 '+device.name+'。蓝牙仅保存网口 IP；SSH / Telnet 请在设备热点或网口页面配置。');}catch(e){show(e);}};
document.getElementById('form').onsubmit=async e=>{e.preventDefault();if(saving)return;saving=true;save.disabled=true;try{const values=fields.map(k=>document.getElementById(k).value.trim());for(const value of values)if(!/^(\d{1,3}\.){3}\d{1,3}$/.test(value)||value.split('.').some(v=>Number(v)>255))throw Error('请填写有效 IPv4 地址');if(wired){const r=await fetch('/network',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:setupBody(values)});const message=await r.text();if(!r.ok)throw Error(message);show(message+'\n网口地址：http://'+values[0]+'。重新配置请按住 BOOT 3 秒。');save.dataset.saved='1';}else{if(!characteristic)throw Error('请先连接设备');const data=new TextEncoder().encode(values.join('|')+'\n');for(let i=0;i<data.length;i+=20)await characteristic.writeValueWithResponse(data.slice(i,i+20));let reply='';for(let i=0;i<20;i++){await new Promise(r=>setTimeout(r,100));reply=await readBLE();if(reply!=='SAVING')break;}if(reply!==values.join('|'))throw Error(reply==='SAVING'?'保存尚未完成，请重新连接核实':reply);show('已保存配置，网口 IP：'+values[0]+'。绿灯亮 3 秒后配置入口自动关闭，重新配置请按住 BOOT 3 秒。');save.dataset.saved='1';}}catch(e){show(e);}finally{saving=false;save.disabled=save.dataset.saved==='1'||(wired?(!switchLoaded||!mqttLoaded):!characteristic);}};
async function loadMqtt(){
 mqttLoaded=false;const note=document.getElementById('mqtt-build');document.getElementById('mqtt-fields').disabled=true;
 try{const response=await fetch('/mqtt',{cache:'no-store'});if(!response.ok)throw Error('MQTT 配置读取失败，请重新开启配置窗口');
 const cfg=new URLSearchParams(await response.text());if(cfg.get('setup_version')!=='2')throw Error('设备固件尚不支持统一保存，请更新固件后重新打开配置页。');
 for(const key of ['host','port','username','prefix'])document.getElementById('mqtt-'+key).value=cfg.get(key)||'';
 document.getElementById('mqtt-enabled').checked=cfg.get('enabled')==='1';
 document.getElementById('mqtt-password').placeholder=cfg.get('has_password')==='1'?'已保存密码，留空保留':'尚未设置密码';
 note.textContent=cfg.get('compiled')==='1'?'当前为实验固件，勾选启用后才会连接。':'当前为普通固件，仅保存参数，不会连接 MQTT。';
 mqttLoaded=true;document.getElementById('mqtt-fields').disabled=false;
 }catch(error){note.textContent=error.message;throw error;}
}
async function loadStorage(){
 const note=document.getElementById('storage-state');
 try{const response=await fetch('/storage',{cache:'no-store'});if(!response.ok)throw Error('TF 卡状态暂不可读，其他参数仍可保存。');
 const cfg=new URLSearchParams(await response.text());const labels={ready:'正常记录',no_card:'未检测到可用卡，请插入 FAT32 格式的 TF 卡',write_error:'写入失败，请检查卡空间或文件系统',unavailable:'记录任务不可用',starting:'正在初始化'};
 note.textContent=(labels[cfg.get('state')]||'状态未知')+'；本次开机已写入 '+(cfg.get('written')||'0')+' 条，未保存 '+(cfg.get('dropped')||'0')+' 条。'+(cfg.get('summary_incomplete')==='1'?'摘要跟踪容量已满，记录可能不完整。':'');
 }catch(error){note.textContent=error.message;}
}
(async()=>{if(location.pathname==='/' || location.pathname==='/portal'){wired=true;connect.hidden=true;try{const r=await fetch('/network');if(!r.ok)throw Error(await r.text());fill(await r.text());await loadSwitch();await loadMqtt();save.disabled=false;document.getElementById('intro').textContent='已连接设备，请填写网口、交换机和 MQTT 参数，最后统一保存。保存成功后绿灯亮 3 秒，随后关闭热点及配置入口。重新配置请按住 BOOT 3 秒。';show('全部参数已读取。修改后点击底部“保存全部配置”。');await loadStorage();}catch(e){show(e);}}})();
</script></html>
)ESP_PAGE";
