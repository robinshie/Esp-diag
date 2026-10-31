import test from "node:test";
import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {setImmediate} from "node:timers/promises";

class Element {
  constructor() {
    this.textContent = ""; this.value = ""; this.innerHTML = "";
    this.children = []; this.dataset = {}; this.style = {};
    this.classList = {add() {}, remove() {}};
  }
  addEventListener() {}
  querySelectorAll() { return []; }
  append(...children) { for (const child of children) this.insertBefore(child, null); }
  insertBefore(child, next) {
    child.parent = this;
    const index = next ? this.children.indexOf(next) : -1;
    this.children.splice(index < 0 ? this.children.length : index, 0, child);
  }
  remove() { this.parent.children.splice(this.parent.children.indexOf(this), 1); }
  get childElementCount() { return this.children.length; }
  get firstElementChild() { return this.children[0]; }
}

test("both running pages remove externally deleted reports without reload or MQTT resurrection", async () => {
  const names = ["fetch", "document", "window", "location", "localStorage", "setInterval"];
  const original = new Map(names.map(name => [name, Object.getOwnPropertyDescriptor(globalThis, name)]));
  try {
    for (const page of ["app", "console"]) {
      const elements = new Map();
      const element = id => {
        if (!elements.has(id)) elements.set(id, new Element());
        return elements.get(id);
      };
      element("severityFilter").value = "all";
      const now = Date.now();
      const makeReport = (id, at) => ({schema_version: 1, report_id: id, sent_at_ms: at,
        status: {reasoning_cycle: "ACTIVE", transport: "USB", seen_at_ms: String(at), uptime_ms: "1000"},
        observations: [], lines: [{text: id, at_ms: at}]});
      const kept = makeReport("kept-record", now - 1000);
      const removed = makeReport("deleted-record", now);
      let reports = [kept, removed];
      let registered = true;
      Object.assign(globalThis, {
        document: {getElementById: element, createElement: () => new Element(), createTextNode: text => ({textContent: text})},
        location: {hostname: "127.0.0.1", search: ""},
        window: {location: {hostname: "127.0.0.1", protocol: "http:"}},
        localStorage: {getItem: () => null, setItem() {}},
        setInterval() { return 0; },
        fetch: async url => ({ok: true, json: async () => {
          if (url === "/runtime-config.json") return {schema_version: 1, topic_root: "h3c", agent_ids: ["edge-1"],
            mqtt: {url: "ws://127.0.0.1:9001/mqtt"}, api: {url: "/api", token: "x".repeat(32)}};
          if (url === "/api/devices?scope=live") return {history_days: 30, devices: registered ? [{id: "edge-1", name: "ESP32", enabled: true,
            report_count: reports.length, latest_report: reports.at(-1) || null}] : []};
          return {reports: reports.map(report => ({agent_id: "edge-1", report}))};
        }})
      });
      const file = new URL(`../src/${page}.js`, import.meta.url);
      let source = await readFile(file, "utf8");
      source = source.replace('import mqtt from "mqtt";', 'const mqtt = {connect: () => ({connected: true, on() {}, subscribe() {}})};')
        .replace('import "./styles.css";', "")
        .replace(/from "(\.\/.+?\.js)"/g, (_, path) => `from ${JSON.stringify(new URL(path, file).href)}`);
      source += page === "app" ? '\nexport {syncRegistry, applyMessage};' : '\nexport {restoreArchive, handleMessage};';
      const module = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
      await setImmediate();
      const sync = page === "app" ? module.syncRegistry : module.restoreArchive;
      const ids = () => page === "app" ? element("eventTableBody").innerHTML
        : element("espLogs").children.map(line => line.dataset.reportId).join(" ");
      assert.match(ids(), /deleted-record/, page);
      reports = [kept];
      await sync();
      assert.doesNotMatch(ids(), /deleted-record/, page);
      assert.match(ids(), /kept-record/, page);
      // A retained or live MQTT report must not bypass the database after deletion.
      if (page === "app") module.applyMessage("h3c/edge-1/report", JSON.stringify(removed));
      else module.handleMessage("h3c/edge-1/report", Buffer.from(JSON.stringify(removed)));
      assert.doesNotMatch(ids(), /deleted-record/, page);
      registered = false;
      await sync();
      assert.equal(ids(), "", page);
      if (page === "app") {
        assert.equal(element("messageMetric").textContent, "0");
        assert.equal(element("onlineMetric").textContent, 0);
        assert.equal(element("lockedMetric").textContent, 0);
      }
      // The archive still exists; explicit manual registration restores it.
      registered = true;
      await sync();
      assert.match(ids(), /kept-record/, page);
      reports = [];
      await sync();
      assert.equal(ids(), "", page);
      if (page === "app") assert.equal(element("messageMetric").textContent, "0");
      else assert.match(element("espStats").textContent, /暂无/);
    }
  } finally {
    for (const [name, descriptor] of original) {
      if (descriptor) Object.defineProperty(globalThis, name, descriptor);
      else delete globalThis[name];
    }
  }
});
