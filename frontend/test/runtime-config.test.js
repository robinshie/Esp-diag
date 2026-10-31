import test from "node:test";
import assert from "node:assert/strict";
import {parseRuntimeConfig, connectionFields, loadRuntimeConfig} from "../src/runtime-config.js";
import {agentTopics, topicInfo} from "../src/telemetry.js";

const config = {schema_version: 1, mqtt: {url: "wss://monitor.example.com/mqtt", username: "readonly", password: "browser"},
  topic_root: "site-a", agent_ids: ["edge-07"], agent_name: "Rack A"};
test("one runtime file controls dashboard and console endpoints and identities", () => {
  const runtime = parseRuntimeConfig(config);
  assert.deepEqual(connectionFields(runtime), {protocol: "wss", host: "monitor.example.com", port: 443, path: "/mqtt", username: "readonly", autoConnect: true});
  assert.ok(agentTopics(runtime.topicRoot, runtime.agentIds).every(topic => topic.startsWith("site-a/edge-07/")));
  assert.equal(topicInfo("site-a/edge-07/observation/sw1", {device: "sw1"}, runtime.topicRoot).id, "edge-07");
});
test("invalid or credential-bearing endpoints fail", () => {
  assert.throws(() => parseRuntimeConfig({...config, mqtt: {url: "ws://user:secret@broker/mqtt"}}));
  assert.throws(() => parseRuntimeConfig({...config, topic_root: "#"}));
  assert.throws(() => parseRuntimeConfig({...config, agent_ids: []}));
});
test("a missing production config does not silently connect to demo MQTT", async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = globalThis.location;
  globalThis.fetch = async () => ({status: 404, ok: false});
  globalThis.location = {hostname: "monitor.example.com"};
  try { await assert.rejects(loadRuntimeConfig(), /runtime-config.json/); }
  finally { globalThis.fetch = originalFetch; globalThis.location = originalLocation; }
});
test("a demo export cannot be served as a production deployment", async () => {
  const originalFetch = globalThis.fetch;
  const originalLocation = globalThis.location;
  globalThis.fetch = async () => ({ok: true, json: async () => ({...config, mode: "demo"})});
  globalThis.location = {hostname: "monitor.example.com"};
  try { await assert.rejects(loadRuntimeConfig(), /demo/); }
  finally { globalThis.fetch = originalFetch; globalThis.location = originalLocation; }
});
