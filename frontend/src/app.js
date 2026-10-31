import mqtt from "mqtt";
import "./styles.css";
import { parseFields, topicInfo, applyTelemetry, telemetryOnline, agentTopics } from "./telemetry.js";
import { loadRuntimeConfig, connectionFields } from "./runtime-config.js";
import {parseReport, ReportHistory, applyReport, reportSummary, reportSeverity} from "./reports.js";
import {registryRequest, loadArchiveSnapshot, deviceMetadata as metadataFields} from "./registry.js";

const STORAGE_CONNECTION = "esp-diag-connection-v1";
const STORAGE_DEVICES = "esp-diag-devices-v1";
const MAX_EVENTS = 300;

const elements = Object.fromEntries(
  [
    "connectionState", "connectionLabel", "connectionSettingsButton", "connectButton",
    "clock", "clockDate", "onlineMetric", "totalDeviceMetric", "criticalMetric",
    "lockedMetric", "messageMetric", "messageTitle", "messageScope", "deviceTableBody", "emptyDevices", "detailEmpty",
    "detailContent", "detailKind", "detailTitle", "detailId", "detailBadges",
    "emptyDevicesTitle", "emptyDevicesHint", "eventsTitle", "emptyEventsTitle", "emptyEventsHint",
    "detailLocation", "detailLastSeen", "detailTransport", "detailUptime", "detailCpu",
    "detailTemp", "detailMessages", "detailFaults", "beliefList", "detailLastEvent",
    "editDeviceButton", "addDeviceButton", "eventTableBody", "emptyEvents",
    "severityFilter", "eventSearch", "pauseButton", "clearButton", "connectionDialog",
    "connectionForm", "mqttProtocol", "mqttHost", "mqttPort", "mqttPath", "mqttUsername",
    "mqttPassword", "autoConnect", "deviceDialog", "deviceForm", "deviceDialogTitle",
    "deviceIdInput", "deviceNameInput", "deviceLocationInput", "deviceEnabledInput",
    "deleteDeviceButton", "toast"
  ].map((id) => [id, document.getElementById(id)])
);

const defaultConnection = {
  protocol: window.location.protocol === "https:" ? "wss" : "ws",
  host: window.location.hostname || "127.0.0.1",
  port: 9001,
  path: "/mqtt",
  username: "esp32",
  autoConnect: true
};

let connection = { ...defaultConnection, ...readJson(STORAGE_CONNECTION, {}) };
let runtime = null;
let deviceMetadata = readJson(STORAGE_DEVICES, {});
let client = null;
let selectedDeviceId = null;
let editingDeviceId = null;
let paused = false;
let messageCount = null;
let events = [];
const devices = new Map();
const reportHistory = new ReportHistory();

function readJson(key, fallback) {
  try {
    return JSON.parse(localStorage.getItem(key)) ?? fallback;
  } catch {
    return fallback;
  }
}

function writeJson(key, value) {
  localStorage.setItem(key, JSON.stringify(value));
}

function escapeHtml(value) {
  return String(value ?? "")
    .replaceAll("&", "&amp;")
    .replaceAll("<", "&lt;")
    .replaceAll(">", "&gt;")
    .replaceAll('"', "&quot;")
    .replaceAll("'", "&#039;");
}

function createDevice(id, kind = "manual") {
  const meta = deviceMetadata[id] || {};
  return {
    id,
    kind,
    name: meta.name || (runtime?.agentIds[0] === id ? runtime.agentName : defaultDeviceName(id, kind)),
    location: meta.location || "未设置",
    enabled: meta.enabled ?? true,
    manuallyAdded: meta.manuallyAdded ?? false,
    lastSeen: null,
    availability: null,
    health: "unknown",
    locked: false,
    transport: "—",
    uptimeMs: null,
    cpu: null,
    temp: null,
    messages: runtime?.api ? null : 0,
    faults: 0,
    lastEvent: "尚无数据",
    lastTopic: "—",
    beliefs: {
      server_healthy: null,
      server_critical: null,
      server_locked: null,
      switch_link_fault: null
    }
  };
}

function defaultDeviceName(id, kind) {
  if (id === "esp32-s3-eth") return "ESP32-S3 BDI Agent";
  return `ESP32 · ${id}`;
}

function ensureDevice(id, kind) {
  if (!devices.has(id)) devices.set(id, createDevice(id, kind));
  const device = devices.get(id);
  if (device.kind === "manual" && kind !== "manual") device.kind = kind;
  return device;
}

function applyMessage(topic, payload, packet = {}) {
  // With an archive configured, only committed database records update the view.
  if (runtime?.api) return;
  const fields = parseFields(payload);
  const info = topicInfo(topic, fields, runtime?.topicRoot);
  if (info.kind !== "agent" || !runtime?.agentIds.includes(info.id)) return;
  const device = ensureDevice(info.id, info.kind);
  if (info.channel === "availability") {
    applyTelemetry(device, info, fields);
    renderMetrics(); renderDevices(); renderDetail();
    return;
  }
  if (info.channel !== "report") return;
  let report;
  try { report = parseReport(payload); } catch { return; }
  if (!reportHistory.accept(`${info.id}/${report.report_id}`)) return;
  const now = Date.now();
  if (report.sent_at_ms >= (device.lastReportAtMs || 0)) {
    applyReport(device, report, now);
    device.lastReportAtMs = report.sent_at_ms;
  }
  const summary = reportSummary(report);
  device.lastEvent = summary;
  device.lastTopic = topic;
  if (!selectedDeviceId) selectedDeviceId = info.id;
  if (packet.retain) { render(); return; }
  if (!runtime.api) {
    device.messages += 1;
    messageCount += 1;
  }
  events.unshift({timestamp: now, topic, payload: summary, raw: report.lines.map(line => line.text).join("\n"),
    severity: reportSeverity(report), deviceId: info.id, sourceId: null});
  if (events.length > MAX_EVENTS) events.length = MAX_EVENTS;
  render();
}

function isOnline(device) {
  return telemetryOnline(device, client?.connected);
}

function statusBadge(label, className) {
  return `<span class="badge badge-${className}">${escapeHtml(label)}</span>`;
}

function formatAgo(timestamp) {
  if (!timestamp) return "从未";
  const seconds = Math.max(0, Math.floor((Date.now() - timestamp) / 1000));
  if (seconds < 2) return "刚刚";
  if (seconds < 60) return `${seconds} 秒前`;
  const minutes = Math.floor(seconds / 60);
  if (minutes < 60) return `${minutes} 分钟前`;
  return `${Math.floor(minutes / 60)} 小时前`;
}

function formatUptime(ms) {
  if (!Number.isFinite(ms)) return "—";
  const totalSeconds = Math.floor(ms / 1000);
  const hours = Math.floor(totalSeconds / 3600);
  const minutes = Math.floor((totalSeconds % 3600) / 60);
  return `${hours}h ${minutes}m`;
}

function renderMetrics() {
  const managed = [...devices.values()].filter((device) => device.enabled);
  elements.onlineMetric.textContent = managed.filter(isOnline).length;
  elements.totalDeviceMetric.textContent = `共 ${managed.length} 台`;
  elements.criticalMetric.textContent = managed.filter((device) => isOnline(device) && device.beliefs.server_critical === true).length;
  elements.lockedMetric.textContent = managed.filter((device) => device.locked).length;
  elements.messageMetric.textContent = messageCount?.toLocaleString("zh-CN") ?? "—";
}

function renderDevices() {
  const list = [...devices.values()].sort((a, b) => {
    // Keep rows in place while incoming messages update their status.
    return Number(a.kind === "agent") - Number(b.kind === "agent") || a.id.localeCompare(b.id);
  });
  elements.emptyDevices.hidden = list.length > 0;
  elements.deviceTableBody.innerHTML = list.map((device) => {
    const online = isOnline(device);
    const healthLabel = { healthy: "健康", warning: "注意", critical: "严重", unknown: "未知" }[online ? device.health : "unknown"];
    return `
      <tr data-device-id="${escapeHtml(device.id)}" class="${selectedDeviceId === device.id ? "is-selected" : ""}">
        <td><span class="device-name">${escapeHtml(device.name)}</span><span class="device-id">${escapeHtml(device.id)} · ${escapeHtml(device.location)}</span></td>
        <td>${device.enabled ? statusBadge(online ? "在线" : "离线", online ? "online" : "offline") : statusBadge("已停用", "disabled")}</td>
        <td>${statusBadge(healthLabel, online ? device.health : "unknown")}</td>
        <td class="mono">${formatAgo(device.lastSeen)}</td>
        <td><div class="row-actions"><button class="icon-button" data-action="edit" aria-label="编辑 ${escapeHtml(device.name)}">✎</button></div></td>
      </tr>`;
  }).join("");
}

function renderDetail() {
  const device = devices.get(selectedDeviceId);
  elements.detailEmpty.hidden = Boolean(device);
  elements.detailContent.hidden = !device;
  if (!device) return;

  const online = isOnline(device);
  elements.detailKind.textContent = "BDI AGENT";
  elements.detailTitle.textContent = device.name;
  elements.detailId.textContent = device.id;
  elements.detailLocation.textContent = device.location;
  elements.detailLastSeen.textContent = formatAgo(device.lastSeen);
  elements.detailTransport.textContent = device.transport;
  elements.detailUptime.textContent = formatUptime(device.uptimeMs);
  elements.detailCpu.textContent = Number.isFinite(device.cpu) ? `${device.cpu}%` : "—";
  elements.detailTemp.textContent = Number.isFinite(device.temp) ? `${device.temp}°C` : "—";
  elements.detailMessages.textContent = device.messages?.toLocaleString("zh-CN") ?? "—";
  elements.detailFaults.textContent = device.faults.toLocaleString("zh-CN");
  elements.detailLastEvent.textContent = device.lastEvent;
  elements.detailBadges.innerHTML = [
    statusBadge(device.enabled ? (online ? "在线" : "离线") : "已停用", device.enabled ? (online ? "online" : "offline") : "disabled"),
    statusBadge({ healthy: "健康", warning: "注意", critical: "严重", unknown: "未知" }[online ? device.health : "unknown"], online ? device.health : "unknown"),
    ...(device.locked ? [statusBadge("监测到锁定", "locked")] : [])
  ].join("");
  elements.beliefList.innerHTML = Object.entries(device.beliefs).map(([name, value]) =>
    `<span class="belief ${value === true ? "is-true" : ""}">${escapeHtml(name)} = ${value === null ? "?" : value}</span>`
  ).join("");
}

function renderEvents() {
  const severity = elements.severityFilter.value;
  const query = elements.eventSearch.value.trim().toLowerCase();
  const filtered = events.filter((event) => {
    const severityMatches = severity === "all" || event.severity === severity;
    const queryMatches = !query || `${event.topic} ${event.payload} ${event.raw || ""} ${event.deviceId} ${event.sourceId || ""}`.toLowerCase().includes(query);
    return severityMatches && queryMatches;
  });
  elements.emptyEvents.hidden = filtered.length > 0;
  if (paused) return;
  const opened = new Set([...elements.eventTableBody.querySelectorAll("details[open]")].map(detail => detail.dataset.time));
  elements.eventTableBody.innerHTML = filtered.slice(0, 100).map((event) => `
    <tr>
      <td>${new Date(event.timestamp).toLocaleTimeString("zh-CN", { hour12: false })}</td>
      <td>${statusBadge(event.severity === "critical" ? "严重" : event.severity === "warning" ? "警告" : "信息", event.severity)}</td>
      <td>${escapeHtml(event.deviceId)}${event.sourceId ? `<span class="device-id">监测对象 ${escapeHtml(event.sourceId)}</span>` : ""}</td>
      <td title="${escapeHtml(event.topic)}">${escapeHtml(event.topic)}</td>
      <td class="event-payload"><details data-time="${event.timestamp}" ${opened.has(String(event.timestamp)) ? "open" : ""}><summary title="${escapeHtml(event.payload)}">${escapeHtml(event.payload)}</summary><pre>${escapeHtml(event.raw)}</pre></details></td>
    </tr>`).join("");
}

function render() {
  renderMetrics();
  renderDevices();
  renderDetail();
  renderEvents();
}

function setConnectionState(state, label) {
  elements.connectionState.className = `connection-state is-${state}`;
  elements.connectionLabel.textContent = label;
  elements.connectButton.textContent = state === "online" || state === "connecting" ? "断开" : "连接 MQTT";
}

function connectMqtt() {
  if (client) disconnectMqtt();
  if (!runtime) return;
  const password = elements.mqttPassword.value;
  const url = `${connection.protocol}://${connection.host}:${connection.port}${connection.path}`;
  setConnectionState("connecting", "连接中");
  client = mqtt.connect(url, {
    username: connection.username,
    password,
    clean: true,
    clientId: `esp-diag-ui-${Math.random().toString(16).slice(2, 10)}`,
    connectTimeout: 5_000,
    reconnectPeriod: 2_000,
    keepalive: 30
  });
  client.on("connect", () => {
    setConnectionState("online", "已连接");
    client.subscribe(agentTopics(runtime.topicRoot, runtime.agentIds), { qos: 1 }, (error) => {
      if (error) showToast(`订阅失败：${error.message}`);
      else showToast("已订阅 ESP32 上报数据");
    });
  });
  client.on("message", (topic, buffer, packet) => applyMessage(topic, buffer.toString(), packet));
  client.on("reconnect", () => setConnectionState("connecting", "重连中"));
  client.on("offline", () => setConnectionState("offline", "连接中断"));
  client.on("error", (error) => {
    setConnectionState("offline", "连接失败");
    showToast(`MQTT：${error.message}`);
  });
  client.on("close", () => {
    if (client) setConnectionState("offline", "未连接");
  });
}

function disconnectMqtt() {
  const activeClient = client;
  client = null;
  if (activeClient) activeClient.end(true);
  setConnectionState("offline", "未连接");
}

function fillConnectionForm() {
  elements.mqttProtocol.value = connection.protocol;
  elements.mqttHost.value = connection.host;
  elements.mqttPort.value = connection.port;
  elements.mqttPath.value = connection.path;
  elements.mqttUsername.value = connection.username;
  elements.mqttPassword.value = runtime?.password || "";
  elements.autoConnect.checked = connection.autoConnect;
}

function saveConnection() {
  connection = {
    protocol: elements.mqttProtocol.value,
    host: elements.mqttHost.value.trim(),
    port: Number(elements.mqttPort.value),
    path: elements.mqttPath.value.trim().startsWith("/") ? elements.mqttPath.value.trim() : `/${elements.mqttPath.value.trim()}`,
    username: elements.mqttUsername.value.trim(),
    autoConnect: elements.autoConnect.checked
  };
  writeJson(STORAGE_CONNECTION, connection);
  elements.connectionDialog.close();
  connectMqtt();
}

function openDeviceDialog(id = null) {
  editingDeviceId = id;
  const device = id ? devices.get(id) : null;
  elements.deviceDialogTitle.textContent = device ? "编辑设备" : "添加设备";
  elements.deviceIdInput.value = device?.id || "";
  elements.deviceIdInput.disabled = Boolean(device);
  elements.deviceNameInput.value = device?.name || "";
  elements.deviceLocationInput.value = device?.location === "未设置" ? "" : device?.location || "";
  elements.deviceEnabledInput.checked = device?.enabled ?? true;
  elements.deleteDeviceButton.hidden = !device;
  elements.deviceDialog.showModal();
  setTimeout(() => (device ? elements.deviceNameInput : elements.deviceIdInput).focus(), 0);
}

async function saveDevice() {
  const id = (editingDeviceId || elements.deviceIdInput.value).trim();
  const name = elements.deviceNameInput.value.trim();
  if (!id || !name || !/^[A-Za-z0-9._-]{1,64}$/.test(id) || ["server", "switch", "service"].includes(id)) return;
  const metadata = {
    kind: "agent",
    name,
    location: elements.deviceLocationInput.value.trim() || "未设置",
    enabled: elements.deviceEnabledInput.checked,
    manuallyAdded: devices.get(id)?.manuallyAdded || !editingDeviceId
  };
  if (runtime?.api) {
    try { await registryRequest(runtime.api, `/devices/${encodeURIComponent(id)}`, "PUT", metadataFields(metadata)); }
    catch (error) { showToast(error.message); return; }
  }
  subscribeAgent(id);
  const existing = ensureDevice(id, "agent");
  deviceMetadata[id] = metadata;
  Object.assign(existing, metadata);
  writeJson(STORAGE_DEVICES, deviceMetadata);
  selectedDeviceId = id;
  elements.deviceDialog.close();
  render();
  showToast(runtime?.api ? "设备信息已保存到数据库" : (editingDeviceId ? "设备信息已更新" : "设备已添加"));
}

async function deleteDevice() {
  const id = editingDeviceId;
  if (!id) return;
  if (runtime?.api) {
    try { await registryRequest(runtime.api, `/devices/${encodeURIComponent(id)}`, "DELETE"); }
    catch (error) { showToast(error.message); return; }
  }
  delete deviceMetadata[id];
  devices.delete(id);
  writeJson(STORAGE_DEVICES, deviceMetadata);
  if (selectedDeviceId === id) selectedDeviceId = devices.keys().next().value || null;
  elements.deviceDialog.close();
  render();
  showToast(runtime?.api ? "设备已移除，历史保留；重新连接硬件或手动添加后恢复" : "设备已从当前浏览器移除；再次上报时会自动发现");
}

function subscribeAgent(id) {
  if (runtime.agentIds.includes(id)) return;
  runtime.agentIds.push(id);
  if (client?.connected) client.subscribe(agentTopics(runtime.topicRoot, [id]), {qos: 1});
}

let registrySyncing = false;
async function syncRegistry() {
  if (!runtime?.api || registrySyncing) return;
  registrySyncing = true;
  try {
    const {registry: saved, history} = await loadArchiveSnapshot(runtime.api);
    const ids = new Set(saved.devices.map(row => row.id));
    for (const id of devices.keys()) if (!ids.has(id)) devices.delete(id);
    deviceMetadata = {};
    for (const row of saved.devices) {
      subscribeAgent(row.id);
      deviceMetadata[row.id] = {...metadataFields(row), manuallyAdded: row.manuallyAdded, kind: "agent"};
      const device = createDevice(row.id, "agent");
      devices.set(row.id, device);
      Object.assign(device, deviceMetadata[row.id]);
      device.messages = row.report_count;
      if (row.latest_report) {
        const report = parseReport(JSON.stringify(row.latest_report));
        applyReport(device, report);
        device.lastReportAtMs = report.sent_at_ms;
        device.lastEvent = reportSummary(report);
        device.lastTopic = `${runtime.topicRoot}/${row.id}/report`;
      }
      if (row.availability && row.availability_at_ms > (device.availabilityAtMs || 0)) {
        applyTelemetry(device, {channel: "availability"}, {state: row.availability}, row.availability_at_ms);
      }
    }
    if (!devices.has(selectedDeviceId)) selectedDeviceId = devices.keys().next().value || null;
    messageCount = saved.devices.reduce((total, row) => total + row.report_count, 0);
    elements.messageScope.textContent = "当前连接 · 数据库";
    events = history.map(item => {
        const report = item.report;
        return {timestamp: report.sent_at_ms, topic: `${runtime.topicRoot}/${item.agent_id}/report`,
          payload: reportSummary(report), raw: report.lines.map(line => line.text).join("\n"),
          severity: reportSeverity(report), deviceId: item.agent_id, sourceId: null};
    }).sort((a, b) => b.timestamp - a.timestamp).slice(0, MAX_EVENTS);
    render();
  } finally { registrySyncing = false; }
}

let toastTimer;
function showToast(message) {
  clearTimeout(toastTimer);
  elements.toast.textContent = message;
  elements.toast.classList.add("is-visible");
  toastTimer = setTimeout(() => elements.toast.classList.remove("is-visible"), 2_600);
}

function updateClock() {
  const now = new Date();
  elements.clock.textContent = now.toLocaleTimeString("zh-CN", { hour12: false });
  elements.clockDate.textContent = now.toLocaleDateString("zh-CN", { year: "numeric", month: "2-digit", day: "2-digit", weekday: "short" });
}

function loadManagedDevices() {
  for (const [id, metadata] of Object.entries(deviceMetadata)) {
    if (!runtime?.agentIds.includes(id)) continue;
    const device = createDevice(id, "agent");
    Object.assign(device, metadata);
    devices.set(id, device);
  }
}

elements.connectionSettingsButton.addEventListener("click", () => {
  fillConnectionForm();
  elements.connectionDialog.showModal();
});
elements.connectButton.addEventListener("click", () => {
  if (client) disconnectMqtt();
  else connectMqtt();
});
elements.connectionForm.addEventListener("submit", (event) => {
  if (event.submitter?.value === "cancel") return;
  event.preventDefault();
  if (elements.connectionForm.reportValidity()) saveConnection();
});
elements.addDeviceButton.addEventListener("click", () => openDeviceDialog());
elements.editDeviceButton.addEventListener("click", () => openDeviceDialog(selectedDeviceId));
elements.deviceForm.addEventListener("submit", (event) => {
  if (event.submitter?.value === "cancel") return;
  event.preventDefault();
  if (elements.deviceForm.reportValidity()) saveDevice();
});
elements.deleteDeviceButton.addEventListener("click", deleteDevice);
elements.deviceTableBody.addEventListener("click", (event) => {
  const row = event.target.closest("tr[data-device-id]");
  if (!row) return;
  const id = row.dataset.deviceId;
  if (event.target.closest("[data-action='edit']")) openDeviceDialog(id);
  else {
    selectedDeviceId = id;
    render();
  }
});
elements.severityFilter.addEventListener("change", renderEvents);
elements.eventSearch.addEventListener("input", renderEvents);
elements.pauseButton.addEventListener("click", () => {
  paused = !paused;
  elements.pauseButton.textContent = paused ? "继续" : "暂停";
  if (!paused) renderEvents();
});
elements.clearButton.addEventListener("click", () => {
  events = [];
  elements.eventTableBody.innerHTML = "";
  renderEvents();
});

async function boot() {
  try {
    runtime = await loadRuntimeConfig();
    connection = {...connectionFields(runtime), ...(runtime.configured ? {} : readJson(STORAGE_CONNECTION, {}))};
    if (runtime.api) {
      elements.messageTitle.textContent = "实时上报";
      elements.messageMetric.title = "当前 USB 连接的已入库上报数；刷新不清零，重新连接另起计数。";
      elements.emptyDevicesTitle.textContent = "暂无管理设备";
      elements.emptyDevicesHint.textContent = "连接 ESP32 后自动发现；已移除设备可重新插入或手动添加。";
      elements.eventsTitle.textContent = "实时上报记录";
      elements.emptyEventsTitle.textContent = "暂无设备上报记录";
      elements.emptyEventsHint.textContent = "等待当前连接的上报；以往记录可在历史查询中查看。";
      try { await syncRegistry(); }
      catch (error) { showToast(error.message); }
      setInterval(() => syncRegistry().catch(() => {
        elements.messageScope.textContent = "数据库暂不可用，自动重试";
      }), 5_000);
    } else {
      messageCount = 0;
      elements.messageTitle.textContent = "MQTT 消息";
      elements.messageScope.textContent = "本次会话";
      loadManagedDevices();
    }
    fillConnectionForm();
    updateClock();
    render();
    if (connection.autoConnect) connectMqtt();
  } catch (error) {
    setConnectionState("offline", "配置未就绪");
    showToast(error.message);
  }
}
boot();
setInterval(() => {
  updateClock();
  renderMetrics();
  renderDevices();
  renderDetail();
}, 1_000);
