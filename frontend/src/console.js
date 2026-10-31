import mqtt from "mqtt";
import { parseFields, agentTopics } from "./telemetry.js";
import { loadRuntimeConfig } from "./runtime-config.js";
import {parseReport, ReportHistory, reportSummary, reportSeverity} from "./reports.js";
import {loadArchiveSnapshot} from "./registry.js";

const $ = (id) => document.getElementById(id);
const observed = {esp: 0};
let follow = true;
let client = null;
let agentTopic = "";
let lastOfflineAt = 0;
let runtime = null;
let agentId = "";
let latestReportAt = 0;
let archiveSyncing = false;
let restoredRows = 0;
const reportHistory = new ReportHistory();
function connection(label, good = false) {
  $("connection").textContent = label;
  $("connection").style.color = good ? "#9de1ba" : "#ffcf75";
}
async function boot() {
try {
runtime = await loadRuntimeConfig();
const selected = new URLSearchParams(location.search).get("agent");
const id = runtime.agentIds.includes(selected) ? selected : runtime.agentIds[0];
agentId = id;
agentTopic = `${runtime.topicRoot}/${id}`;
$("espName").textContent = runtime.agentName;
$("serialLabel").textContent = runtime.serialLabel;
client = mqtt.connect(runtime.mqttUrl, {
  username: runtime.username, password: runtime.password, clean: true,
  clientId: `esp-live-console-${Math.random().toString(16).slice(2)}`,
  reconnectPeriod: 2000, connectTimeout: 5000
});
client.on("connect", () => {
  connection("MQTT 已连接", true);
  client.subscribe(agentTopics(runtime.topicRoot, [id]), {qos: 1});
});
client.on("reconnect", () => connection("MQTT 重连中"));
client.on("close", () => connection("MQTT 已断开"));
client.on("error", (error) => connection(`MQTT 错误：${error.message}`));
client.on("message", handleMessage);
if (runtime.api) {
  const sync = () => restoreArchive().catch(() => {
    $("archiveState").textContent = "历史数据库暂不可用，将自动重试";
  });
  sync();
  setInterval(sync, 5_000);
} else $("archiveState").textContent = "未配置历史数据库，仅显示本次会话";
} catch (error) { connection(error.message); }
}

async function restoreArchive() {
  if (archiveSyncing) return;
  archiveSyncing = true;
  try {
    const {registry, history} = await loadArchiveSnapshot(runtime.api, agentId);
    const device = registry.devices.find(row => row.id === agentId);
    observed.esp = 0;
    latestReportAt = 0;
    lastOfflineAt = 0;
    $("espStats").textContent = "数据库暂无 ESP32 运行状态";
    $("observations").textContent = "数据库暂无监测结果";
    $("espName").textContent = device?.name || runtime.agentName;
    if (device) {
      $("espName").textContent = device.name;
      if (device.availability === "OFFLINE") lastOfflineAt = Math.max(lastOfflineAt, device.availability_at_ms);
      if (device.latest_report) showReport(parseReport(JSON.stringify(device.latest_report)));
    }
    const logs = $("espLogs");
    const scrollTop = logs.scrollTop;
    const retained = new Set(history.map(item => item.report.report_id));
    for (const line of [...logs.children]) {
      if (!retained.has(line.dataset.reportId)) line.remove();
    }
    const existing = new Set([...logs.children].map(line => line.dataset.reportId));
    for (const item of history) {
      if (!existing.has(item.report.report_id)) {
        append(item.report, true);
      }
    }
    restoredRows = history.length;
    logs.scrollTop = follow ? logs.scrollHeight : scrollTop;
    refresh();
    $("archiveState").textContent = `当前连接已入库 ${(device?.report_count ?? 0).toLocaleString("zh-CN")} 包 · 显示最近 ${restoredRows} 条实时记录`;
  } finally { archiveSyncing = false; }
}

function append(report, archived = false) {
  const line = document.createElement("details");
  line.className = "line";
  const severity = reportSeverity(report);
  line.classList.add(severity === "critical" ? "fault" : severity === "warning" ? "warn" : "ok");
  const summary = document.createElement("summary");
  const time = document.createElement("span");
  time.className = "time";
  time.textContent = new Date(report.sent_at_ms).toLocaleTimeString("zh-CN", {hour12: false});
  time.title = archived ? "数据库历史上报时间" : "ESP32 上报时间";
  summary.append(time, document.createTextNode(reportSummary(report)));
  const raw = document.createElement("pre");
  raw.textContent = report.lines.map(line => `${new Date(line.at_ms).toLocaleTimeString("zh-CN", {hour12: false})} ${line.text}`).join("\n");
  line.append(summary, raw);
  line.dataset.sentAtMs = String(report.sent_at_ms);
  line.dataset.reportId = report.report_id;
  const logs = $("espLogs");
  const next = [...logs.children].find(child => Number(child.dataset.sentAtMs) > report.sent_at_ms);
  logs.insertBefore(line, next || null);
  while (logs.childElementCount > 600) logs.firstElementChild.remove();
  if (follow) logs.scrollTop = logs.scrollHeight;
  return line;
}

function handleMessage(topic, raw, packet = {}) {
  if (runtime?.api) return;
  const payload = raw.toString();
  if (topic === `${agentTopic}/availability`) {
    if (parseFields(payload).state === "OFFLINE") { observed.esp = 0; lastOfflineAt = Date.now(); }
    refresh();
    return;
  }
  if (topic !== `${agentTopic}/report`) return;
  let report;
  try { report = parseReport(payload); } catch { return; }
  showReport(report);
  if (!packet.retain && reportHistory.accept(report.report_id)) append(report);
}

function showReport(report) {
  if (report.sent_at_ms < latestReportAt) return;
  latestReportAt = report.sent_at_ms;
  const fields = report.status;
  observed.esp = fields?.seen_at_ms && Number(fields.seen_at_ms) > lastOfflineAt ? Math.min(Date.now(), Number(fields.seen_at_ms)) : 0;
  $("espStats").textContent = fields ? `推理 ${fields.reasoning_cycle} · 运行 ${Math.floor(Number(fields.uptime_ms) / 1000)}s · 可用堆 ${fields.free_heap} B · 设备 ${fields.devices} · 拒绝 ${fields.rejected}` : "等待 ESP32 运行状态…";
  $("observations").textContent = reportSummary({...report, lines: []});
  refresh();
}
function refresh() {
  for (const side of ["esp"]) {
    const age = observed[side] ? Math.floor((Date.now() - observed[side]) / 1000) : null;
    const online = client?.connected && age !== null && age < 15;
    $(`${side}State`).textContent = online ? "在线 · 日志接收中" : "等待实时数据";
    $(`${side}State`).style.color = online ? "#9de1ba" : "#ffcf75";
    $(`${side}Last`).textContent = age === null ? "尚未收到实时日志" : `最近日志 ${age} 秒前`;
  }
}
$("pause").addEventListener("click", () => {
  follow = !follow;
  $("pause").textContent = follow ? "暂停滚动" : "继续滚动";
});
setInterval(refresh, 1000);
boot();
