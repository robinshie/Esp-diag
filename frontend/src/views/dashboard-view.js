import {escapeHtml, statusBadge, formatAgo, formatUptime} from "./html.js";

// View: render supplied state. No requests, persistence or event orchestration.
export function createDashboardView(elements) {
  function renderMetrics(devices, isOnline) {
    const managed = [...devices.values()].filter((device) => device.enabled);
    elements.onlineMetric.textContent = managed.filter(isOnline).length;
    elements.totalDeviceMetric.textContent = `共 ${managed.length} 台`;
    elements.criticalMetric.textContent = managed.filter((device) => isOnline(device) && device.beliefs.server_critical === true).length;
    elements.lockedMetric.textContent = managed.filter((device) => device.locked).length;
  }

  function renderDevices(devices, selectedDeviceId, archived, isOnline) {
    const list = [...devices.values()].sort((a, b) => {
      // Keep rows in place while incoming messages update their status.
      return Number(a.kind === "agent") - Number(b.kind === "agent") || a.id.localeCompare(b.id);
    });
    elements.emptyDevices.hidden = list.length > 0;
    elements.deviceTableBody.innerHTML = list.map((device) => {
      const online = isOnline(device);
      const healthLabel = { healthy: "健康", warning: "注意", critical: "严重", unknown: "未知" }[online ? device.health : "unknown"];
      return `
        <tr data-device-id="${escapeHtml(device.id)}" class="${selectedDeviceId === device.id ? "is-selected" : ""}">
          <td><button class="device-number" data-action="edit" title="设置设备编号">${escapeHtml(device.device_number || "未设置")}</button></td>
          <td><button class="device-name device-loop-open" data-action="loops" aria-label="查看 ${escapeHtml(device.name)} 的环路处置">${escapeHtml(device.name)} ↗</button><span class="device-id">${escapeHtml(device.id)} · ${escapeHtml(device.location)}</span></td>
          <td>${device.enabled ? statusBadge(online ? "在线" : "离线", online ? "online" : "offline") : statusBadge("已停用", "disabled")}</td>
          <td>${statusBadge(healthLabel, online ? device.health : "unknown")}</td>
          <td class="device-report-count"><strong class="mono">${device.messages?.toLocaleString("zh-CN") ?? "—"}</strong><small>${archived ? "当前连接" : "本次会话"}</small></td>
          <td class="mono">${formatAgo(device.lastSeen)}</td>
          <td><div class="row-actions"><button class="icon-button" data-action="edit" aria-label="编辑 ${escapeHtml(device.name)}">✎</button></div></td>
        </tr>`;
    }).join("");
  }

  function renderDetail(device, isOnline) {
    elements.detailEmpty.hidden = Boolean(device);
    elements.detailContent.hidden = !device;
    if (!device) return;

    const online = isOnline(device);
    elements.detailKind.textContent = "BDI AGENT";
    elements.detailTitle.textContent = device.name;
    elements.detailNumber.textContent = device.device_number ? `编号：${device.device_number}` : "编号未设置";
    elements.detailId.textContent = device.id;
    elements.detailLocation.textContent = device.location;
    elements.detailLastSeen.textContent = formatAgo(device.lastSeen);
    elements.detailTransport.textContent = device.transport;
    elements.detailUptime.textContent = formatUptime(device.uptimeMs);
    elements.detailCpu.textContent = Number.isFinite(device.cpu) ? `${device.cpu}%` : "—";
    elements.detailTemp.textContent = Number.isFinite(device.temp) ? `${device.temp}°C` : "—";
    elements.detailMessages.textContent = device.messages?.toLocaleString("zh-CN") ?? "—";
    elements.detailFaults.textContent = device.faults.toLocaleString("zh-CN");
    elements.detailLastEvent.textContent = device.lastEvent;
    elements.detailBadges.innerHTML = [
      statusBadge(device.enabled ? (online ? "在线" : "离线") : "已停用", device.enabled ? (online ? "online" : "offline") : "disabled"),
      statusBadge({ healthy: "健康", warning: "注意", critical: "严重", unknown: "未知" }[online ? device.health : "unknown"], online ? device.health : "unknown"),
      ...(device.locked ? [statusBadge("监测到锁定", "locked")] : [])
    ].join("");
    elements.beliefList.innerHTML = Object.entries(device.beliefs).map(([name, value]) =>
      `<span class="belief ${value === true ? "is-true" : ""}">${escapeHtml(name)} = ${value === null ? "?" : value}</span>`
    ).join("");
  }

  function renderEvents(events, paused) {
    const severity = elements.severityFilter.value;
    const query = elements.eventSearch.value.trim().toLowerCase();
    const filtered = events.filter((event) => {
      const severityMatches = severity === "all" || event.severity === severity;
      const queryMatches = !query || `${event.topic} ${event.payload} ${event.raw || ""} ${event.deviceId} ${event.sourceId || ""}`.toLowerCase().includes(query);
      return severityMatches && queryMatches;
    });
    elements.emptyEvents.hidden = filtered.length > 0;
    if (paused) return;
    const opened = new Set([...elements.eventTableBody.querySelectorAll("details[open]")].map(detail => detail.dataset.time));
    elements.eventTableBody.innerHTML = filtered.slice(0, 100).map((event) => `
      <tr>
        <td>${new Date(event.timestamp).toLocaleTimeString("zh-CN", { hour12: false })}</td>
        <td>${statusBadge(event.severity === "critical" ? "严重" : event.severity === "warning" ? "警告" : "信息", event.severity)}</td>
        <td>${escapeHtml(event.deviceId)}${event.sourceId ? `<span class="device-id">监测对象 ${escapeHtml(event.sourceId)}</span>` : ""}</td>
        <td title="${escapeHtml(event.topic)}">${escapeHtml(event.topic)}</td>
        <td class="event-payload"><details data-time="${event.timestamp}" ${opened.has(String(event.timestamp)) ? "open" : ""}><summary title="${escapeHtml(event.payload)}">${escapeHtml(event.payload)}</summary><pre>${escapeHtml(event.raw)}</pre></details></td>
      </tr>`).join("");
  }


    return {renderMetrics, renderDevices, renderDetail, renderEvents};
}
