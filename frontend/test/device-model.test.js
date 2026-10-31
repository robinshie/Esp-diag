import test from "node:test";
import assert from "node:assert/strict";
import {DeviceModel, deviceProfile} from "../src/models/device-model.js";
import {applyReport} from "../src/reports.js";
import {renderLoopView} from "../src/views/loop-view.js";
import {demoLoopReports, loopIncidents} from "../src/loops.js";

test("editable device number stays separate from identity, state and other devices", () => {
  const model = new DeviceModel({'edge-1': {name: '机架 A', device_number: 'ESP-001'}});
  const first = model.ensure('edge-1', 'agent', {api: {}, agentIds: []});
  const second = model.ensure('edge-2', 'agent', null);
  assert.equal(first.device_number, 'ESP-001');
  assert.equal(first.messages, null);
  assert.equal(second.messages, 0);
  applyReport(first, {status: null, observations: [], lines: []});
  assert.equal(first.id, 'edge-1');
  assert.equal(first.device_number, 'ESP-001');
  assert.equal(first.name, '机架 A');
  assert.equal(second.device_number, '');
  assert.deepEqual(deviceProfile({...first, password: 'secret'}), {name: '机架 A', location: '未设置', enabled: true, device_number: 'ESP-001'});
});

test("loop view renders supplied incident state without controller globals or network access", () => {
  const elements = Object.fromEntries(['loopTitle', 'loopAgent', 'loopConnection', 'loopDemoNotice', 'loopDemoButton', 'loopIncidentList', 'loopBody'].map(key => [key, {}]));
  const incidents = loopIncidents(demoLoopReports(Date.now()));
  const rendered = renderLoopView(elements, {agentId: 'edge-1', device: {name: 'ESP'}, online: true, demo: true, incidents});
  assert.ok(rendered.signature);
  assert.match(elements.loopConnection.textContent, /ESP 在线/);
  assert.ok(elements.loopBody.innerHTML.includes('处置时间线'));
  renderLoopView(elements, {agentId: 'edge-1', device: {name: 'ESP'}, online: false, demo: false, incidents: []});
  assert.match(elements.loopConnection.textContent, /ESP 离线/);
  assert.match(elements.loopBody.innerHTML, /尚未收到明确的环路事件/);
});
