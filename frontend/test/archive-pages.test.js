import test from "node:test";
import assert from "node:assert/strict";
import {readFile} from "node:fs/promises";
import {setImmediate} from "node:timers/promises";

class Element {
  constructor() {
    this.textContent = ""; this.value = ""; this.innerHTML = "";
    this.children = []; this.dataset = {}; this.style = {};
    this.handlers = {};
    this.classList = {add() {}, remove() {}};
  }
  addEventListener(name, handler) { this.handlers[name] = handler; }
  showModal() {}
  close() {}
  focus() {}
  reportValidity() { return true; }
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
      let intervalSeconds = 60, settingsAvailable = true;
      const settingsRequests = [];
      let profile = {name: 'ESP32', device_number: 'ESP-001', location: 'A', enabled: true};
      Object.assign(globalThis, {
        document: {getElementById: element, createElement: () => new Element(), createTextNode: text => ({textContent: text})},
        location: {hostname: "127.0.0.1", search: ""},
        window: {location: {hostname: "127.0.0.1", protocol: "http:"}},
        localStorage: {getItem: () => null, setItem() {}},
        setInterval() { return 0; },
        fetch: async (url, options = {}) => {
          if (url === "/api/devices/edge-1" && options.method === "PUT") {
            profile = {...profile, ...JSON.parse(options.body)};
            return {ok: true, json: async () => ({id: "edge-1", ...profile})};
          }
          if (url === "/api/devices/edge-1/reporting") {
            if (!settingsAvailable) return {ok: false, status: 503};
            if (options.method === "PUT") {
              settingsRequests.push({url, ...JSON.parse(options.body)});
              intervalSeconds = JSON.parse(options.body).interval_seconds;
            }
            return {ok: true, json: async () => ({agent_id: "edge-1", interval_ms: intervalSeconds * 1000, saved: true})};
          }
          return {ok: true, json: async () => {
          if (url === "/runtime-config.json") return {schema_version: 1, topic_root: "h3c", agent_ids: ["edge-1"],
            reporting: {agent_id: "edge-1", interval_ms: 60000},
            mqtt: {url: "ws://127.0.0.1:9001/mqtt"}, api: {url: "/api", token: "x".repeat(32)}};
          if (url === "/api/devices?scope=live") return {history_days: 30, devices: registered ? [{id: "edge-1", ...profile,
            report_count: reports.length, latest_report: reports.at(-1) || null},
            {id: "edge-2", name: "Second ESP32", enabled: true, report_count: 35, latest_report: null}] : []};
          return {reports: reports.map(report => ({agent_id: "edge-1", report}))};
        }};}
      });
      const file = new URL(page === "app" ? "../src/controllers/dashboard-controller.js" : `../src/${page}.js`, import.meta.url);
      let source = await readFile(file, "utf8");
      source = source.replace('import mqtt from "mqtt";', 'const mqtt = {connect: () => ({connected: true, on() {}, subscribe() {}})};')
        .replace('import "./styles.css";', "")
        .replace(/from "(\.{1,2}\/.+?\.js)"/g, (_, path) => `from ${JSON.stringify(new URL(path, file).href)}`);
      source += page === "app" ? '\nconst {syncRegistry, applyMessage} = startDashboard(); export {syncRegistry, applyMessage};' : '\nexport {restoreArchive, handleMessage};';
      const module = await import('data:text/javascript;base64,' + Buffer.from(source).toString('base64'));
      await setImmediate();
      const sync = page === "app" ? module.syncRegistry : module.restoreArchive;
      const ids = () => page === "app" ? element("eventTableBody").innerHTML
        : element("espLogs").children.map(line => line.dataset.reportId).join(" ");
      assert.match(ids(), /deleted-record/, page);
      if (page === "app") {
        const row = id => element("deviceTableBody").innerHTML.split(`data-device-id="${id}"`)[1].split("</tr>")[0];
        assert.match(row("edge-1"), /<strong class="mono">2<\/strong>/);
        assert.match(row("edge-2"), /<strong class="mono">35<\/strong>/);
        assert.equal(element("detailMessages").textContent, "2");
        assert.equal(element("detailNumber").textContent, "编号：ESP-001");
        const select = id => element("deviceTableBody").handlers.click({target: {closest: selector => selector.startsWith("tr[") ? {dataset: {deviceId: id}} : null}});
        select("edge-2");
        assert.equal(element("detailMessages").textContent, "35");
        assert.equal(element("reportingForm").hidden, true);
        select("edge-1");
        assert.equal(element("detailMessages").textContent, "2");
        assert.equal(element("reportingForm").hidden, false);
        element("reportingSeconds").value = "90";
        await element("reportingForm").handlers.submit({preventDefault() {}});
        assert.deepEqual(settingsRequests, [{url: "/api/devices/edge-1/reporting", interval_seconds: 90}]);
        assert.match(element("reportingResult").textContent, /已保存为 90 秒/);
        assert.equal(element("detailMessages").textContent, "2");
        settingsAvailable = false;
        element("reportingSeconds").value = "120";
        await element("reportingForm").handlers.submit({preventDefault() {}});
        assert.match(element("reportingResult").textContent, /暂不可用/);
        assert.equal(intervalSeconds, 90);
        assert.equal(element("saveReportingButton").disabled, false);
        settingsAvailable = true;
        element("editDeviceButton").handlers.click();
        assert.equal(element("deviceIdInput").disabled, true);
        assert.equal(element("deviceNumberInput").value, "ESP-001");
        element("deviceNumberInput").value = "ESP-009";
        element("deviceForm").handlers.submit({preventDefault() {}});
        await setImmediate();
        await sync();
        assert.equal(element("detailId").textContent, "edge-1");
        assert.equal(element("detailNumber").textContent, "编号：ESP-009");
        assert.equal(element("detailMessages").textContent, "2");
        assert.match(row("edge-1"), /ESP-009/);
      }
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
        assert.equal(element("deviceTableBody").innerHTML, "");
        assert.equal(element("detailContent").hidden, true);
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
      if (page === "app") assert.equal(element("detailMessages").textContent, "0");
      else assert.match(element("espStats").textContent, /暂无/);
    }
  } finally {
    for (const [name, descriptor] of original) {
      if (descriptor) Object.defineProperty(globalThis, name, descriptor);
      else delete globalThis[name];
    }
  }
});
