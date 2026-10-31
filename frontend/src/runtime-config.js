const demo = () => ({
  configured: false, topicRoot: "h3c", agentIds: ["esp32-s3-eth"], agentName: "ESP32-S3 BDI Agent",
  serialLabel: "USB", mqttUrl: `ws://${location.hostname || "127.0.0.1"}:9001/mqtt`,
  username: "esp32", password: "esp32-bdi-test"
});

export function parseRuntimeConfig(config) {
  if (config.schema_version !== 1 || !config.mqtt || !Array.isArray(config.agent_ids) || !config.agent_ids.length) {
    throw new Error("运行时配置格式错误");
  }
  const url = new URL(config.mqtt.url);
  if (!["ws:", "wss:"].includes(url.protocol) || url.username || url.password) throw new Error("MQTT WebSocket 地址无效");
  if (![config.topic_root, ...config.agent_ids].every((id) => /^[A-Za-z0-9_.-]{1,64}$/.test(id))) throw new Error("MQTT 主题或 ESP32 ID 无效");
  if (config.api && (!/^\/[A-Za-z0-9/_-]+$/.test(config.api.url) || config.api.url.startsWith("//") ||
      typeof config.api.token !== "string" || config.api.token.length < 32)) throw new Error("设备数据库 API 配置无效");
  return {configured: true, topicRoot: config.topic_root, agentIds: config.agent_ids,
    agentName: config.agent_name || config.agent_ids[0], serialLabel: config.serial_label || "USB",
    mqttUrl: url.href, username: config.mqtt.username || "", password: config.mqtt.password || "", api: config.api || null,
    reporting: config.reporting ? {agentId: config.reporting.agent_id, intervalMs: config.reporting.interval_ms} : null};
}

export async function loadRuntimeConfig() {
  const response = await fetch("/runtime-config.json", {cache: "no-store"});
  const local = ["localhost", "127.0.0.1", "::1", "[::1]"].includes(location.hostname);
  if (response.status === 404 && local) return demo();
  if (!response.ok) throw new Error("缺少 runtime-config.json，请先导出部署配置");
  const config = await response.json();
  if (config.mode === "demo" && !local) throw new Error("生产地址不能使用 demo 配置，请导出 SSH 部署配置");
  return parseRuntimeConfig(config);
}

export function connectionFields(runtime) {
  const url = new URL(runtime.mqttUrl);
  return {protocol: url.protocol.slice(0, -1), host: url.hostname, port: Number(url.port || (url.protocol === "wss:" ? 443 : 80)),
    path: url.pathname + url.search, username: runtime.username, autoConnect: true};
}
