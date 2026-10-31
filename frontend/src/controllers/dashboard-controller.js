import {DeviceModel, deviceProfile as metadataFields} from "../models/device-model.js";
import {createDashboardView} from "../views/dashboard-view.js";
import {renderLoopView} from "../views/loop-view.js";
import {loopIncidents, demoLoopReports} from "../loops.js";
import mqtt from "mqtt";
import { parseFields, topicInfo, applyTelemetry, telemetryOnline, agentTopics } from "../telemetry.js";
import { loadRuntimeConfig, connectionFields } from "../runtime-config.js";
import {parseReport, ReportHistory, applyReport, reportSummary, reportSeverity} from "../reports.js";
import {registryRequest, loadArchiveSnapshot} from "../services/registry-service.js";

// Controller: coordinates model, views, UI actions and services.
export function startDashboard() {
  const STORAGE_CONNECTION = "esp-diag-connection-v1";
  const STORAGE_DEVICES = "esp-diag-devices-v1";
  const MAX_EVENTS = 300;

  const elements = Object.fromEntries(
    [
      "connectionState", "connectionLabel", "connectionSettingsButton", "connectButton",
      "clock", "clockDate", "onlineMetric", "totalDeviceMetric", "criticalMetric",
      "lockedMetric", "messageScope", "deviceTableBody", "emptyDevices", "detailEmpty",
      "detailContent", "detailKind", "detailTitle", "detailId", "detailNumber", "detailBadges",
      "emptyDevicesTitle", "emptyDevicesHint", "eventsTitle", "emptyEventsTitle", "emptyEventsHint",
      "detailLocation", "detailLastSeen", "detailTransport", "detailUptime", "detailCpu",
      "detailTemp", "detailMessages", "detailFaults", "beliefList", "detailLastEvent",
      "editDeviceButton", "addDeviceButton", "eventTableBody", "emptyEvents",
      "severityFilter", "eventSearch", "pauseButton", "clearButton", "connectionDialog",
      "connectionForm", "mqttProtocol", "mqttHost", "mqttPort", "mqttPath", "mqttUsername",
      "mqttPassword", "autoConnect", "deviceDialog", "deviceForm", "deviceDialogTitle",
      "deviceIdInput", "deviceNumberInput", "deviceNameInput", "deviceLocationInput", "deviceEnabledInput",
      "deleteDeviceButton", "toast", "reportingForm", "reportingSeconds", "saveReportingButton", "reportingResult"
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
  const model = new DeviceModel(readJson(STORAGE_DEVICES, {}));
  let client = null;
  let selectedDeviceId = null;
  let editingDeviceId = null;
  let paused = false;
  let events = [];
  const devices = model.devices;
  const view = createDashboardView(elements);
  const reportHistory = new ReportHistory();
  const loopReports = new Map();
  let loopAgentId=null, loopSelected=null, loopDemo=false, loopDemoAt=0, loopRenderedSignature=null;
  const loopEl=Object.fromEntries(["loopDialog","loopTitle","loopAgent","loopConnection","loopIncidentList","loopBody","closeLoopButton","loopDemoButton","loopDemoNotice"].map(id=>[id,document.getElementById(id)]));
  function rememberLoopReport(id,report){
    if(!loopReports.has(id))loopReports.set(id,new Map());
    const cache=loopReports.get(id);cache.set(report.report_id,report);
    while(cache.size>300)cache.delete(cache.keys().next().value);
  }

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

  function createDevice(id, kind = "manual") { return model.create(id, kind, runtime); }
  function ensureDevice(id, kind) { return model.ensure(id, kind, runtime); }

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
    rememberLoopReport(info.id,report);
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
    }
    events.unshift({timestamp: now, topic, payload: summary, raw: report.lines.map(line => line.text).join("\n"),
      severity: reportSeverity(report), deviceId: info.id, sourceId: null});
    if (events.length > MAX_EVENTS) events.length = MAX_EVENTS;
    render();
  }

  function isOnline(device) {
    return telemetryOnline(device, client?.connected);
  }

  function renderMetrics() { view.renderMetrics(devices, isOnline); }
  function renderDevices() { view.renderDevices(devices, selectedDeviceId, Boolean(runtime?.api), isOnline); }
  function renderDetail() {
    renderLoopDialog();
    const device = devices.get(selectedDeviceId);
    view.renderDetail(device, isOnline);
    if (device) loadReportingSettings(device);
  }
  function loadReportingSettings(device) {
    elements.reportingForm.hidden = !runtime?.api || runtime.reporting?.agentId !== device.id;
    if (!elements.reportingForm.hidden && elements.reportingForm.dataset.deviceId !== device.id) {
      elements.reportingForm.dataset.deviceId = device.id;
      elements.reportingSeconds.value = String((runtime.reporting.intervalMs || 60000) / 1000);
      const id = device.id;
      const initialValue = elements.reportingSeconds.value;
      registryRequest(runtime.api, `/devices/${encodeURIComponent(id)}/reporting`).then(value => {
        if (elements.reportingForm.dataset.deviceId === id && elements.reportingSeconds.value === initialValue) elements.reportingSeconds.value = String(value.interval_ms / 1000);
      }).catch(error => { if (elements.reportingForm.dataset.deviceId === id) elements.reportingResult.textContent = error.message; });
    }
  }
  function renderEvents() { view.renderEvents(events, paused); }

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
    elements.deviceNumberInput.value = device?.device_number || "";
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
    const deviceNumber = elements.deviceNumberInput.value.trim();
    if (!id || !name || !/^[A-Za-z0-9._-]{1,64}$/.test(id) || ["server", "switch", "service"].includes(id)) return;
    if (!/^[A-Za-z0-9._-]{0,32}$/.test(deviceNumber)) { showToast("设备编号最多 32 位，支持字母、数字、点、横线和下划线"); return; }
    const metadata = {
      kind: "agent",
      name,
      device_number: deviceNumber,
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
    model.metadata[id] = metadata;
    Object.assign(existing, metadata);
    writeJson(STORAGE_DEVICES, model.metadata);
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
    delete model.metadata[id];
    devices.delete(id);
    writeJson(STORAGE_DEVICES, model.metadata);
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
      loopReports.clear();
      for(const item of history)rememberLoopReport(item.agent_id,item.report);
      const ids = new Set(saved.devices.map(row => row.id));
      for (const id of devices.keys()) if (!ids.has(id)) devices.delete(id);
      model.metadata = {};
      for (const row of saved.devices) {
        subscribeAgent(row.id);
        model.metadata[row.id] = {...metadataFields(row), manuallyAdded: row.manuallyAdded, kind: "agent"};
        const device = createDevice(row.id, "agent");
        devices.set(row.id, device);
        Object.assign(device, model.metadata[row.id]);
        device.messages = row.report_count;
        if (row.latest_report) {
          const report = parseReport(JSON.stringify(row.latest_report));
          rememberLoopReport(row.id,report);
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


  function openLoopDialog(id){
   loopAgentId=id;loopSelected=null;loopDemo=false;loopRenderedSignature=null;renderLoopDialog();loopEl.loopDialog.showModal();
  }
  function renderLoopDialog() {
    if (!loopAgentId) return;
    const device = devices.get(loopAgentId);
    const incidents = loopIncidents(loopDemo ? demoLoopReports(loopDemoAt) : [...(loopReports.get(loopAgentId)?.values() || [])]);
    const result = renderLoopView(loopEl, {agentId: loopAgentId, device, online: device && isOnline(device),
      demo: loopDemo, incidents, selected: loopSelected, previousSignature: loopRenderedSignature});
    loopSelected = result.selected;
    loopRenderedSignature = result.signature;
  }
  document.getElementById('openLoopButton').addEventListener('click',()=>{if(selectedDeviceId)openLoopDialog(selectedDeviceId);});
  loopEl.closeLoopButton.addEventListener('click',()=>loopEl.loopDialog.close());
  loopEl.loopDialog.addEventListener('close',()=>{loopAgentId=null;});
  loopEl.loopDemoButton.addEventListener('click',()=>{loopDemo=!loopDemo;loopDemoAt=Date.now();loopSelected=null;renderLoopDialog();});
  loopEl.loopIncidentList.addEventListener('click',event=>{const button=event.target.closest('[data-loop-id]');if(button){loopSelected=button.dataset.loopId;renderLoopDialog();}});

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
    for (const [id, metadata] of Object.entries(model.metadata)) {
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
      openLoopDialog(id);
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
  elements.reportingForm.addEventListener("submit", async event => {
    event.preventDefault();
    const id = elements.reportingForm.dataset.deviceId;
    if (!runtime?.api || id !== selectedDeviceId || !elements.reportingForm.reportValidity()) return;
    elements.saveReportingButton.disabled = true;
    try {
      const value = await registryRequest(runtime.api, `/devices/${encodeURIComponent(id)}/reporting`, "PUT",
        {interval_seconds: Number(elements.reportingSeconds.value)});
      runtime.reporting.intervalMs = value.interval_ms;
      elements.reportingResult.textContent = `已保存为 ${value.interval_ms / 1000} 秒，网关自动应用；故障和恢复及时上报。`;
    } catch (error) { elements.reportingResult.textContent = error.message; }
    finally { elements.saveReportingButton.disabled = false; }
  });

  async function boot() {
    try {
      runtime = await loadRuntimeConfig();
      connection = {...connectionFields(runtime), ...(runtime.configured ? {} : readJson(STORAGE_CONNECTION, {}))};
      if (runtime.api) {
        elements.detailMessages.title = "本设备当前 USB 连接的已入库上报数；刷新不清零，重新连接另起计数。";
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

  return {syncRegistry, applyMessage};
}
