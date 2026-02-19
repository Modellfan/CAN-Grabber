const state = {
  config: null,
  token: "",
  wifiScan: [],
  selectedFiles: new Set(),
  activeFiles: new Set(),
  downloadInProgress: false
};
let statusRequestInFlight = false;

const views = ["status", "can", "config", "files"];
const apiBase = "";

function showToast(message, isError) {
  const toast = document.getElementById("toast");
  toast.textContent = message;
  toast.style.background = isError ? "#b8342a" : "#1f1f1f";
  toast.style.display = "block";
  setTimeout(() => {
    toast.style.display = "none";
  }, 2500);
}

function setView(name) {
  views.forEach((view) => {
    const section = document.getElementById(view);
    const link = document.querySelector(`.tablink[data-view="${view}"]`);
    if (!section || !link) {
      return;
    }
    const active = view === name;
    const display = section.dataset.display || "block";
    section.style.display = active ? display : "none";
    if (active) {
      link.classList.add("active");
    } else {
      link.classList.remove("active");
    }
  });
}

function formatBytes(bytes) {
  if (bytes === null || bytes === undefined) {
    return "-";
  }
  const units = ["B", "KB", "MB", "GB"];
  let value = Number(bytes);
  let idx = 0;
  while (value >= 1024 && idx < units.length - 1) {
    value /= 1024;
    idx += 1;
  }
  return `${value.toFixed(1)} ${units[idx]}`;
}

function formatUptime(seconds) {
  if (!seconds) {
    return "-";
  }
  const days = Math.floor(seconds / 86400);
  const hours = Math.floor((seconds % 86400) / 3600);
  const mins = Math.floor((seconds % 3600) / 60);
  return `${days}d ${hours}h ${mins}m`;
}

function formatFlags(flags) {
  const entries = [];
  if (flags & 1) entries.push("downloaded");
  if (flags & 2) entries.push("uploaded");
  if (flags & 4) entries.push("active");
  return entries.length ? entries.join(", ") : "-";
}

function formatCount(value) {
  if (value === null || value === undefined) {
    return "-";
  }
  return Number(value).toLocaleString();
}

function formatBitrate(value) {
  if (!value) {
    return "-";
  }
  return `${Number(value).toLocaleString()} bit/s`;
}

function formatPercent(value) {
  if (value === null || value === undefined) {
    return "-";
  }
  return `${Number(value).toFixed(1)}%`;
}

function formatHexByte(value) {
  if (value === null || value === undefined) {
    return "0x00";
  }
  const n = Number(value) & 0xff;
  return `0x${n.toString(16).toUpperCase().padStart(2, "0")}`;
}

function decodeEflgFlags(value) {
  const eflg = Number(value || 0) & 0xff;
  const flags = [];
  if (eflg & 0x01) flags.push("EWARN");
  if (eflg & 0x02) flags.push("RXWAR");
  if (eflg & 0x04) flags.push("TXWAR");
  if (eflg & 0x08) flags.push("RXEP");
  if (eflg & 0x10) flags.push("TXEP");
  if (eflg & 0x20) flags.push("TXBO");
  if (eflg & 0x40) flags.push("RX0OVR");
  if (eflg & 0x80) flags.push("RX1OVR");
  return flags.length ? flags.join(", ") : "none";
}

function mcp2515StatusText(eflgValue, recValue, tecValue) {
  const eflg = Number(eflgValue || 0) & 0xff;
  const rec = Number(recValue || 0);
  const tec = Number(tecValue || 0);
  const faults = [];

  if (eflg & 0x20) faults.push("bus off");
  if (eflg & 0x10) faults.push("tx error-passive");
  if (eflg & 0x08) faults.push("rx error-passive");
  if (eflg & 0x04) faults.push("tx warning");
  if (eflg & 0x02) faults.push("rx warning");
  if (eflg & 0x01) faults.push("error warning");
  if (eflg & 0x40) faults.push("RX0 overflow");
  if (eflg & 0x80) faults.push("RX1 overflow");

  if (!faults.length) {
    return `MCP2515: OK (REC ${rec}, TEC ${tec})`;
  }
  return `MCP2515: ${faults.join(", ")} (REC ${rec}, TEC ${tec})`;
}

function toYesNo(value) {
  return value ? "yes" : "no";
}

function isDefaultBusName(busId, name) {
  if (!name) {
    return true;
  }
  return String(name).toLowerCase() === `can${busId}`;
}

function renderCanStatusCards(canEntries) {
  const container = document.getElementById("can-stats-cards");
  if (!container) {
    return;
  }
  container.innerHTML = "";
  const byId = new Map((canEntries || []).map((entry) => [Number(entry.id ?? entry.bus), entry]));

  for (let busId = 0; busId < 6; busId += 1) {
    const entry = byId.get(busId) || { id: busId, bus: busId };
    const enabled = !!entry.enabled;
    const baseName = `CAN ${busId}`;
    const nameSuffix = !isDefaultBusName(busId, entry.name) ? `: ${entry.name}` : "";
    const card = document.createElement("div");
    card.className = `dash-box can-stat-card${enabled ? "" : " disabled"}`;
    card.innerHTML = `
      <h3>${baseName}${nameSuffix}</h3>
      <p>Drops: <strong>${formatCount(entry.drops)} msg</strong></p>
      <p>Total received: <strong>${formatCount(entry.total_received)} msg</strong></p>
      <p>Total sent: <strong>${formatCount(entry.total_sent)} msg</strong></p>
      <p>High Water: <strong>${formatCount(entry.high_water)} / ${formatCount(entry.queue_capacity)} bytes (${formatPercent(entry.high_water_pct)})</strong></p>
      <p>Bus load: <strong>${formatPercent(entry.queue_load_pct)}</strong></p>
      <p>Enabled: <strong>${toYesNo(entry.enabled)}</strong></p>
      <p>Logging: <strong>${toYesNo(entry.logging)}</strong></p>
      <p>Read only: <strong>${toYesNo(entry.read_only)}</strong></p>
      <p>Bitrate: <strong>${formatBitrate(entry.bitrate)}</strong></p>
      <p>RX task: <strong>${entry.rx_task_running ? "running" : "stopped"}</strong></p>
      <p>MCP2515: <strong>${mcp2515StatusText(entry.eflg, entry.rec, entry.tec).replace("MCP2515: ", "")}</strong></p>
      <p>Bus off: <strong>${toYesNo(entry.bus_off)}</strong></p>
    `;
    container.appendChild(card);
  }
}

function apiHeaders() {
  const headers = {
    "Content-Type": "application/json"
  };
  if (state.token) {
    headers["X-Api-Token"] = state.token;
  }
  return headers;
}

async function apiGet(path) {
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 4000);
  const res = await fetch(`${apiBase}${path}`, {
    headers: apiHeaders(),
    signal: controller.signal
  }).finally(() => clearTimeout(timeout));
  if (!res.ok) {
    throw new Error(`Request failed: ${res.status}`);
  }
  return res.json();
}

async function apiPost(path, payload) {
  const res = await fetch(`${apiBase}${path}`, {
    method: "POST",
    headers: apiHeaders(),
    body: JSON.stringify(payload || {})
  });
  if (!res.ok) {
    throw new Error(`Request failed: ${res.status}`);
  }
  return res.json();
}

async function apiPut(path, payload) {
  const res = await fetch(`${apiBase}${path}`, {
    method: "PUT",
    headers: apiHeaders(),
    body: JSON.stringify(payload || {})
  });
  if (!res.ok) {
    throw new Error(`Request failed: ${res.status}`);
  }
  return res.json();
}

function renderBusConfig(cfg) {
  const container = document.getElementById("bus-configs");
  container.innerHTML = "";
  cfg.buses.forEach((bus) => {
    const wrapper = document.createElement("div");
    wrapper.className = "bus-card";
    wrapper.innerHTML = `
      <strong>Bus ${bus.id}</strong>
      <div class="form-grid">
        <label>Enabled</label>
        <input id="bus-enabled-${bus.id}" type="checkbox" class="cfg-checkbox">
        <label>Bitrate</label>
        <select id="bus-bitrate-${bus.id}">
          <option value="125000">125 kbit/s</option>
          <option value="250000">250 kbit/s</option>
          <option value="500000">500 kbit/s</option>
          <option value="1000000">1 Mbit/s</option>
        </select>
        <label>Read only</label>
        <input id="bus-readonly-${bus.id}" type="checkbox" class="cfg-checkbox">
        <label>Logging</label>
        <input id="bus-logging-${bus.id}" type="checkbox" class="cfg-checkbox">
        <label>Name</label>
        <input id="bus-name-${bus.id}" type="text" maxlength="16">
      </div>
    `;
    container.appendChild(wrapper);
    document.getElementById(`bus-enabled-${bus.id}`).checked = !!bus.enabled;
    document.getElementById(`bus-bitrate-${bus.id}`).value = String(bus.bitrate);
    document.getElementById(`bus-readonly-${bus.id}`).checked = !!bus.read_only;
    document.getElementById(`bus-logging-${bus.id}`).checked = !!bus.logging;
    document.getElementById(`bus-name-${bus.id}`).value = bus.name || "";
  });
}

function renderWifiConfig(cfg) {
  const container = document.getElementById("wifi-configs");
  container.innerHTML = "";
  for (let i = 0; i < 3; i += 1) {
    const entry = cfg.global.wifi[i] || { ssid: "", password: "" };
    const wrapper = document.createElement("div");
    wrapper.className = "wifi-card";
    wrapper.innerHTML = `
      <strong>Network ${i + 1}</strong>
      <div class="form-grid">
        <label>SSID</label>
        <input id="wifi-ssid-${i}" type="text" maxlength="32" list="wifi-scan-${i}">
        <datalist id="wifi-scan-${i}">
          <option value="">Pick a network...</option>
        </datalist>
        <label>Password</label>
        <input id="wifi-pass-${i}" type="password" maxlength="64">
      </div>
    `;
    container.appendChild(wrapper);
    document.getElementById(`wifi-ssid-${i}`).value = entry.ssid || "";
    document.getElementById(`wifi-pass-${i}`).value = entry.password || "";
  }
  renderWifiScanOptions();
}

function renderWifiScanOptions() {
  for (let i = 0; i < 3; i += 1) {
    const list = document.getElementById(`wifi-scan-${i}`);
    if (!list) {
      continue;
    }
    list.innerHTML = `<option value="">Pick a network...</option>`;
    state.wifiScan.forEach((net) => {
      const label = `${net.ssid} (${net.rssi_percent}%)`;
      list.appendChild(new Option(label, net.ssid));
    });
  }
}

async function refreshWifiScan() {
  if (state.downloadInProgress) {
    return;
  }
  try {
    const data = await apiGet("/api/wifi/scan");
    state.wifiScan = data
      .filter((net) => net.ssid)
      .sort((a, b) => b.rssi_percent - a.rssi_percent);
    renderWifiScanOptions();
  } catch (err) {
    showToast(err.message, true);
  }
}

function collectConfig() {
  const cfg = state.config;
  if (!cfg) {
    return null;
  }

  const buses = cfg.buses.map((bus) => {
    return {
      id: bus.id,
      enabled: document.getElementById(`bus-enabled-${bus.id}`).checked,
      bitrate: Number(document.getElementById(`bus-bitrate-${bus.id}`).value),
      read_only: document.getElementById(`bus-readonly-${bus.id}`).checked,
      logging: document.getElementById(`bus-logging-${bus.id}`).checked,
      name: document.getElementById(`bus-name-${bus.id}`).value.trim()
    };
  });

  const wifi = [];
  let wifiCount = 0;
  for (let i = 0; i < 3; i += 1) {
    const ssid = document.getElementById(`wifi-ssid-${i}`).value.trim();
    const password = document.getElementById(`wifi-pass-${i}`).value;
    if (ssid) {
      wifiCount = i + 1;
    }
    wifi.push({ ssid, password });
  }

  const maxSizeMb = Number(document.getElementById("log-max-size").value || 0);
  const lowSpaceMb = Number(document.getElementById("log-low-space").value || 0);

  return {
    global: {
      max_file_size_bytes: Math.max(1, maxSizeMb) * 1024 * 1024,
      low_space_threshold_bytes: Math.max(0, lowSpaceMb) * 1024 * 1024,
      wifi_count: wifiCount,
      wifi_sta_enabled: document.getElementById("wifi-sta-enabled").checked,
      wifi,
      upload_url: document.getElementById("upload-url").value.trim(),
      influx_url: cfg.global.influx_url,
      influx_token: cfg.global.influx_token,
      api_token: cfg.global.api_token,
      dbc_name: cfg.global.dbc_name,
      can_time_sync: document.getElementById("can-time-sync").checked
    },
    buses
  };
}

async function loadStatus() {
  if (state.downloadInProgress) {
    return;
  }
  if (statusRequestInFlight) {
    return;
  }
  statusRequestInFlight = true;
  try {
    const data = await apiGet("/api/status");
    document.getElementById("uptime").textContent = formatUptime(data.uptime_sec);
    document.getElementById("wifi-ssid").textContent = data.ssid || "-";
    document.getElementById("wifi-ip").textContent = data.ip || "-";
    document.getElementById("wifi-rssi").textContent = `${data.rssi_percent}%`;
    document.getElementById("wifi-sta-mode").textContent = data.sta_mode_enabled ? "on" : "off";
    document.getElementById("sd-ready").textContent = data.storage.ready ? "yes" : "no";
    document.getElementById("sd-total").textContent = formatBytes(data.storage.total_bytes);
    document.getElementById("sd-free").textContent = formatBytes(data.storage.free_bytes);
    document.getElementById("sd-log-files").textContent = formatCount(data.storage.log_files);
    document.getElementById("log-rate").textContent = formatBytes(data.logging.bytes_per_sec) + "/s";
    document.getElementById("log-total").textContent = formatBytes(data.logging.total_bytes);
    document.getElementById("log-buses").textContent = data.logging.active_buses;
    document.getElementById("logging-state").textContent = data.logging.started ? "running" : "stopped";
    if (data.time_epoch) {
      const dt = new Date(data.time_epoch * 1000);
      document.getElementById("time-now").textContent = dt.toISOString().replace("T", " ").slice(0, 19);
    } else {
      document.getElementById("time-now").textContent = "-";
    }

    renderCanStatusCards(data.can);
  } catch (err) {
    if (err && err.name === "AbortError") {
      return;
    }
    showToast(err.message, true);
  } finally {
    statusRequestInFlight = false;
  }
}

async function loadConfig() {
  try {
    const data = await apiGet("/api/config");
    state.config = data;
    renderBusConfig(data);
    renderWifiConfig(data);
    refreshWifiScan();
    document.getElementById("log-max-size").value = Math.round(data.global.max_file_size_bytes / (1024 * 1024));
    document.getElementById("log-low-space").value = Math.round(data.global.low_space_threshold_bytes / (1024 * 1024));
    document.getElementById("upload-url").value = data.global.upload_url || "";
    document.getElementById("can-time-sync").checked = !!data.global.can_time_sync;
    document.getElementById("wifi-sta-enabled").checked = !!data.global.wifi_sta_enabled;
  } catch (err) {
    showToast(err.message, true);
  }
}

async function loadFiles() {
  if (state.downloadInProgress) {
    return;
  }
  try {
    const data = await apiGet("/api/files");
    state.activeFiles = new Set(
      data.filter((entry) => (entry.flags & 4) !== 0).map((entry) => String(entry.id))
    );
    state.activeFiles.forEach((id) => state.selectedFiles.delete(id));

    const filter = document.getElementById("file-filter");
    const selected = filter.value;
    filter.innerHTML = "";
    filter.appendChild(new Option("All", "all"));
    for (let i = 0; i < 6; i += 1) {
      filter.appendChild(new Option(`Bus ${i}`, String(i)));
    }
    if (selected) {
      filter.value = selected;
    }

    const body = document.getElementById("files-body");
    body.innerHTML = "";
    data.forEach((entry) => {
      if (filter.value !== "all" && String(entry.bus_id) !== filter.value) {
        return;
      }
      const row = document.createElement("tr");
      const name = entry.path.split("/").pop();
      const isActive = (entry.flags & 4) !== 0;
      const checked = state.selectedFiles.has(String(entry.id)) ? "checked" : "";
      const selectDisabled = isActive ? "disabled" : "";
      const actionHtml = isActive
        ? `<button class="butt small file-action-btn rotate-file" data-bus="${entry.bus_id}">Rotate file</button>`
        : `
          <a class="butt small file-action-btn" href="${apiBase}/api/files/${entry.id}/download" target="_blank" rel="noopener">Download</a>
        `;
      row.innerHTML = `
        <td><input type="checkbox" class="file-select" data-id="${entry.id}" ${checked} ${selectDisabled}></td>
        <td>${entry.id}</td>
        <td>${entry.bus_id}</td>
        <td>${name}</td>
        <td>${formatBytes(entry.size_bytes)}</td>
        <td>${entry.start_ms}</td>
        <td>${entry.end_ms}</td>
        <td>${formatFlags(entry.flags)}</td>
        <td>${actionHtml}</td>
      `;
      body.appendChild(row);
    });

    document.querySelectorAll(".file-select").forEach((checkbox) => {
      checkbox.addEventListener("change", () => {
        if (checkbox.disabled) {
          return;
        }
        const id = checkbox.dataset.id;
        if (checkbox.checked) {
          state.selectedFiles.add(id);
        } else {
          state.selectedFiles.delete(id);
        }
      });
    });

    document.querySelectorAll(".rotate-file").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          await apiPost("/api/control/close_active_file", { bus_id: Number(btn.dataset.bus) });
          showToast("Rotated active file");
          loadStatus();
          loadFiles();
        } catch (err) {
          showToast(err.message, true);
        }
      });
    });
  } catch (err) {
    showToast(err.message, true);
  }
}

function selectedFileIds() {
  return Array.from(state.selectedFiles);
}

function clearFileSelection() {
  state.selectedFiles.clear();
  const selectAll = document.getElementById("select-all-files");
  if (selectAll) {
    selectAll.checked = false;
  }
}

function wireEvents() {
  document.querySelectorAll(".tablink").forEach((link) => {
    link.addEventListener("click", (event) => {
      const view = event.currentTarget.dataset.view;
      setView(view);
      if (view === "files") {
        loadFiles();
      }
    });
  });

  const refreshStatusButton = document.getElementById("refresh-status");
  if (refreshStatusButton) {
    refreshStatusButton.addEventListener("click", loadStatus);
  }
  const refreshConfigButton = document.getElementById("refresh-config");
  if (refreshConfigButton) {
    refreshConfigButton.addEventListener("click", loadConfig);
  }
  const refreshCanButton = document.getElementById("refresh-can");
  if (refreshCanButton) {
    refreshCanButton.addEventListener("click", loadConfig);
  }
  const refreshFilesButton = document.getElementById("refresh-files");
  if (refreshFilesButton) {
    refreshFilesButton.addEventListener("click", loadFiles);
  }

  document.getElementById("save-config").addEventListener("click", async () => {
    const payload = collectConfig();
    if (!payload) {
      return;
    }
    try {
      await apiPut("/api/config", payload);
      showToast("Config saved");
      await Promise.all([loadConfig(), loadStatus()]);
    } catch (err) {
      showToast(err.message, true);
    }
  });
  document.getElementById("save-can").addEventListener("click", async () => {
    const payload = collectConfig();
    if (!payload) {
      return;
    }
    try {
      await apiPut("/api/config", payload);
      showToast("Config saved");
      loadConfig();
    } catch (err) {
      showToast(err.message, true);
    }
  });

  const startLoggingButton = document.getElementById("start-logging");
  if (startLoggingButton) {
    startLoggingButton.addEventListener("click", async () => {
      try {
        await apiPost("/api/control/start_logging", {});
        showToast("Logging started");
        loadStatus();
      } catch (err) {
        showToast(err.message, true);
      }
    });
  }

  const stopLoggingButton = document.getElementById("stop-logging");
  if (stopLoggingButton) {
    stopLoggingButton.addEventListener("click", async () => {
      try {
        await apiPost("/api/control/stop_logging", {});
        showToast("Logging stopped");
        loadStatus();
      } catch (err) {
        showToast(err.message, true);
      }
    });
  }

  const closeFileButton = document.getElementById("close-file");
  if (closeFileButton) {
    closeFileButton.addEventListener("click", async () => {
      try {
        await apiPost("/api/control/close_active_file", {});
        showToast("File closed");
      } catch (err) {
        showToast(err.message, true);
      }
    });
  }

  document.getElementById("set-time").addEventListener("click", async () => {
    const value = document.getElementById("time-set").value;
    if (!value) {
      showToast("Pick a time first", true);
      return;
    }
    const epoch = Math.floor(new Date(value).getTime() / 1000);
    if (!epoch) {
      showToast("Invalid time", true);
      return;
    }
    try {
      await apiPost("/api/time", { epoch });
      showToast("Time updated");
      loadStatus();
    } catch (err) {
      showToast(err.message, true);
    }
  });

  document.getElementById("file-filter").addEventListener("change", loadFiles);

  const tokenInput = document.getElementById("api-token");
  tokenInput.value = localStorage.getItem("apiToken") || "";
  state.token = tokenInput.value;
  tokenInput.addEventListener("input", () => {
    state.token = tokenInput.value.trim();
    localStorage.setItem("apiToken", state.token);
  });

  document.getElementById("select-all-files").addEventListener("change", (event) => {
    const checked = event.target.checked;
    document.querySelectorAll(".file-select").forEach((checkbox) => {
      if (checkbox.disabled) {
        checkbox.checked = false;
        return;
      }
      checkbox.checked = checked;
      const id = checkbox.dataset.id;
      if (checked) {
        state.selectedFiles.add(id);
      } else {
        state.selectedFiles.delete(id);
      }
    });
  });

  document.getElementById("download-selected").addEventListener("click", () => {
    const ids = selectedFileIds();
    if (!ids.length) {
      showToast("Select files first", true);
      return;
    }
    state.downloadInProgress = true;
    ids.forEach((id, idx) => {
      setTimeout(() => {
        const iframe = document.createElement("iframe");
        iframe.style.display = "none";
        iframe.src = `${apiBase}/api/files/${id}/download?t=${Date.now()}`;
        document.body.appendChild(iframe);
        setTimeout(() => iframe.remove(), 30000);
      }, idx * 800);
    });
    setTimeout(() => {
      state.downloadInProgress = false;
    }, (ids.length * 800) + 3000);
  });

  document.getElementById("delete-selected").addEventListener("click", async () => {
    const ids = selectedFileIds();
    if (!ids.length) {
      showToast("Select files first", true);
      return;
    }
    if (!window.confirm(`Delete ${ids.length} file(s)?`)) {
      return;
    }
    let deleted = 0;
    const failed = [];
    const idsDescending = ids
      .map((id) => Number(id))
      .filter((id) => Number.isFinite(id))
      .sort((a, b) => b - a);

    for (const id of idsDescending) {
      try {
        await apiPost(`/api/files/${id}/delete`, {});
        deleted += 1;
      } catch (err) {
        failed.push(id);
      }
    }

    clearFileSelection();
    loadFiles();
    if (!failed.length) {
      showToast(`Deleted ${deleted} file(s)`);
      return;
    }
    showToast(`Deleted ${deleted}, failed ${failed.length}`, true);
  });
}

document.addEventListener("DOMContentLoaded", () => {
  wireEvents();
  const hash = window.location.hash.replace("#", "");
  const initialView = views.includes(hash) ? hash : "status";
  setView(initialView);
  loadStatus();
  loadConfig();
  if (initialView === "files") {
    loadFiles();
  }
  setInterval(loadStatus, 5000);
  setInterval(refreshWifiScan, 15000);
  document.addEventListener("visibilitychange", () => {
    if (!document.hidden) {
      loadStatus();
    }
  });
  window.addEventListener("focus", loadStatus);
});
