import test from "node:test";
import assert from "node:assert/strict";
import {registryRequest, deviceMetadata, loadArchivedReports, loadArchiveSnapshot} from "../src/registry.js";
import {ReportHistory} from "../src/reports.js";
import {parseRuntimeConfig} from "../src/runtime-config.js";

const api = {url: "/api", token: "x".repeat(32)};
test("registry mutations send only metadata and API credentials to the same origin", async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async (url, options) => {
    assert.equal(url, "/api/devices/edge-1");
    assert.equal(options.method, "PUT");
    assert.equal(options.headers.Authorization, `Bearer ${api.token}`);
    assert.deepEqual(JSON.parse(options.body), {name: "设备", location: "A", enabled: true});
    return {ok: true, json: async () => ({id: "edge-1"})};
  };
  try {
    const result = await registryRequest(api, "/devices/edge-1", "PUT", deviceMetadata({name: "设备", location: "A", enabled: true, kind: "agent", latest_report: {}}));
    assert.equal(result.id, "edge-1");
  } finally { globalThis.fetch = original; }
});
test("database write failure is surfaced instead of reporting a successful local save", async () => {
  const original = globalThis.fetch;
  globalThis.fetch = async () => ({ok: false, status: 503});
  try { await assert.rejects(registryRequest(api, "/devices/edge-1", "DELETE"), /数据库暂不可用/); }
  finally { globalThis.fetch = original; }
});
test("runtime API credentials cannot be redirected to another origin", () => {
  const config = {schema_version: 1, topic_root: "h3c", agent_ids: ["edge-1"], mqtt: {url: "ws://localhost/mqtt"}, api};
  assert.deepEqual(parseRuntimeConfig(config).api, api);
  for (const url of ["https://other.test/api", "//other.test/api", "/api?redirect=1"]) {
    assert.throws(() => parseRuntimeConfig({...config, api: {...api, url}}));
  }
});

const archivedReport = (id, timestamp) => ({schema_version: 1, report_id: id, sent_at_ms: timestamp,
  status: null, observations: [], lines: [{text: "[BDI][STATE]", at_ms: timestamp}]});

test("refresh loads persisted logs in original timestamp order and deduplicates live overlap", async () => {
  const original = globalThis.fetch;
  const rows = [archivedReport("latest", 3000), archivedReport("middle", 2000), archivedReport("oldest", 1000)];
  globalThis.fetch = async (url, options) => {
    assert.equal(url, "/api/reports?limit=50&agent=edge-1&scope=history");
    assert.equal(options.headers.Authorization, `Bearer ${api.token}`);
    return {ok: true, json: async () => ({reports: rows.map(report => ({agent_id: "edge-1", report}))})};
  };
  try {
    const history = await loadArchivedReports(api, "edge-1");
    assert.deepEqual(history.map(row => row.report.sent_at_ms), [1000, 2000, 3000]);
    const seen = new ReportHistory();
    seen.accept("latest"); // MQTT can arrive while the database fetch is pending.
    assert.deepEqual(history.filter(row => seen.accept(row.report.report_id)).map(row => row.report.report_id), ["oldest", "middle"]);
  } finally { globalThis.fetch = original; }
});

test("history loading failures propagate so the pages can retry without marking restoration complete", async () => {
  const original = globalThis.fetch;
  let available = false;
  globalThis.fetch = async () => ({ok: available, status: 503,
    json: async () => ({reports: [{agent_id: "edge-1", report: archivedReport("saved", 1000)}]})});
  try {
    await assert.rejects(loadArchivedReports(api), /数据库暂不可用/);
    available = true;
    assert.equal((await loadArchivedReports(api))[0].report.report_id, "saved");
  } finally { globalThis.fetch = original; }
});

test("each archive refresh replaces deleted records and accepts a completely empty database", async () => {
  const original = globalThis.fetch;
  let rows = [archivedReport("keep", 1000), archivedReport("delete", 2000)];
  globalThis.fetch = async url => ({ok: true, json: async () => url === "/api/devices?scope=live"
    ? {devices: [{id: "edge-1", report_count: rows.length}], history_days: 30}
    : {reports: rows.map(report => ({agent_id: "edge-1", report}))}});
  try {
    let snapshot = await loadArchiveSnapshot(api);
    assert.equal(snapshot.registry.devices[0].report_count, 2);
    assert.deepEqual(snapshot.history.map(item => item.report.report_id), ["keep", "delete"]);
    rows = rows.slice(0, 1);
    snapshot = await loadArchiveSnapshot(api);
    assert.deepEqual(snapshot.history.map(item => item.report.report_id), ["keep"]);
    assert.equal(snapshot.registry.devices[0].report_count, 1);
    rows = [];
    snapshot = await loadArchiveSnapshot(api);
    assert.deepEqual(snapshot.history, []);
    assert.equal(snapshot.registry.devices[0].report_count, 0);
  } finally { globalThis.fetch = original; }
});
