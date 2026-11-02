import "./styles.css";
import "./history.css";
import {loadRuntimeConfig} from "./runtime-config.js";
import {registryRequest} from "./registry.js";
import {parseReport, reportSummary, reportSeverity, archiveTime, reportTimeText} from "./reports.js";
import {renderReportAI} from "../shared/report-ai-view.js";

const $ = id => document.getElementById(id);
let api;
let query = null;
let cursors = [null];
let page = 0;
let next = null;
let loading = false;
let followLatest = true;
function localInput(date) {
  return new Date(date.getTime() - date.getTimezoneOffset() * 60000).toISOString().slice(0, 19);
}
function renderRows(items) {
  const body = $("historyRows");
  body.replaceChildren();
  for (const item of items) {
    const report = parseReport(JSON.stringify(item.report));
    const row = document.createElement("tr");
    const archivedAt = archiveTime(item);
    for (const text of [archivedAt === null ? "未提供入库时间" : new Date(archivedAt).toLocaleString("zh-CN", {hour12: false}), item.agent_id,
      {info: "信息", warning: "警告", critical: "严重"}[reportSeverity(report)]]) {
      const cell = document.createElement("td"); cell.textContent = text; row.append(cell);
    }
    const cell = document.createElement("td");
    const details = document.createElement("details");
    const summary = document.createElement("summary"); summary.textContent = reportSummary(report);
    const raw = document.createElement("pre");
    raw.textContent = `report_id=${report.report_id}\n` + report.lines.map(line => `${reportTimeText(report, line.at_ms)} ${line.text}`).join("\n");
    details.append(summary, raw); cell.append(details); row.append(cell); body.append(row);
    const assistant = document.createElement('details');
    const assistantTitle = document.createElement('summary'); assistantTitle.textContent = 'AI 解释本包日志';
    const content = document.createElement('div');
    assistant.append(assistantTitle); cell.append(assistant);
    const analysisRow = document.createElement('tr'); analysisRow.className = 'history-analysis-row'; analysisRow.hidden = true;
    const analysisCell = document.createElement('td'); analysisCell.colSpan = 4; analysisCell.append(content); analysisRow.append(analysisCell); body.append(analysisRow);
    let rendered = false;
    assistant.addEventListener('toggle', async () => {
      analysisRow.hidden = !assistant.open;
      if (!assistant.open || rendered) return;
      rendered = true;
      content.textContent = '正在关联本包之前的故障与处置证据…';
      let contextReports = [report], contextError = '', contextLimited = false;
      try {
        const context = await registryRequest(api, `/reports/${encodeURIComponent(report.report_id)}/context?${new URLSearchParams({agent: item.agent_id})}`);
        if (context.source !== 'database' || context.report_id !== report.report_id || !Array.isArray(context.reports) || context.reports.length > 12 ||
            JSON.stringify(context.reports.find(value => value.report_id === report.report_id)) !== JSON.stringify(report)) throw Error('数据库原文已变化，请重新查询');
        contextReports = context.reports.map(value => parseReport(JSON.stringify(value)));contextLimited = context.limited === true;
      } catch (error) { contextError = error.message; }
      content.textContent = '';
      renderReportAI(report, content, async (_, line, sourceReportId) => {
        const source = contextReports.find(value => value.report_id === sourceReportId);
        if (!source?.lines[line]) throw Error('原文索引不在已加载证据中');
        const reply = await registryRequest(api,
          `/reports/${encodeURIComponent(sourceReportId)}/explain?${new URLSearchParams({agent: item.agent_id, line: String(line)})}`);
        if (reply.source !== 'database' || reply.report_id !== sourceReportId || reply.line !== line || reply.evidence_text !== source.lines[line].text) {
          throw new Error('数据库记录已变化，请刷新后重新解释');
        }
        return reply;
      }, {contextReports, contextError, contextLimited});
    });
  }
}
async function loadPage(refreshLatest = false) {
  if (loading || !api || !query) return;
  if (refreshLatest && page === 0 && followLatest) {
    query.set("end_ms", String(Date.now()));
    cursors = [null]; next = null;
  }
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
    const cutoff = new Date(Number(query.get("end_ms"))).toLocaleString("zh-CN", {hour12: false});
    const refresh = followLatest ? (page === 0 ? "第一页每60秒刷新" : "翻页期间暂停自动刷新") : "固定时间范围";
    $("historyState").textContent = `所选范围共 ${result.total.toLocaleString("zh-CN")} 包上报 · 本页 ${result.reports.length} 包 · 截至 ${cutoff} · ${refresh} · ${result.history_days === 0 ? "历史长期保留" : `历史保留 ${result.history_days} 天`}`;
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
  const latest = $("historyFollowLatest").checked;
  const end = !latest && $("historyEnd").value ? new Date($("historyEnd").value).getTime() : Date.now();
  if ((start !== null && !Number.isFinite(start)) || !Number.isFinite(end) || (start !== null && start > end)) {
    $("historyState").textContent = "请检查时间范围，开始时间不能晚于结束时间。"; return;
  }
  query = new URLSearchParams({scope: "history", limit: "50", end_ms: String(end)});
  followLatest = latest;
  if (start !== null) query.set("start_ms", String(start));
  if ($("historyAgent").value.trim()) query.set("agent", $("historyAgent").value.trim());
  cursors = [null]; page = 0; next = null;
  loadPage();
}
$("historyForm").addEventListener("submit", event => { event.preventDefault(); search(); });
$("historyFollowLatest").addEventListener("change", () => {
  const latest = $("historyFollowLatest").checked;
  $("historyEnd").disabled = latest;
  $("historyEnd").value = latest ? "" : localInput(new Date());
  search();
});
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
  $("historyFollowLatest").checked = true;
  $("historyEnd").disabled = true;
  $("historyStart").value = localInput(new Date(Date.now() - 30 * 86400000));
  $("historyEnd").value = "";
  search();
  setInterval(() => {
    if (followLatest && page === 0 && !loading) {
      if ($('historyRows').querySelector?.('details[open]')) {
        if (!$('historyState').textContent.endsWith('查看内容期间暂停刷新')) $('historyState').textContent += ' · 查看内容期间暂停刷新';
        return;
      }
      loadPage(true);
    }
  }, 60_000);
} catch (error) { $("historyState").textContent = error.message; }
