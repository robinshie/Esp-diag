import mqtt from "mqtt";
import "./styles.css";

const STORAGE_CONNECTION = "esp-diag-connection-v1";
const STORAGE_DEVICES = "esp-diag-devices-v1";
const ONLINE_WINDOW_MS = 15_000;
const MAX_EVENTS = 300;
const DEFAULT_DEVICE_ID = "esp32-s3-eth";

const elements = Object.fromEntries(
  [
    "connectionState", "connectionLabel", "connectionSettingsButton", "connectButton",
    "clock", "clockDate", "onlineMetric", "totalDeviceMetric", "criticalMetric",
    "lockedMetric", "messageMetric", "deviceTableBody", "emptyDevices", "detailEmpty",
    "detailContent", "detailKind", "detailTitle", "detailId", "detailBadges",
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
let deviceMetadata = readJson(STORAGE_DEVICES, {});
let client = null;
let selectedDeviceId = null;
let editingDeviceId = null;
let paused = false;
let messageCount = 0;
let events = [];
const devices = new Map();

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

function parseFields(payload) {
  const fields = {};
  for (const token of payload.split(/\s+/)) {
    const index = token.indexOf("=");
    if (index > 0) fields[token.slice(0, index)] = token.slice(index + 1);
  }
  return fields;
}

function topicInfo(topic) {
  const parts = topic.split("/");
  if (parts[0] !== "h3c") {
    return { id: "unknown", kind: "unknown", channel: parts.at(-1), sourceId: null };
  }
  if (parts[1] === "server") {
    return {
      id: DEFAULT_DEVICE_ID,
      kind: "agent",
      channel: parts[3] || "log",
      sourceId: parts[2] || "server"
    };
  }
  return {
    id: parts[1] || "unknown",
    kind: "agent",
    channel: parts[2] || "status",
    sourceId: null
  };
}

function createDevice(id, kind = "manual") {
  const meta = deviceMetadata[id] || {};
  return {
    id,
    kind,
    name: meta.name || defaultDeviceName(id, kind),
    location: meta.location || "未设置",
    enabled: meta.enabled ?? true,
    manuallyAdded: meta.manuallyAdded ?? false,
    lastSeen: null,
    health: "unknown",
    locked: false,
    transport: "—",
    uptimeMs: null,
    cpu: null,
    temp: null,
    messages: 0,
    faults: 0,
    lastEvent: "尚无数据",
    lastTopic: "—",
    beliefs: {
      server_healthy: null,
      server_critical: null,
      server_locked: null
    }
  };
}

function defaultDeviceName(id, kind) {
  if (id === "esp32-s3-eth") return "ESP32-S3 BDI Agent";
  if (id === "sim-01") return "H3C Server Simulator";
  return kind === "server" ? `Server · ${id}` : `Device · ${id}`;
}

function ensureDevice(id, kind) {
  if (!devices.has(id)) devices.set(id, createDevice(id, kind));
  const device = devices.get(id);
  if (device.kind === "manual" && kind !== "manual") device.kind = kind;
  return device;
}

function severityFor(payload, fields) {
  if (
    fields.level === "CRITICAL" || fields.status === "ALARM" ||
    (fields.belief === "server_critical" && fields.value === "true") ||
    payload.includes("result=FAILED")
  ) return "critical";
  if (
    fields.level === "WARN" || fields.status === "LOCKED" ||
    fields.event === "AUTH_LOCKED" || payload.includes("UNLOCK")
  ) return "warning";
  return "info";
}

function applyMessage(topic, payload) {
  const info = topicInfo(topic);
  const fields = parseFields(payload);
  const severity = severityFor(payload, fields);
  const device = ensureDevice(info.id, info.kind);
  const now = Date.now();

  device.lastSeen = now;
  device.messages += 1;
  device.lastEvent = payload;
  device.lastTopic = topic;
  if (fields.transport) device.transport = fields.transport;
  if (fields.uptime_ms) device.uptimeMs = Number(fields.uptime_ms);
  if (fields.cpu_pct) device.cpu = Number(fields.cpu_pct);
  if (fields.temp_c) device.temp = Number(fields.temp_c);

  if (severity === "critical") {
    device.health = "critical";
    if (info.channel === "log") device.faults += 1;
  }
  if (
    fields.event === "HEARTBEAT" && fields.status === "OK" &&
    device.beliefs.server_critical !== true
  ) device.health = "healthy";
  if (fields.event === "RECOVERY" && fields.status === "OK") device.health = "healthy";
  if (fields.event === "AUTH_LOCKED" || fields.status === "LOCKED") {
    device.locked = true;
    if (device.health !== "critical") device.health = "warning";
  }
  if (fields.belief && Object.hasOwn(device.beliefs, fields.belief)) {
    const value = fields.value === "true";
    device.beliefs[fields.belief] = value;
    if (
      fields.belief === "server_healthy" && value &&
      device.beliefs.server_critical !== true
    ) device.health = device.locked ? "warning" : "healthy";
    if (fields.belief === "server_critical") {
      device.health = value
        ? "critical"
        : device.locked
          ? "warning"
          : device.beliefs.server_healthy === true
            ? "healthy"
            : "unknown";
    }
    if (fields.belief === "server_locked") {
      device.locked = value;
      if (value && device.health !== "critical") device.health = "warning";
      if (!value && device.beliefs.server_critical !== true && device.beliefs.server_healthy === true) {
        device.health = "healthy";
      }
    }
  }
  if (/UNLOCK/i.test(payload) && payload.includes("result=SUCCESS")) device.locked = false;

  messageCount += 1;
  events.unshift({
    timestamp: now,
    topic,
    payload,
    severity,
    deviceId: info.id,
    sourceId: info.sourceId
  });
  if (events.length > MAX_EVENTS) events.length = MAX_EVENTS;
  if (!selectedDeviceId) selectedDeviceId = info.id;
  render();
}

function isOnline(device) {
  return Boolean(device.lastSeen && Date.now() - device.lastSeen < ONLINE_WINDOW_MS);
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
  elements.criticalMetric.textContent = managed.filter((device) => device.health === "critical").length;
  elements.lockedMetric.textContent = managed.filter((device) => device.locked).length;
  elements.messageMetric.textContent = messageCount.toLocaleString("zh-CN");
}

function renderDevices() {
  const list = [...devices.values()].sort((a, b) => {
    if (a.enabled !== b.enabled) return a.enabled ? -1 : 1;
    return (b.lastSeen || 0) - (a.lastSeen || 0);
  });
  elements.emptyDevices.hidden = list.length > 0;
  elements.deviceTableBody.innerHTML = list.map((device) => {
    const online = isOnline(device);
    const healthLabel = { healthy: "健康", warning: "注意", critical: "严重", unknown: "未知" }[device.health];
    return `
      <tr data-device-id="${escapeHtml(device.id)}" class="${selectedDeviceId === device.id ? "is-selected" : ""}">
        <td><span class="device-name">${escapeHtml(device.name)}</span><span class="device-id">${escapeHtml(device.id)} · ${escapeHtml(device.location)}</span></td>
        <td>${device.enabled ? statusBadge(online ? "在线" : "离线", online ? "online" : "offline") : statusBadge("已停用", "disabled")}</td>
        <td>${statusBadge(healthLabel, device.health)}</td>
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
  elements.detailKind.textContent = device.kind === "server" ? "SERVER" : device.kind === "agent" ? "BDI AGENT" : "MANAGED DEVICE";
  elements.detailTitle.textContent = device.name;
  elements.detailId.textContent = device.id;
  elements.detailLocation.textContent = device.location;
  elements.detailLastSeen.textContent = formatAgo(device.lastSeen);
  elements.detailTransport.textContent = device.transport;
  elements.detailUptime.textContent = formatUptime(device.uptimeMs);
  elements.detailCpu.textContent = Number.isFinite(device.cpu) ? `${device.cpu}%` : "—";
  elements.detailTemp.textContent = Number.isFinite(device.temp) ? `${device.temp}°C` : "—";
  elements.detailMessages.textContent = device.messages.toLocaleString("zh-CN");
  elements.detailFaults.textContent = device.faults.toLocaleString("zh-CN");
  elements.detailLastEvent.textContent = device.lastEvent;
  elements.detailBadges.innerHTML = [
    statusBadge(device.enabled ? (online ? "在线" : "离线") : "已停用", device.enabled ? (online ? "online" : "offline") : "disabled"),
    statusBadge({ healthy: "健康", warning: "注意", critical: "严重", unknown: "未知" }[device.health], device.health),
    ...(device.locked ? [statusBadge("已锁定", "locked")] : [])
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
    const queryMatches = !query || `${event.topic} ${event.payload} ${event.deviceId} ${event.sourceId || ""}`.toLowerCase().includes(query);
    return severityMatches && queryMatches;
  });
  elements.emptyEvents.hidden = filtered.length > 0;
  if (paused) return;
  elements.eventTableBody.innerHTML = filtered.slice(0, 100).map((event) => `
    <tr>
      <td>${new Date(event.timestamp).toLocaleTimeString("zh-CN", { hour12: false })}</td>
      <td>${statusBadge(event.severity === "critical" ? "严重" : event.severity === "warning" ? "警告" : "信息", event.severity)}</td>
      <td>${escapeHtml(event.deviceId)}${event.sourceId ? `<span class="device-id">来源 ${escapeHtml(event.sourceId)}</span>` : ""}</td>
      <td title="${escapeHtml(event.topic)}">${escapeHtml(event.topic)}</td>
      <td class="event-payload" title="${escapeHtml(event.payload)}">${escapeHtml(event.payload)}</td>
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
  const password = elements.mqttPassword.value || "esp32-bdi-test";
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
    client.subscribe("h3c/#", { qos: 1 }, (error) => {
      if (error) showToast(`订阅失败：${error.message}`);
      else showToast("已订阅 h3c/#");
    });
  });
  client.on("message", (topic, buffer) => applyMessage(topic, buffer.toString()));
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
  elements.mqttPassword.value = "esp32-bdi-test";
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

function saveDevice() {
  const id = (editingDeviceId || elements.deviceIdInput.value).trim();
  const name = elements.deviceNameInput.value.trim();
  if (!id || !name || !/^[A-Za-z0-9._-]+$/.test(id)) return;
  const existing = ensureDevice(id, editingDeviceId ? devices.get(id)?.kind : "manual");
  const metadata = {
    name,
    location: elements.deviceLocationInput.value.trim() || "未设置",
    enabled: elements.deviceEnabledInput.checked,
    manuallyAdded: existing.manuallyAdded || !editingDeviceId
  };
  deviceMetadata[id] = metadata;
  Object.assign(existing, metadata);
  writeJson(STORAGE_DEVICES, deviceMetadata);
  selectedDeviceId = id;
  elements.deviceDialog.close();
  render();
  showToast(editingDeviceId ? "设备信息已更新" : "设备已添加");
}

function deleteDevice() {
  const id = editingDeviceId;
  if (!id) return;
  delete deviceMetadata[id];
  devices.delete(id);
  writeJson(STORAGE_DEVICES, deviceMetadata);
  if (selectedDeviceId === id) selectedDeviceId = devices.keys().next().value || null;
  elements.deviceDialog.close();
  render();
  showToast("设备已从当前浏览器移除；再次上报时会自动发现");
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
    const device = createDevice(id, "manual");
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

loadManagedDevices();
fillConnectionForm();
updateClock();
render();
setInterval(() => {
  updateClock();
  renderMetrics();
  renderDevices();
  renderDetail();
}, 1_000);
if (connection.autoConnect) connectMqtt();
