// Service: same-origin HTTP access; no DOM or device state.
import {parseReport} from "../reports.js";

export async function registryRequest(api, path, method = "GET", data) {
  const response = await fetch(api.url + path, {
    method, cache: "no-store", signal: AbortSignal.timeout(5_000),
    headers: {Authorization: `Bearer ${api.token}`, ...(data ? {"Content-Type": "application/json"} : {})},
    ...(data ? {body: JSON.stringify(data)} : {})
  });
  if (!response.ok) throw new Error(response.status === 401 ? "数据库 API 身份验证失败" : response.status === 409 ? "设备编号已被其他设备使用，请换一个编号" : response.status === 422 ? "设备参数无效，请检查输入内容" : "设备数据库暂不可用，请稍后重试");
  return response.json();
}

export {deviceProfile as deviceMetadata} from "../models/device-model.js";

export async function loadArchivedReports(api, agent = null, limit = 50, scope = "history") {
  const query = new URLSearchParams({limit: String(limit)});
  if (agent) query.set("agent", agent);
  query.set("scope", scope);
  const result = await registryRequest(api, `/reports?${query}`);
  return result.reports.map(item => ({...item, report: parseReport(JSON.stringify(item.report))}))
    .sort((a, b) => a.report.sent_at_ms - b.report.sent_at_ms || a.report.report_id.localeCompare(b.report.report_id));
}

export async function loadArchiveSnapshot(api, agent = null, limit = 50) {
  const [registry, history] = await Promise.all([
    registryRequest(api, "/devices?scope=live"), loadArchivedReports(api, agent, limit, "live")
  ]);
  const managed = new Set(registry.devices.map(device => device.id));
  return {registry, history: history.filter(item => managed.has(item.agent_id))};
}
