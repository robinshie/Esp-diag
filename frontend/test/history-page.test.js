import test from "node:test";
import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {setImmediate} from "node:timers/promises";

class Element {
  constructor() { this.value = ""; this.textContent = ""; this.children = []; this.events = {}; }
  addEventListener(name, callback) { this.events[name] = callback; }
  append(...children) { this.children.push(...children); }
  replaceChildren(...children) { this.children = children; }
}

test("history page queries dates/devices and paginates independently of live data", async () => {
  const names = ["fetch", "document", "window", "location"];
  const original = new Map(names.map(name => [name, Object.getOwnPropertyDescriptor(globalThis, name)]));
  const elements = new Map();
  const element = id => {
    if (!elements.has(id)) elements.set(id, new Element());
    return elements.get(id);
  };
  const queries = [];
  const old = Date.now() - 45 * 86400000;
  const report = {schema_version: 1, report_id: "old-report", sent_at_ms: old, status: null,
    observations: [], lines: [{at_ms: old, text: "<script>untrusted USB line</script>"}]};
  try {
    Object.assign(globalThis, {
      document: {getElementById: element, createElement: () => new Element()},
      window: {location: {hostname: "127.0.0.1", protocol: "http:"}},
      location: {hostname: "127.0.0.1", search: ""},
      fetch: async url => ({ok: true, json: async () => {
        if (url === "/runtime-config.json") return {schema_version: 1, topic_root: "h3c", agent_ids: ["edge-1"],
          mqtt: {url: "ws://127.0.0.1:9001/mqtt"}, api: {url: "/api", token: "x".repeat(32)}};
        const params = new URL(url, "http://localhost").searchParams;
        queries.push(params);
        return {reports: [{agent_id: "edge-1", report}], total: 51, history_days: 0,
          next_cursor: params.has("cursor") ? null : "next-page"};
      }})
    });
    const file = new URL("../src/history.js", import.meta.url);
    let source = await readFile(file, "utf8");
    source = source.replace(/import "\.\/.+?\.css";/g, "")
      .replace(/from "(\.\/.+?\.js)"/g, (_, path) => `from ${JSON.stringify(new URL(path, file).href)}`);
    await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
    await setImmediate();
    assert.equal(queries[0].get("scope"), "history");
    assert.ok(Number(queries[0].get("start_ms")) > old, "initial range defaults to recent 30 days");
    assert.match(element("historyState").textContent, /长期保留/);
    element("historyAgent").value = "removed-edge";
    element("allHistory").events.click();
    await setImmediate();
    const all = queries.at(-1);
    assert.equal(all.has("start_ms"), false, "all-history query has no 30-day lower bound");
    assert.equal(all.get("agent"), "removed-edge");
    assert.equal(element("historyRows").children.length, 1);
    const raw = element("historyRows").children[0].children[3].children[0].children[1];
    assert.match(raw.textContent, /<script>untrusted USB line<\/script>/, "raw USB text is rendered as text");
    element("historyNext").events.click();
    await setImmediate();
    assert.equal(queries.at(-1).get("cursor"), "next-page");
    assert.equal(queries.at(-1).get("end_ms"), all.get("end_ms"), "pagination keeps its query time fixed");
    assert.equal(element("historyNext").disabled, true);
    element("historyPrevious").events.click();
    await setImmediate();
    assert.equal(queries.at(-1).has("cursor"), false);
    element("historyStart").value = "2026-10-31T12:00";
    element("historyEnd").value = "2026-10-01T12:00";
    const before = queries.length;
    element("historyForm").events.submit({preventDefault() {}});
    assert.equal(queries.length, before);
    assert.match(element("historyState").textContent, /开始时间不能晚于结束时间/);
  } finally {
    for (const [name, descriptor] of original) {
      if (descriptor) Object.defineProperty(globalThis, name, descriptor);
      else delete globalThis[name];
    }
  }
});
