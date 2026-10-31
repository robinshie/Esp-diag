import "./styles.css";
import "./history.css";
import {loadRuntimeConfig} from "./runtime-config.js";
import {registryRequest} from "./registry.js";
import {parseReport, reportSummary, reportSeverity} from "./reports.js";

const $ = id => document.getElementById(id);
let api;
let query = null;
let cursors = [null];
let page = 0;
let next = null;
let loading = false;
function localInput(date) {
  return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 19);
}
function renderRows(items) {
  const body = $("historyRows");
  body.replaceChildren();
  for (const item of items) {
    const report = parseReport(JSON.stringify(item.report));
    const row = document.createElement("tr");
    for (const text of [new Date(report.sent_at_ms).toLocaleString("zh-CN", {hour12: false}), item.agent_id,
      {info: "信息", warning: "警告", critical: "严重"}[reportSeverity(report)]]) {
      const cell = document.createElement("td"); cell.textContent = text; row.append(cell);
    }
    const cell = document.createElement("td");
    const details = document.createElement("details");
    const summary = document.createElement("summary"); summary.textContent = reportSummary(report);
    const raw = document.createElement("pre");
    raw.textContent = `report_id=${report.report_id}\n` + report.lines.map(line => `${new Date(line.at_ms).toLocaleString("zh-CN", {hour12: false})} ${line.text}`).join("\n");
    details.append(summary, raw); cell.append(details); row.append(cell); body.append(row);
  }
}
async function loadPage() {
  if (loading || !api || !query) return;
  loading = true;
  $("historyPrevious").disabled = true; $("historyNext").disabled = true;
  $("historyState").textContent = "正在查询数据库…";
  try {
    const params = new URLSearchParams(query);
    if (cursors[page]) params.set("cursor", cursors[page]);
    const result = await registryRequest(api, `/reports?${params}`);
    renderRows(result.reports);
    next = result.next_cursor;
    $("historyPage").textContent = `第 ${page + 1} 页`;
    $("historyState").textContent = `数据库共 ${result.total.toLocaleString("zh-CN")} 条 · 本页 ${result.reports.length} 条 · ${result.history_days === 0 ? "历史长期保留" : `历史保留 ${result.history_days} 天`}`;
    $("historyPrevious").disabled = page === 0;
    $("historyNext").disabled = !next;
  } catch (error) {
    $("historyRows").replaceChildren();
    $("historyState").textContent = `查询失败：${error.message}`;
  } finally { loading = false; }
}
function search() {
  if (loading) return;
  const start = $("historyStart").value ? new Date($("historyStart").value).getTime() : null;
  const end = $("historyEnd").value ? new Date($("historyEnd").value).getTime() : Date.now();
  if ((start !== null && !Number.isFinite(start)) || !Number.isFinite(end) || (start !== null && start > end)) {
    $("historyState").textContent = "请检查时间范围，开始时间不能晚于结束时间。"; return;
  }
  query = new URLSearchParams({scope: "history", limit: "50", end_ms: String(end)});
  if (start !== null) query.set("start_ms", String(start));
  if ($("historyAgent").value.trim()) query.set("agent", $("historyAgent").value.trim());
  cursors = [null]; page = 0; next = null;
  loadPage();
}
$("historyForm").addEventListener("submit", event => { event.preventDefault(); search(); });
$("allHistory").addEventListener("click", () => {
  if (loading) return;
  $("historyStart").value = ""; $("historyEnd").value = ""; search();
});
$("historyNext").addEventListener("click", () => {
  if (!loading && next) { cursors[++page] = next; loadPage(); }
});
$("historyPrevious").addEventListener("click", () => {
  if (!loading && page > 0) { page--; loadPage(); }
});
try {
  const runtime = await loadRuntimeConfig();
  if (!runtime.api) throw new Error("未配置数据库 API，无法查询历史。");
  api = runtime.api;
  $("historyStart").value = localInput(new Date(Date.now() - 30 * 86400000));
  $("historyEnd").value = localInput(new Date());
  search();
} catch (error) { $("historyState").textContent = error.message; }
