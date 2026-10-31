import test from "node:test";
import assert from "node:assert/strict";
import { parseFields, topicInfo, applyTelemetry, telemetryOnline, AGENT_TOPICS } from "../src/telemetry.js";
const device = () => ({kind: "server", health: "unknown", availability: null, beliefs: {server_critical: null}});
test("source identities and BDI context are preserved", () => {
  assert.equal(topicInfo("h3c/server/a/health").id, "a");
  assert.equal(topicInfo("h3c/server/b/health").id, "b");
  const agent = topicInfo("h3c/esp32-s3-eth/event", {device: "a"});
  assert.equal(agent.id, "esp32-s3-eth");
  assert.equal(agent.sourceId, "a");
});
test("ESP32 aggregates monitored faults without changing its own runtime health", () => {
  const d = {...device(), kind: "agent", health: "healthy", lastSeen: 1000};
  const info = topicInfo("h3c/esp32-s3-eth/observation/a", {device: "a"});
  applyTelemetry(d, info, {device: "a", state: "DEGRADED", locked: "true", healthy: "false", active_faults: "1"});
  assert.equal(info.id, "esp32-s3-eth");
  assert.equal(d.locked, true);
  assert.equal(d.beliefs.server_locked, true);
  assert.equal(d.health, "healthy");
  assert.equal(d.lastSeen, 1000);
  applyTelemetry(d, info, {device: "a", state: "NORMAL", locked: "false", healthy: "true", active_faults: "0"});
  assert.equal(d.locked, false);
  assert.equal(d.faults, 0);
  assert.equal(d.beliefs.server_healthy, true);
});
test("dashboard subscriptions exclude direct server publications", () => {
  const matches = (topic) => AGENT_TOPICS.some((filter) => new RegExp(`^${filter.replaceAll("+", "[^/]+")}$`).test(topic));
  assert.equal(matches("h3c/esp32-s3-eth/report"), true);
  assert.equal(matches("h3c/esp32-s3-eth/availability"), true);
  assert.equal(matches("h3c/esp32-s3-eth/status"), false);
  assert.equal(matches("h3c/esp32-s3-eth/serial"), false);
  assert.equal(matches("h3c/esp32-s3-eth/observation/a"), false);
  assert.equal(matches("h3c/server/sim-01/health"), false);
  assert.equal(matches("h3c/server/sim-01/live"), false);
});
test("heartbeat and command ACK cannot clear confirmed faults", () => {
  const d = device();
  applyTelemetry(d, {channel: "health"}, parseFields("state=FAULT availability=ONLINE active_faults=2 seen_at_ms=1000"), 1000);
  applyTelemetry(d, {channel: "log"}, parseFields("event=HEARTBEAT status=OK"), 2000);
  applyTelemetry(d, {channel: "action"}, parseFields("result=SUCCESS action=UNLOCK"), 2000);
  assert.equal(d.health, "critical");
  assert.equal(d.activeFaults, 2);
});
test("stale retained online state does not resurrect a node", () => {
  const d = device();
  applyTelemetry(d, {channel: "health"}, parseFields("state=NORMAL availability=ONLINE seen_at_ms=1000"), 200000);
  assert.equal(telemetryOnline(d, true, 200000), false);
});
test("broker disconnection invalidates displayed online status", () => {
  const d = device();
  applyTelemetry(d, {channel: "health"}, parseFields("state=NORMAL availability=ONLINE seen_at_ms=1000"), 1000);
  assert.equal(telemetryOnline(d, true, 2000), true);
  assert.equal(telemetryOnline(d, false, 2000), false);
});
test("partial redundancy and stale source have distinct states", () => {
  const d = device();
  applyTelemetry(d, {channel: "health"}, {state: "DEGRADED", active_faults: "1"});
  assert.equal(d.health, "warning");
  applyTelemetry(d, {channel: "health"}, {state: "UNKNOWN", availability: "OFFLINE"});
  assert.equal(d.health, "unknown");
});
test("real ESP32 status sets agent health and retains its observation time", () => {
  const d = {...device(), kind: "agent"};
  applyTelemetry(d, {channel: "status"}, parseFields("reasoning_cycle=ACTIVE transport=USB uptime_ms=5000 seen_at_ms=1000"), 1000);
  assert.equal(d.health, "healthy");
  assert.equal(telemetryOnline(d, true, 2000), true);
  assert.equal(telemetryOnline(d, true, 20000), false);
  applyTelemetry(d, {channel: "availability"}, {state: "ONLINE"}, 20000);
  assert.equal(telemetryOnline(d, true, 20000), false);
});
test("raw serial frames stay attached to ESP32 even when mentioning a server", () => {
  assert.deepEqual(topicInfo("h3c/esp32-s3-eth/serial", {device: "sim-01"}),
    {id: "esp32-s3-eth", kind: "agent", channel: "serial", sourceId: null});
  const d = {...device(), kind: "agent", health: "healthy"};
  applyTelemetry(d, {channel: "serial"}, {belief: "server_critical", value: "true"});
  assert.equal(d.beliefs.server_critical, null);
  assert.equal(d.health, "healthy");
});
