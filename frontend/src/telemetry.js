export const SOURCE_TTL_MS = 90_000;
export function reportFreshnessMs(interval) {
  return Number.isFinite(interval) && interval >= 1000 && interval <= 300000
    ? Math.max(15000, 2 * interval + 5000) : 15000;
}
export function agentTopics(topicRoot = "h3c", agentIds = ["+"]) {
  return agentIds.flatMap((id) => ["report", "availability"]
    .map((channel) => `${topicRoot}/${id}/${channel}`));
}
export const AGENT_TOPICS = agentTopics();
export function parseFields(payload) {
  const fields = {};
  for (const token of payload.split(/\s+/)) {
    const index = token.indexOf("=");
    if (index > 0) fields[token.slice(0, index)] = token.slice(index + 1);
  }
  return fields;
}
export function topicInfo(topic, fields = {}, topicRoot = "h3c") {
  const parts = topic.split("/");
  if (parts[0] !== topicRoot) return { id: "unknown", kind: "unknown", channel: parts.at(-1), sourceId: null };
  if (["server", "switch", "service"].includes(parts[1])) {
    return { id: parts[2], kind: parts[1], channel: parts[3] || "log", sourceId: null };
  }
  if (parts[2] === "serial") return {id: parts[1], kind: "agent", channel: "serial", sourceId: null};
  return { id: parts[1], kind: "agent", channel: parts[2] || "status", sourceId: fields.device || null };
}
export function applyTelemetry(device, info, fields, now = Date.now()) {
  // Raw USB frames can mention server beliefs; they are not ESP32's own health.
  if (info.channel === "serial") return;
  if (device.kind === "agent" && info.channel === "status" && fields.reasoning_cycle) {
    device.health = fields.reasoning_cycle === "ACTIVE" ? "healthy" : "warning";
    device.availability = "ONLINE";
  }
  if (device.kind === "agent" && info.channel === "observation" && fields.device) {
    device.observations ??= {};
    device.observations[fields.device] = {...fields};
    const observations = Object.values(device.observations);
    device.locked = observations.some((value) => value.locked === "true");
    device.faults = observations.reduce((count, value) => count + Number(value.active_faults || 0), 0);
    device.beliefs.server_healthy = observations.every((value) => value.healthy === "true");
    device.beliefs.server_critical = observations.some((value) => value.critical === "true");
    device.beliefs.server_locked = device.locked;
    device.beliefs.switch_link_fault = observations.some((value) => value.link_fault === "true");
  }
  if (info.channel === "health") {
    const state = { NORMAL: "healthy", DEGRADED: "warning", FAULT: "critical", UNKNOWN: "unknown" };
    device.health = state[fields.state] || "unknown";
    device.locked = fields.locked === "true";
    device.activeFaults = Number(fields.active_faults || 0);
    device.faults = device.activeFaults;
    device.availability = fields.availability || device.availability;
    device.beliefs.server_healthy = fields.healthy ? fields.healthy === "true" : fields.state === "NORMAL";
    device.beliefs.server_critical = fields.critical ? fields.critical === "true" : fields.state === "FAULT";
    device.beliefs.server_locked = device.locked;
    device.beliefs.switch_link_fault = fields.link_fault === "true";
  }
  if (info.channel === "availability") {
    device.availability = fields.state;
    device.availabilityAtMs = now;
  }
  // Arrival of a retained message or an action is not a fresh observation.
  if (["health", "availability", "status"].includes(info.channel)) {
    if (fields.seen_at_ms) device.lastSeen = Math.min(now, Number(fields.seen_at_ms));
    else if (device.kind === "agent" && info.channel === "status" && fields.reasoning_cycle) device.lastSeen = now;
  }
  if (device.kind !== "agent" && fields.belief && Object.hasOwn(device.beliefs, fields.belief)) {
    device.beliefs[fields.belief] = fields.value === "true";
  }
}
export function telemetryOnline(device, connected, now = Date.now()) {
  return Boolean(connected && device.availability !== "OFFLINE" && device.lastSeen &&
    now - device.lastSeen < (device.kind === "agent" ? reportFreshnessMs(device.reportIntervalMs) : SOURCE_TTL_MS));
}
