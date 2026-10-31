import test from "node:test";
import assert from "node:assert/strict";
import {parseReport, ReportHistory, applyReport, reportSummary, reportSeverity} from "../src/reports.js";
import {telemetryOnline, applyTelemetry} from "../src/telemetry.js";

const report = () => ({schema_version: 1, report_id: "one", status: {reasoning_cycle: "ACTIVE", seen_at_ms: "1000", transport: "USB", uptime_ms: "5000"},
  observations: [{device: "a", state: "DEGRADED", healthy: "false", locked: "true", critical: "false", active_faults: "1"}],
  lines: [{at_ms: 1000, text: "[BDI][STATE] device=a state=DEGRADED"}, {at_ms: 1000, text: "[BDI][RECEIPT] result=ACCEPTED"}]});
test("one bundled report updates ESP32 and monitored state together", () => {
  const r = parseReport(JSON.stringify(report()));
  const d = {kind: "agent", beliefs: {server_healthy: null, server_locked: null}};
  applyReport(d, r, 2000);
  assert.equal(d.health, "healthy");
  assert.equal(d.locked, true);
  assert.equal(d.faults, 1);
  assert.equal(d.lastSeen, 1000);
  assert.equal(reportSummary(r).includes("RECEIPT"), false);
  assert.match(reportSummary(r), /故障 1 · 锁定 是/);
  assert.equal(reportSeverity(r), "warning");
});
test("QoS redelivery does not create another row, distinct action reports remain visible", () => {
  const history = new ReportHistory();
  assert.equal(history.accept("one"), true);
  assert.equal(history.accept("one"), false);
  assert.equal(history.accept("two"), true);
});
test("retained bundles and receipts do not make old ESP32 status fresh", () => {
  const d = {kind: "agent", beliefs: {}};
  applyReport(d, report(), 20000);
  assert.equal(telemetryOnline(d, true, 20000), false);
  assert.equal(d.lastSeen, 1000);
});
test("a complete new snapshot replaces old faults without inferring health from an ACK", () => {
  const d = {kind: "agent", beliefs: {}};
  applyReport(d, report(), 2000);
  const r = report();
  r.lines = [{at_ms: 2000, text: "[BDI][ACK] result=SUCCESS"}];
  applyReport(d, r, 2000);
  assert.equal(d.locked, true);
  assert.equal(d.faults, 1);
  r.observations = [{device: "a", state: "NORMAL", healthy: "true", locked: "false", active_faults: "0"}];
  applyReport(d, r, 2000);
  assert.equal(d.locked, false);
  assert.equal(d.faults, 0);
});
test("invalid bundles are rejected", () => {
  assert.throws(() => parseReport("[BDI][STATUS]"));
  assert.throws(() => parseReport(JSON.stringify({...report(), lines: [{}]})));
});
test("a cached report cannot override a newer offline notification", () => {
  const d = {kind: "agent", beliefs: {}};
  applyTelemetry(d, {channel: "availability"}, {state: "OFFLINE"}, 1500);
  applyReport(d, report(), 2000);
  assert.equal(telemetryOnline(d, true, 2000), false);
  applyReport(d, {...report(), status: {...report().status, seen_at_ms: "2500"}}, 3000);
  assert.equal(telemetryOnline(d, true, 3000), true);
});
