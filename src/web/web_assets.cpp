#include "web/web_assets.h"

namespace web {

const char kIndexHtml[] PROGMEM = R"HTML(
<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>CAN-Grabber</title>
  <link rel="stylesheet" href="/style.css">
</head>
<body>
  <nav id="navbar">
    <div id="logo">
      <div class="logo-mark">CAN</div>
      <div class="logo-sub">Grabber</div>
    </div>
    <a class="nav-link active" data-view="status" href="#status">Status</a>
    <a class="nav-link" data-view="config" href="#config">Config</a>
    <a class="nav-link" data-view="files" href="#files">Files</a>
    <div class="nav-token">
      <label for="api-token">API Token</label>
      <input id="api-token" type="password" placeholder="optional">
    </div>
  </nav>

  <main id="content-wrapper">
    <section id="view-status" class="view active">
      <div class="page-title">
        <h1>Status</h1>
        <button class="butt" id="refresh-status">Refresh</button>
      </div>
      <div class="grid">
        <div class="card">
          <h2>System</h2>
          <div class="row"><span>Uptime</span><strong id="uptime">-</strong></div>
          <div class="row"><span>Time</span><strong id="time-now">-</strong></div>
          <div class="row"><span>Logging</span><strong id="logging-state">-</strong></div>
        </div>
        <div class="card">
          <h2>Network</h2>
          <div class="row"><span>SSID</span><strong id="wifi-ssid">-</strong></div>
          <div class="row"><span>IP</span><strong id="wifi-ip">-</strong></div>
          <div class="row"><span>RSSI</span><strong id="wifi-rssi">-</strong></div>
        </div>
        <div class="card">
          <h2>Storage</h2>
          <div class="row"><span>Ready</span><strong id="sd-ready">-</strong></div>
          <div class="row"><span>Total</span><strong id="sd-total">-</strong></div>
          <div class="row"><span>Free</span><strong id="sd-free">-</strong></div>
        </div>
        <div class="card">
          <h2>Buffers</h2>
          <div class="row"><span>Bytes/s</span><strong id="log-rate">-</strong></div>
          <div class="row"><span>Total</span><strong id="log-total">-</strong></div>
          <div class="row"><span>Active buses</span><strong id="log-buses">-</strong></div>
        </div>
      </div>

      <h2 class="section-title">CAN Stats</h2>
      <table>
        <thead>
          <tr><th>Bus</th><th>Drops</th><th>High Water (bytes)</th></tr>
        </thead>
        <tbody id="can-stats-body"></tbody>
      </table>
    </section>

    <section id="view-config" class="view">
      <div class="page-title">
        <h1>Configuration</h1>
        <button class="butt" id="refresh-config">Reload</button>
        <button class="butt" id="save-config">Save</button>
      </div>

      <h2 class="section-title">CAN Buses</h2>
      <div id="bus-configs" class="stack"></div>

      <h2 class="section-title">Logging</h2>
      <div class="form-grid">
        <label>Max file size (MB)</label>
        <input id="log-max-size" type="number" min="1">
        <label>Low space threshold (MB)</label>
        <input id="log-low-space" type="number" min="0">
      </div>
      <div class="button-row">
        <button class="butt" id="start-logging">Start logging</button>
        <button class="butt" id="stop-logging">Stop logging</button>
        <button class="butt" id="close-file">Close active file</button>
      </div>

      <h2 class="section-title">Time</h2>
      <div class="form-grid">
        <label>CAN time sync</label>
        <div class="switch">
          <input id="can-time-sync" type="checkbox">
          <span class="slider"></span>
        </div>
        <label>Set time</label>
        <div class="time-row">
          <input id="time-set" type="datetime-local">
          <button class="butt" id="set-time">Set</button>
        </div>
      </div>

      <h2 class="section-title">WiFi</h2>
      <div id="wifi-configs" class="stack"></div>

      <h2 class="section-title">Upload</h2>
      <div class="form-grid">
        <label>Upload URL</label>
        <input id="upload-url" type="text" placeholder="http://...">
      </div>
    </section>

    <section id="view-files" class="view">
      <div class="page-title">
        <h1>Files</h1>
        <div class="filters">
          <label for="file-filter">Bus</label>
          <select id="file-filter"></select>
          <button class="butt" id="refresh-files">Refresh</button>
        </div>
      </div>
      <table>
        <thead>
          <tr><th>ID</th><th>Bus</th><th>Name</th><th>Size</th><th>Start</th><th>End</th><th>Flags</th><th>Actions</th></tr>
        </thead>
        <tbody id="files-body"></tbody>
      </table>
    </section>
  </main>

  <div id="toast" class="toast"></div>
  <script src="/app.js"></script>
</body>
</html>
)HTML";

const char kStyleCss[] PROGMEM = R"CSS(
* {
  box-sizing: border-box;
}

body {
  margin: 0;
  font-family: "Verdana", "Geneva", sans-serif;
  background: #f4f5f2;
  color: #1f1f1f;
}

#navbar {
  background-color: #4caf50;
  min-height: 100vh;
  position: fixed;
  width: 200px;
  overflow-y: auto;
  top: 0;
  left: 0;
  padding: 16px 0;
  border-right: 3px solid #2f7a33;
}

#logo {
  text-align: center;
  color: #0d140d;
  margin-bottom: 16px;
}

.logo-mark {
  font-size: 26px;
  font-weight: 700;
  letter-spacing: 2px;
}

.logo-sub {
  font-size: 12px;
  text-transform: uppercase;
  letter-spacing: 3px;
}

.nav-link {
  display: block;
  padding: 12px 16px;
  color: #0d140d;
  text-decoration: none;
  font-weight: 600;
  border-left: 4px solid transparent;
}

.nav-link:hover,
.nav-link.active {
  background: #f4f5f2;
  border-left-color: #f19136;
}

.nav-token {
  padding: 12px 16px;
  margin-top: 12px;
  font-size: 12px;
}

.nav-token label {
  display: block;
  margin-bottom: 6px;
}

.nav-token input {
  width: 100%;
  padding: 6px 8px;
  border: 2px solid #2f7a33;
  border-radius: 4px;
}

#content-wrapper {
  margin-left: 200px;
  padding: 24px 32px;
  min-height: 100vh;
}

.view {
  display: none;
}

.view.active {
  display: block;
}

.page-title {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 12px;
  margin-bottom: 16px;
}

.page-title h1 {
  margin: 0;
  font-size: 24px;
}

.butt {
  background-color: #4caf50;
  border: 2px solid #2f7a33;
  color: #0d140d;
  padding: 6px 12px;
  font-weight: 600;
  border-radius: 4px;
  cursor: pointer;
}

.butt:hover {
  background-color: #f4f5f2;
}

.butt.small {
  padding: 4px 8px;
  font-size: 12px;
}

.grid {
  display: grid;
  grid-template-columns: repeat(auto-fit, minmax(220px, 1fr));
  gap: 16px;
}

.card {
  background: #ffffff;
  border: 2px solid #d6dcd2;
  border-radius: 10px;
  padding: 16px;
  box-shadow: 0 2px 0 rgba(0, 0, 0, 0.05);
}

.card h2 {
  margin: 0 0 10px 0;
  font-size: 16px;
  text-transform: uppercase;
}

.row {
  display: flex;
  justify-content: space-between;
  margin-bottom: 6px;
}

.section-title {
  margin: 24px 0 10px 0;
  font-size: 18px;
  border-bottom: 2px solid #d6dcd2;
  padding-bottom: 6px;
}

table {
  width: 100%;
  border-collapse: collapse;
  background: #ffffff;
}

th, td {
  padding: 8px;
  border-bottom: 1px solid #e0e0e0;
  text-align: left;
  font-size: 13px;
}

th {
  background: #e5e5e5;
  position: sticky;
  top: 0;
}

.stack {
  display: grid;
  gap: 10px;
}

.bus-card,
.wifi-card {
  background: #ffffff;
  border: 2px solid #d6dcd2;
  border-radius: 8px;
  padding: 12px;
  display: grid;
  gap: 8px;
}

.form-grid {
  display: grid;
  grid-template-columns: 180px 1fr;
  gap: 8px 16px;
  align-items: center;
}

.form-grid input,
.form-grid select {
  padding: 6px 8px;
  border: 2px solid #4caf50;
  border-radius: 4px;
}

.button-row {
  display: flex;
  flex-wrap: wrap;
  gap: 10px;
  margin-top: 10px;
}

.filters {
  display: flex;
  align-items: center;
  gap: 10px;
}

.filters select {
  padding: 6px 8px;
  border: 2px solid #4caf50;
  border-radius: 4px;
}

.time-row {
  display: flex;
  gap: 8px;
}

.switch {
  position: relative;
  display: inline-block;
  width: 36px;
  height: 18px;
}

.switch input {
  opacity: 0;
  width: 0;
  height: 0;
}

.slider {
  position: absolute;
  cursor: pointer;
  top: 0;
  left: 0;
  right: 0;
  bottom: 0;
  background-color: #ccc;
  transition: 0.2s;
  border-radius: 20px;
}

.slider:before {
  position: absolute;
  content: "";
  height: 12px;
  width: 12px;
  left: 3px;
  bottom: 3px;
  background-color: white;
  transition: 0.2s;
  border-radius: 50%;
}

.switch input:checked + .slider {
  background-color: #f19136;
}

.switch input:checked + .slider:before {
  transform: translateX(18px);
}

.toast {
  position: fixed;
  bottom: 16px;
  right: 16px;
  background: #1f1f1f;
  color: #ffffff;
  padding: 10px 14px;
  border-radius: 6px;
  display: none;
  font-size: 13px;
}

@media (max-width: 900px) {
  #navbar {
    position: static;
    width: 100%;
    min-height: auto;
    border-right: none;
    border-bottom: 3px solid #2f7a33;
  }

  #content-wrapper {
    margin-left: 0;
  }

  .page-title {
    flex-direction: column;
    align-items: flex-start;
  }

  .form-grid {
    grid-template-columns: 1fr;
  }
}
)CSS";

const char kAppJs[] PROGMEM = R"JS(
const state = {
  config: null,
  token: ""
};

const views = ["status", "config", "files"];

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
    const section = document.getElementById(`view-${view}`);
    const link = document.querySelector(`.nav-link[data-view="${view}"]`);
    if (!section || !link) {
      return;
    }
    const active = view === name;
    section.classList.toggle("active", active);
    link.classList.toggle("active", active);
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
  const res = await fetch(path, { headers: apiHeaders() });
  if (!res.ok) {
    throw new Error(`Request failed: ${res.status}`);
  }
  return res.json();
}

async function apiPost(path, payload) {
  const res = await fetch(path, {
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
  const res = await fetch(path, {
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
        <div class="switch">
          <input id="bus-enabled-${bus.id}" type="checkbox">
          <span class="slider"></span>
        </div>
        <label>Bitrate</label>
        <select id="bus-bitrate-${bus.id}">
          <option value="125000">125 kbit/s</option>
          <option value="250000">250 kbit/s</option>
          <option value="500000">500 kbit/s</option>
          <option value="1000000">1 Mbit/s</option>
        </select>
        <label>Read only</label>
        <div class="switch">
          <input id="bus-readonly-${bus.id}" type="checkbox">
          <span class="slider"></span>
        </div>
        <label>Logging</label>
        <div class="switch">
          <input id="bus-logging-${bus.id}" type="checkbox">
          <span class="slider"></span>
        </div>
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
        <input id="wifi-ssid-${i}" type="text" maxlength="32">
        <label>Password</label>
        <input id="wifi-pass-${i}" type="password" maxlength="64">
      </div>
    `;
    container.appendChild(wrapper);
    document.getElementById(`wifi-ssid-${i}`).value = entry.ssid || "";
    document.getElementById(`wifi-pass-${i}`).value = entry.password || "";
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
  try {
    const data = await apiGet("/api/status");
    document.getElementById("uptime").textContent = formatUptime(data.uptime_sec);
    document.getElementById("wifi-ssid").textContent = data.ssid || "-";
    document.getElementById("wifi-ip").textContent = data.ip || "-";
    document.getElementById("wifi-rssi").textContent = `${data.rssi_percent}%`;
    document.getElementById("sd-ready").textContent = data.storage.ready ? "yes" : "no";
    document.getElementById("sd-total").textContent = formatBytes(data.storage.total_bytes);
    document.getElementById("sd-free").textContent = formatBytes(data.storage.free_bytes);
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

    const body = document.getElementById("can-stats-body");
    body.innerHTML = "";
    data.can.forEach((entry) => {
      const row = document.createElement("tr");
      row.innerHTML = `<td>${entry.bus}</td><td>${entry.drops}</td><td>${entry.high_water}</td>`;
      body.appendChild(row);
    });
  } catch (err) {
    showToast(err.message, true);
  }
}

async function loadConfig() {
  try {
    const data = await apiGet("/api/config");
    state.config = data;
    renderBusConfig(data);
    renderWifiConfig(data);
    document.getElementById("log-max-size").value = Math.round(data.global.max_file_size_bytes / (1024 * 1024));
    document.getElementById("log-low-space").value = Math.round(data.global.low_space_threshold_bytes / (1024 * 1024));
    document.getElementById("upload-url").value = data.global.upload_url || "";
    document.getElementById("can-time-sync").checked = !!data.global.can_time_sync;
  } catch (err) {
    showToast(err.message, true);
  }
}

async function loadFiles() {
  try {
    const data = await apiGet("/api/files");
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
      row.innerHTML = `
        <td>${entry.id}</td>
        <td>${entry.bus_id}</td>
        <td>${name}</td>
        <td>${formatBytes(entry.size_bytes)}</td>
        <td>${entry.start_ms}</td>
        <td>${entry.end_ms}</td>
        <td>${formatFlags(entry.flags)}</td>
        <td>
          <a href="/api/files/${entry.id}/download">Download</a>
          <button class="butt small" data-id="${entry.id}">Mark downloaded</button>
        </td>
      `;
      body.appendChild(row);
    });

    document.querySelectorAll(".butt.small").forEach((btn) => {
      btn.addEventListener("click", async () => {
        try {
          await apiPost(`/api/files/${btn.dataset.id}/mark_downloaded`, {});
          showToast("Marked downloaded");
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

function wireEvents() {
  document.querySelectorAll(".nav-link").forEach((link) => {
    link.addEventListener("click", (event) => {
      const view = event.currentTarget.dataset.view;
      setView(view);
      if (view === "files") {
        loadFiles();
      }
    });
  });

  document.getElementById("refresh-status").addEventListener("click", loadStatus);
  document.getElementById("refresh-config").addEventListener("click", loadConfig);
  document.getElementById("refresh-files").addEventListener("click", loadFiles);

  document.getElementById("save-config").addEventListener("click", async () => {
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

  document.getElementById("start-logging").addEventListener("click", async () => {
    try {
      await apiPost("/api/control/start_logging", {});
      showToast("Logging started");
      loadStatus();
    } catch (err) {
      showToast(err.message, true);
    }
  });

  document.getElementById("stop-logging").addEventListener("click", async () => {
    try {
      await apiPost("/api/control/stop_logging", {});
      showToast("Logging stopped");
      loadStatus();
    } catch (err) {
      showToast(err.message, true);
    }
  });

  document.getElementById("close-file").addEventListener("click", async () => {
    try {
      await apiPost("/api/control/close_active_file", {});
      showToast("File closed");
    } catch (err) {
      showToast(err.message, true);
    }
  });

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
});
)JS";

} // namespace web
