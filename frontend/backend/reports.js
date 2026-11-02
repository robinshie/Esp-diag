import {applyTelemetry, parseFields} from "./telemetry.js";

export function parseReport(payload) {
  const report = JSON.parse(payload);
  const native = report.source === "esp_native" && report.clock === "uptime" && typeof report.boot === "string";
  if (report.schema_version !== 1 || typeof report.report_id !== "string" || !report.report_id ||
      !Array.isArray(report.lines) || (!native && !Array.isArray(report.observations)) ||
      !report.lines.every(line => typeof line.text === "string" && Number.isFinite(line.at_ms)) ||
      (report.observations !== undefined && (!Array.isArray(report.observations) || !report.observations.every(value => value && typeof value.device === "string"))) ||
      (report.status !== null && !(native && report.status === undefined) && (typeof report.status !== "object" || !report.status))) {
    throw new Error("ESP32 上报包格式错误");
  }
  return report;
}

// The archive receipt is a wall clock; the native evidence stays on its boot clock.
export function archiveTime(item) {
  if (Number.isFinite(item.archive_at_ms)) return item.archive_at_ms;
  const received = Date.parse(item.received_at);
  if (Number.isFinite(received)) return received;
  return item.report.clock === "uptime" ? null : item.report.sent_at_ms;
}

export function reportTimeText(report, at = report.sent_at_ms) {
  return report.clock === "uptime" ? `本次开机 +${(at / 1000).toFixed(1)} 秒`
    : new Date(at).toLocaleString("zh-CN", {hour12: false});
}

export class ReportHistory {
  constructor() { this.seen = new Set(); }
  accept(id) {
    if (this.seen.has(id)) return false;
    this.seen.add(id);
    if (this.seen.size > 512) this.seen.delete(this.seen.values().next().value);
    return true;
  }
}

export function applyReport(device, report, now = Date.now()) {
  if (report.source === "esp_native") {
    device.transport = report.transport || "ETH";
    // Reports are evidence, not current telemetry. Never renew online state from a log packet.
    return;
  }
  device.reportIntervalMs = report.report_interval_ms;
  // Retained reports and receipts must not renew the original observation time.
  if (report.status) {
    const offline = device.availability === "OFFLINE" && Number(report.status.seen_at_ms) <= (device.availabilityAtMs || 0);
    applyTelemetry(device, {channel: "status"}, report.status, now);
    if (offline) device.availability = "OFFLINE";
    if (report.status.transport) device.transport = report.status.transport;
    if (report.status.uptime_ms) device.uptimeMs = Number(report.status.uptime_ms);
  } else {
    device.lastSeen = null;
    device.health = "unknown";
  }
  device.observations = {};
  device.locked = false;
  device.faults = 0;
  for (const belief of Object.keys(device.beliefs)) device.beliefs[belief] = null;
  for (const observation of report.observations) applyTelemetry(device, {channel: "observation"}, observation, now);
}

export function reportSummary(report) {
  const observations = report.observations || [];
  const faults = observations.reduce((count, value) => count + Number(value.active_faults || 0), 0);
  const locked = observations.some(value => value.locked === "true");
  const state = observations.map(value => value.state).filter(Boolean);
  const events = report.lines.map(({text}) => {
    const f = parseFields(text);
    const action = f.action === "RECOVER_INTERFACE" || text.startsWith("[BDI][CMD] RECOVER_INTERFACE") ? "接口恢复" : "解锁";
    if (text.startsWith("[BDI][CMD]")) return `请求${action}（第 ${f.attempt || 1} 次）`;
    if (text.startsWith("[BDI][ACK]")) return `${action}${({COMMAND_ACCEPTED: "处理中", ATTEMPT_FAILED: "本次失败", SUCCESS: "成功", FAILED: "失败"})[f.result] || f.result || f.state}`;
    if (text.startsWith("[BDI][RECOVERED]")) return `${f.component} 恢复已确认`;
    if (text.startsWith("[BDI][REJECT")) return `输入被拒绝 ${f.reason || ""}`;
    if (text.startsWith("[GATEWAY]")) return `网关诊断 ${f.reason || ""}`;
    return null;
  }).filter(Boolean);
  const native = report.source === "esp_native";
  return (native ? `ESP 本机记录 · ${reportTimeText(report)} · ${report.lines.length} 条证据` : `推理 ${report.status?.reasoning_cycle || "等待状态"} · 监测 ${observations.length} 台 · ${[...new Set(state)].join("/") || "等待结果"} · 故障 ${faults} · 锁定 ${locked ? "是" : "否"}`) +
    (events.length ? ` · ${[...new Set(events)].slice(-2).join("；")}` : "");
}

export function reportSeverity(report) {
  const lines = report.lines.map(line => line.text).join("\n");
  const observations = report.observations || [];
  if (observations.some(value => value.critical === "true") || /result=FAILED|state=FAULT|\[BDI\]\[REJECT/.test(lines)) return "critical";
  if (observations.some(value => value.locked === "true" || value.state !== "NORMAL") || /UNLOCK|DEGRADED|LOCKED|heartbeat_state=lost|host_key_mismatch/.test(lines)) return "warning";
  return "info";
}
