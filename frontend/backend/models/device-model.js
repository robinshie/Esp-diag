// Model: device identity/profile and state defaults. No DOM, storage or HTTP.
export function deviceProfile(value) {
  return {name: value.name, location: value.location, enabled: value.enabled, device_number: value.device_number || ""};
}

export class DeviceModel {
  constructor(metadata = {}) {
    this.metadata = metadata;
    this.devices = new Map();
  }

  create(id, kind = "manual", runtime = null) {
    const meta = this.metadata[id] || {};
    return {
      id, kind,
      device_number: meta.device_number || "",
      name: meta.name || (runtime?.agentIds[0] === id ? runtime.agentName : id === "esp32-s3-eth" ? "ESP32-S3 BDI Agent" : `ESP32 · ${id}`),
      location: meta.location || "未设置", enabled: meta.enabled ?? true,
      manuallyAdded: meta.manuallyAdded ?? false,
      lastSeen: null, availability: null, health: "unknown", locked: false,
      transport: "—", uptimeMs: null, cpu: null, temp: null,
      messages: runtime?.api ? null : 0, faults: 0, lastEvent: "尚无数据", lastTopic: "—",
      beliefs: {server_healthy: null, server_critical: null, server_locked: null, switch_link_fault: null}
    };
  }

  ensure(id, kind, runtime) {
    if (!this.devices.has(id)) this.devices.set(id, this.create(id, kind, runtime));
    const device = this.devices.get(id);
    if (device.kind === "manual" && kind !== "manual") device.kind = kind;
    return device;
  }
}
