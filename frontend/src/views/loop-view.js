import {LOOP_PHASES} from "../loops.js";
import {escapeHtml} from "./html.js";

export function renderLoopView(loopEl, {agentId, device, online, demo, incidents, selected, previousSignature}) {
 loopEl.loopTitle.textContent=`${device?.name||agentId} · 环路处置`;
 loopEl.loopAgent.textContent=agentId;
 loopEl.loopConnection.textContent=device?(online?'ESP 在线 · 持续观察上报':'ESP 离线 / 上报过期 · 当前为历史记录'):'设备已移除';
 loopEl.loopDemoNotice.hidden=!demo;
 loopEl.loopDemoButton.textContent=demo?'返回真实数据':'查看流程演示';
 if(!incidents.some(item=>item.id===selected))selected=incidents[0]?.id;
 const signature=JSON.stringify([selected,incidents]);
 if(signature===previousSignature)return {selected, signature};
 const phaseLabel=phase=>({blocked:'已阻止',failed:'执行失败',...Object.fromEntries(LOOP_PHASES)}[phase]||phase);
 loopEl.loopIncidentList.innerHTML=incidents.map(item=>`<button type="button" class="loop-incident ${item.id===selected?'is-active':''}" data-loop-id="${escapeHtml(item.id)}"><strong>${escapeHtml(item.source)}</strong><span class="mono">${escapeHtml(item.port)}</span><small>${escapeHtml(phaseLabel(item.phase))}</small></button>`).join('');
 const item=incidents.find(value=>value.id===selected);
 if(!item){loopEl.loopBody.innerHTML='<div class="loop-empty"><span class="radar" aria-hidden="true"></span><h3>尚未收到明确的环路事件</h3><p>等待包含交换机、端口与环路标识的上报。可点击“查看流程演示”预览处置过程。</p></div>';return {selected, signature};}
 const current=LOOP_PHASES.findIndex(([key])=>key===item.phase);
 loopEl.loopBody.innerHTML=`<div class="loop-target"><div><small>故障交换机 / 端口</small><h3>${escapeHtml(item.source)}</h3><p class="mono">${escapeHtml(item.port)}</p></div><span class="badge badge-${item.phase==='resolved'?'healthy':item.phase==='failed'?'critical':'warning'}">${escapeHtml(phaseLabel(item.phase))}</span></div>
 <p class="form-note">事件 ${escapeHtml(item.incident)} · 最后证据 ${new Date(item.lastAt).toLocaleString('zh-CN')}${item.uncertain?' · 缺少原始检测日志，过程可能不完整':''}</p>
 <ol class="loop-steps">${LOOP_PHASES.map(([key,label],index)=>{const observed=item.timeline.some(event=>event.phase===key);return `<li class="${key===item.phase?'is-current':observed?'is-done':''}"><span>${observed?'●':index+1}</span><strong>${label}</strong><small>${observed?'已收到证据':index>current?'等待证据':'未收到此阶段证据'}</small></li>`;}).join('')}</ol>
 <h3>处置时间线</h3><ol class="loop-timeline">${item.timeline.map(event=>`<li><time>${new Date(event.at).toLocaleTimeString('zh-CN',{hour12:false})}</time><div><strong>${escapeHtml(event.label)}</strong>${event.request?`<p class="mono">请求 ${escapeHtml(event.request)}</p>`:''}<details><summary>查看原始证据</summary><pre>${escapeHtml(event.text)}</pre></details></div></li>`).join('')}</ol>`;
 return {selected, signature};
}
