import {applyTelemetry, parseFields} from "./telemetry.js";

export function parseReport(payload) {
  const report = JSON.parse(payload);
  if (report.schema_version !== 1 || typeof report.report_id !== "string" || !report.report_id ||
      !Array.isArray(report.lines) || !Array.isArray(report.observations) ||
      !report.lines.every(line => typeof line.text === "string" && Number.isFinite(line.at_ms)) ||
      !report.observations.every(value => value && typeof value.device === "string") ||
      (report.status !== null && (typeof report.status !== "object" || !report.status))) {
    throw new Error("ESP32 上报包格式错误");
  }
  return report;
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
  const faults = report.observations.reduce((count, value) => count + Number(value.active_faults || 0), 0);
  const locked = report.observations.some(value => value.locked === "true");
  const state = report.observations.map(value => value.state).filter(Boolean);
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
  return `推理 ${report.status?.reasoning_cycle || "等待状态"} · 监测 ${report.observations.length} 台 · ${[...new Set(state)].join("/") || "等待结果"} · 故障 ${faults} · 锁定 ${locked ? "是" : "否"}` +
    (events.length ? ` · ${[...new Set(events)].slice(-2).join("；")}` : "");
}

export function reportSeverity(report) {
  const lines = report.lines.map(line => line.text).join("\n");
  if (report.observations.some(value => value.critical === "true") || /result=FAILED|state=FAULT|\[BDI\]\[REJECT/.test(lines)) return "critical";
  if (report.observations.some(value => value.locked === "true" || value.state !== "NORMAL") || /UNLOCK|DEGRADED|LOCKED/.test(lines)) return "warning";
  return "info";
}
