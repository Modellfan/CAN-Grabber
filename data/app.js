const state = {
  config: null,
  token: "",
  wifiScan: [],
  selectedFiles: new Set()
};

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
    link.style.backgroundColor = active ? "white" : "";
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
  const res = await fetch(`${apiBase}${path}`, { headers: apiHeaders() });
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
      const checked = state.selectedFiles.has(String(entry.id)) ? "checked" : "";
      row.innerHTML = `
        <td><input type="checkbox" class="file-select" data-id="${entry.id}" ${checked}></td>
        <td>${entry.id}</td>
        <td>${entry.bus_id}</td>
        <td>${name}</td>
        <td>${formatBytes(entry.size_bytes)}</td>
        <td>${entry.start_ms}</td>
        <td>${entry.end_ms}</td>
        <td>${formatFlags(entry.flags)}</td>
        <td>
          <a href="${apiBase}/api/files/${entry.id}/download">Download</a>
          <button class="butt small" data-id="${entry.id}">Mark downloaded</button>
        </td>
      `;
      body.appendChild(row);
    });

    document.querySelectorAll(".file-select").forEach((checkbox) => {
      checkbox.addEventListener("change", () => {
        const id = checkbox.dataset.id;
        if (checkbox.checked) {
          state.selectedFiles.add(id);
        } else {
          state.selectedFiles.delete(id);
        }
      });
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

  document.getElementById("refresh-status").addEventListener("click", loadStatus);
  document.getElementById("refresh-config").addEventListener("click", loadConfig);
  document.getElementById("refresh-can").addEventListener("click", loadConfig);
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

  document.getElementById("select-all-files").addEventListener("change", (event) => {
    const checked = event.target.checked;
    document.querySelectorAll(".file-select").forEach((checkbox) => {
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
    ids.forEach((id, idx) => {
      setTimeout(() => {
        window.open(`${apiBase}/api/files/${id}/download`, "_blank");
      }, idx * 300);
    });
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
    try {
      for (const id of ids) {
        await apiPost(`/api/files/${id}/delete`, {});
      }
      showToast("Files deleted");
      clearFileSelection();
      loadFiles();
    } catch (err) {
      showToast(err.message, true);
    }
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
});
