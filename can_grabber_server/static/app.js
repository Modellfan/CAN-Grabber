const elements = {
  banner: document.getElementById("messageBanner"),
  navItems: Array.from(document.querySelectorAll(".nav-item")),
  panels: Array.from(document.querySelectorAll(".panel")),

  sidebarStatus: document.getElementById("sidebarStatus"),
  topStatus: document.getElementById("topStatus"),
  overviewRunState: document.getElementById("overviewRunState"),
  overviewMessage: document.getElementById("overviewMessage"),

  selectedPathInput: document.getElementById("selectedPathInput"),
  selectedBinaryState: document.getElementById("selectedBinaryState"),
  installDirState: document.getElementById("installDirState"),
  pidState: document.getElementById("pidState"),
  logPathState: document.getElementById("logPathState"),
  candidateList: document.getElementById("candidateList"),
  searchButton: document.getElementById("searchButton"),
  savePathButton: document.getElementById("savePathButton"),
  startButton: document.getElementById("startButton"),
  refreshStatusButton: document.getElementById("refreshStatusButton"),

  uploadServerStatus: document.getElementById("uploadServerStatus"),
  uploadServerRefreshButton: document.getElementById("uploadServerRefreshButton"),
  uploadServerLanUrl: document.getElementById("uploadServerLanUrl"),
  uploadServerDirectory: document.getElementById("uploadServerDirectory"),
  uploadServerBonjour: document.getElementById("uploadServerBonjour"),
  uploadServerAuth: document.getElementById("uploadServerAuth"),
  uploadServerMaxBytes: document.getElementById("uploadServerMaxBytes"),
  uploadServerEndpoints: document.getElementById("uploadServerEndpoints"),

  dbcStatus: document.getElementById("dbcStatus"),
  dbcUploadInput: document.getElementById("dbcUploadInput"),
  dbcUploadOpenButton: document.getElementById("dbcUploadOpenButton"),
  dbcDirectoryInput: document.getElementById("dbcDirectoryInput"),
  dbcStorageMeta: document.getElementById("dbcStorageMeta"),
  dbcFileCount: document.getElementById("dbcFileCount"),
  dbcSelectedFile: document.getElementById("dbcSelectedFile"),
  dbcMessageCount: document.getElementById("dbcMessageCount"),
  dbcFileList: document.getElementById("dbcFileList"),
  dbcOpenFileTitle: document.getElementById("dbcOpenFileTitle"),
  dbcOpenFileMeta: document.getElementById("dbcOpenFileMeta"),
  dbcMessageTree: document.getElementById("dbcMessageTree"),
  dbcUseFolderButton: document.getElementById("dbcUseFolderButton"),
  dbcScanButton: document.getElementById("dbcScanButton"),
  dbcRefreshButton: document.getElementById("dbcRefreshButton"),
  dbcMessageForm: document.getElementById("dbcMessageForm"),
  dbcMessageCurrent: document.getElementById("dbcMessageCurrent"),
  dbcMessageName: document.getElementById("dbcMessageName"),
  dbcMessageComment: document.getElementById("dbcMessageComment"),
  dbcSaveMessageButton: document.getElementById("dbcSaveMessageButton"),
  dbcSignalForm: document.getElementById("dbcSignalForm"),
  dbcSignalCurrent: document.getElementById("dbcSignalCurrent"),
  dbcSignalName: document.getElementById("dbcSignalName"),
  dbcSignalScale: document.getElementById("dbcSignalScale"),
  dbcSignalOffset: document.getElementById("dbcSignalOffset"),
  dbcSignalComment: document.getElementById("dbcSignalComment"),
  dbcSaveSignalButton: document.getElementById("dbcSaveSignalButton"),
};

const actionButtons = [
  elements.searchButton,
  elements.savePathButton,
  elements.startButton,
  elements.refreshStatusButton,
  elements.uploadServerRefreshButton,
  elements.dbcUploadOpenButton,
  elements.dbcUseFolderButton,
  elements.dbcScanButton,
  elements.dbcRefreshButton,
  elements.dbcSaveMessageButton,
  elements.dbcSaveSignalButton,
];

const state = {
  dbcStatus: null,
  dbcDetails: null,
  selectedMessageName: "",
  selectedSignalName: "",
};

function setActivePanel(target) {
  elements.navItems.forEach((item) => {
    item.classList.toggle("is-active", item.dataset.panelTarget === target);
  });

  elements.panels.forEach((panel) => {
    panel.classList.toggle("is-active", panel.id === `panel-${target}`);
  });
}

function setPending(isPending) {
  actionButtons.forEach((button) => {
    button.disabled = isPending;
  });
}

function showMessage(message, tone = "info") {
  if (!message) {
    elements.banner.className = "message-banner is-hidden";
    elements.banner.textContent = "";
    return;
  }

  elements.banner.className = `message-banner ${tone}`;
  elements.banner.textContent = message;
}

function toneFromInflux(payload) {
  if (payload.running) {
    return "success";
  }
  return payload.selected_exists ? "info" : "error";
}

function toneFromDbc(payload) {
  if (!payload.directory_exists) {
    return "error";
  }
  return payload.file_count > 0 ? "success" : "info";
}

function setInfluxPill(target, payload) {
  const className = payload.running ? "running" : payload.selected_exists ? "idle" : "error";
  target.className = `status-pill ${className}`;
  target.textContent = payload.running ? "Running" : payload.selected_exists ? "Ready" : "Path missing";
}

function setDbcPill(payload, isLoaded = false) {
  const className = !payload.directory_exists ? "error" : isLoaded ? "running" : "idle";
  elements.dbcStatus.className = `status-pill ${className}`;
  elements.dbcStatus.textContent = !payload.directory_exists
    ? "Store missing"
    : isLoaded
      ? "File loaded"
      : "Store ready";
}

function setUploadServerPill(payload) {
  const ready = payload.bonjour_enabled ? payload.bonjour_registered : true;
  const className = ready ? "running" : "idle";
  elements.uploadServerStatus.className = `status-pill ${className}`;
  elements.uploadServerStatus.textContent = ready ? "Ready" : "Local only";
}

function requestBody(options) {
  const headers = { "Content-Type": "application/json" };
  return {
    headers,
    ...options,
  };
}

async function requestJson(url, options = {}) {
  const response = await fetch(url, requestBody(options));
  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(body.detail || "Request failed.");
  }
  return body;
}

async function requestMultipart(url, formData) {
  const response = await fetch(url, {
    method: "POST",
    body: formData,
  });
  const body = await response.json().catch(() => ({}));
  if (!response.ok) {
    throw new Error(body.detail || "Request failed.");
  }
  return body;
}

function renderInfluxCandidates(paths) {
  if (!paths.length) {
    elements.candidateList.innerHTML = '<li class="empty-state">No cached search results yet.</li>';
    return;
  }

  elements.candidateList.innerHTML = "";
  paths.forEach((path) => {
    const item = document.createElement("li");
    item.className = "candidate-item";

    const title = document.createElement("strong");
    title.textContent = path;

    const detail = document.createElement("small");
    detail.textContent = "Click use to make this the active binary path.";

    const button = document.createElement("button");
    button.type = "button";
    button.dataset.path = path;
    button.textContent = "Use this path";

    item.append(title, detail, button);
    elements.candidateList.appendChild(item);
  });
}

function renderInfluxStatus(payload) {
  elements.selectedPathInput.value = payload.selected_path || "";
  elements.selectedBinaryState.textContent = payload.selected_exists ? "Detected and ready" : "Missing or not selected";
  elements.installDirState.textContent = payload.install_dir || "Not available";
  elements.pidState.textContent = payload.pid || "-";
  elements.logPathState.textContent = payload.log_path || "<app-home>/runtime/influxd.log";
  elements.overviewRunState.textContent = payload.running ? "InfluxDB is running" : "InfluxDB is not running";
  elements.overviewMessage.textContent = payload.message || "No status message available.";

  setInfluxPill(elements.sidebarStatus, payload);
  setInfluxPill(elements.topStatus, payload);
  renderInfluxCandidates(payload.candidate_paths || []);
}

function renderUploadServerStatus(payload) {
  elements.uploadServerLanUrl.textContent = payload.lan_upload_url || "Not available";
  elements.uploadServerDirectory.textContent = payload.upload_directory || "Not available";
  elements.uploadServerBonjour.textContent = payload.bonjour_enabled
    ? payload.bonjour_registered
      ? payload.bonjour_url
      : `Failed${payload.bonjour_error ? `: ${payload.bonjour_error}` : ""}`
    : "Disabled";
  elements.uploadServerAuth.textContent = payload.requires_auth_token ? "Required" : "Not required";
  elements.uploadServerMaxBytes.textContent = `${payload.max_upload_bytes ?? 0} bytes`;

  const endpoints = payload.post_endpoints || [];
  if (!endpoints.length) {
    elements.uploadServerEndpoints.innerHTML = '<li class="empty-state">No upload endpoints reported.</li>';
  } else {
    elements.uploadServerEndpoints.innerHTML = "";
    endpoints.forEach((endpoint) => {
      const item = document.createElement("li");
      item.className = "candidate-item";
      const title = document.createElement("strong");
      title.textContent = endpoint;
      const detail = document.createElement("small");
      detail.textContent = endpoint.includes("chunked")
        ? "Raw stream upload endpoint."
        : "Multipart upload endpoint.";
      item.append(title, detail);
      elements.uploadServerEndpoints.appendChild(item);
    });
  }

  setUploadServerPill(payload);
}

function renderDbcFiles(files) {
  if (!files.length) {
    elements.dbcFileList.innerHTML = '<li class="empty-state">No DBC files found in the persistent store.</li>';
    return;
  }

  elements.dbcFileList.innerHTML = "";
  files.forEach((file) => {
    const item = document.createElement("li");
    item.className = "candidate-item";

    const title = document.createElement("strong");
    title.textContent = file.name;

    const detail = document.createElement("small");
    detail.textContent = `${file.size} bytes | ${file.modified}`;

    const button = document.createElement("button");
    button.type = "button";
    button.dataset.fileName = file.name;
    button.textContent = "Open file";

    item.append(title, detail, button);
    elements.dbcFileList.appendChild(item);
  });
}

function findMessage(messageName) {
  return state.dbcDetails?.messages?.find((message) => message.name === messageName) || null;
}

function findSignal(messageName, signalName) {
  const message = findMessage(messageName);
  return message?.signals?.find((signal) => signal.name === signalName) || null;
}

function clearSignalForm() {
  elements.dbcSignalCurrent.value = "";
  elements.dbcSignalName.value = "";
  elements.dbcSignalScale.value = "";
  elements.dbcSignalOffset.value = "";
  elements.dbcSignalComment.value = "";
}

function populateMessageForm(message) {
  elements.dbcMessageCurrent.value = message?.name || "";
  elements.dbcMessageName.value = message?.name || "";
  elements.dbcMessageComment.value = message?.comment || "";
}

function populateSignalForm(messageName, signal) {
  if (!signal) {
    clearSignalForm();
    return;
  }

  state.selectedMessageName = messageName;
  state.selectedSignalName = signal.name;
  elements.dbcSignalCurrent.value = signal.name || "";
  elements.dbcSignalName.value = signal.name || "";
  elements.dbcSignalScale.value = signal.scale ?? "";
  elements.dbcSignalOffset.value = signal.offset ?? "";
  elements.dbcSignalComment.value = signal.comment || "";
}

function selectMessage(messageName) {
  state.selectedMessageName = messageName;
  const message = findMessage(messageName);
  populateMessageForm(message);
  renderDbcTree();
}

function selectSignal(messageName, signalName) {
  const message = findMessage(messageName);
  const signal = findSignal(messageName, signalName);
  if (!message || !signal) {
    return;
  }

  state.selectedMessageName = messageName;
  state.selectedSignalName = signalName;
  populateMessageForm(message);
  populateSignalForm(messageName, signal);
  renderDbcTree();
}

function renderDbcTree() {
  if (!state.dbcDetails) {
    elements.dbcMessageTree.innerHTML = '<div class="empty-state">Messages and signals will appear here.</div>';
    return;
  }

  elements.dbcMessageTree.innerHTML = "";
  state.dbcDetails.messages.forEach((message) => {
    const messageNode = document.createElement("article");
    messageNode.className = `message-node${state.selectedMessageName === message.name ? " is-selected" : ""}`;

    const header = document.createElement("header");

    const infoBlock = document.createElement("div");
    const title = document.createElement("strong");
    title.className = "message-title";
    title.textContent = message.name;

    const copy = document.createElement("p");
    copy.className = "message-copy";
    copy.textContent = message.comment || "No message comment set.";

    const meta = document.createElement("div");
    meta.className = "message-meta";
    [
      message.frame_id_hex,
      `${message.length} bytes`,
      `${message.signal_count} signals`,
    ].forEach((label) => {
      const pill = document.createElement("span");
      pill.className = "meta-pill";
      pill.textContent = label;
      meta.appendChild(pill);
    });

    infoBlock.append(title, copy, meta);

    const actions = document.createElement("div");
    actions.className = "message-actions";
    const messageButton = document.createElement("button");
    messageButton.type = "button";
    messageButton.dataset.messageName = message.name;
    messageButton.textContent = "Edit message";
    actions.appendChild(messageButton);

    header.append(infoBlock, actions);
    messageNode.appendChild(header);

    const signalList = document.createElement("div");
    signalList.className = "signal-list";

    message.signals.forEach((signal) => {
      const signalNode = document.createElement("article");
      signalNode.className = `signal-node${
        state.selectedMessageName === message.name && state.selectedSignalName === signal.name ? " is-selected" : ""
      }`;

      const signalHeader = document.createElement("header");
      const signalInfo = document.createElement("div");
      signalInfo.className = "signal-info";

      const signalName = document.createElement("strong");
      signalName.textContent = signal.name;

      const signalCopy = document.createElement("small");
      signalCopy.textContent = `scale ${signal.scale} | offset ${signal.offset} | ${signal.unit || "no unit"}`;

      signalInfo.append(signalName, signalCopy);

      const signalActions = document.createElement("div");
      signalActions.className = "signal-actions";
      const signalButton = document.createElement("button");
      signalButton.type = "button";
      signalButton.dataset.messageName = message.name;
      signalButton.dataset.signalName = signal.name;
      signalButton.textContent = "Edit signal";
      signalActions.appendChild(signalButton);

      signalHeader.append(signalInfo, signalActions);
      signalNode.appendChild(signalHeader);
      signalList.appendChild(signalNode);
    });

    messageNode.appendChild(signalList);
    elements.dbcMessageTree.appendChild(messageNode);
  });
}

function renderDbcStatus(payload) {
  state.dbcStatus = payload;
  elements.dbcDirectoryInput.value = payload.source_directory_path || "";
  elements.dbcStorageMeta.textContent = `Persistent store: ${payload.storage_directory_path || payload.directory_path || "-"}`;
  elements.dbcFileCount.textContent = payload.file_count ?? 0;
  elements.dbcSelectedFile.textContent = state.dbcDetails?.file_name || payload.selected_file_name || "None";
  renderDbcFiles(payload.files || []);
  setDbcPill(payload, Boolean(state.dbcDetails));

  if (
    state.dbcDetails &&
    !payload.files.some((file) => file.name === state.dbcDetails.file_name)
  ) {
    state.dbcDetails = null;
    state.selectedMessageName = "";
    state.selectedSignalName = "";
    renderDbcDetails(null);
  }
}

function renderDbcDetails(payload) {
  if (!payload) {
    elements.dbcOpenFileTitle.textContent = "No DBC file opened";
    elements.dbcOpenFileMeta.textContent = "Pick a file from the list to inspect messages and signals.";
    elements.dbcSelectedFile.textContent = "None";
    elements.dbcMessageCount.textContent = "0";
    elements.dbcMessageCurrent.value = "";
    elements.dbcMessageName.value = "";
    elements.dbcMessageComment.value = "";
    clearSignalForm();
    renderDbcTree();
    if (state.dbcStatus) {
      setDbcPill(state.dbcStatus, false);
    }
    return;
  }

  state.dbcDetails = payload;
  elements.dbcOpenFileTitle.textContent = payload.file_name;
  elements.dbcOpenFileMeta.textContent = `${payload.file_path} | ${payload.message_count} message(s)`;
  elements.dbcSelectedFile.textContent = payload.file_name;
  elements.dbcMessageCount.textContent = payload.message_count ?? 0;

  if (!findMessage(state.selectedMessageName)) {
    state.selectedMessageName = payload.messages[0]?.name || "";
  }
  if (
    state.selectedMessageName &&
    state.selectedSignalName &&
    !findSignal(state.selectedMessageName, state.selectedSignalName)
  ) {
    state.selectedSignalName = "";
  }

  const selectedMessage = findMessage(state.selectedMessageName);
  populateMessageForm(selectedMessage);
  populateSignalForm(
    state.selectedMessageName,
    state.selectedSignalName ? findSignal(state.selectedMessageName, state.selectedSignalName) : null,
  );

  renderDbcTree();
  if (state.dbcStatus) {
    setDbcPill(state.dbcStatus, true);
  }
}

async function refreshInfluxStatus({ silent = false } = {}) {
  try {
    const payload = await requestJson("/api/influx/status", { method: "GET" });
    renderInfluxStatus(payload);
    if (!silent) {
      showMessage(payload.message, toneFromInflux(payload));
    }
  } catch (error) {
    showMessage(error.message, "error");
  }
}

async function refreshDbcStatus({ silent = false } = {}) {
  try {
    const payload = await requestJson("/api/dbc/status", { method: "GET" });
    renderDbcStatus(payload);
    if (!silent) {
      showMessage(payload.message, toneFromDbc(payload));
    }
  } catch (error) {
    showMessage(error.message, "error");
  }
}

async function refreshUploadServerStatus({ silent = false } = {}) {
  try {
    const payload = await requestJson("/api/upload-server/status", { method: "GET" });
    renderUploadServerStatus(payload);
    if (!silent) {
      showMessage("Upload server status refreshed.", "success");
    }
  } catch (error) {
    showMessage(error.message, "error");
  }
}

async function runAction(action, onSuccess) {
  setPending(true);
  try {
    const payload = await action();
    await onSuccess(payload);
  } catch (error) {
    showMessage(error.message, "error");
  } finally {
    setPending(false);
  }
}

async function openDbcFile(fileName) {
  return runAction(
    () => requestJson(`/api/dbc/file?file_name=${encodeURIComponent(fileName)}`, { method: "GET" }),
    (payload) => {
      state.selectedMessageName = payload.messages[0]?.name || "";
      state.selectedSignalName = "";
      renderDbcDetails(payload);
      elements.dbcSelectedFile.textContent = payload.file_name;
      showMessage(payload.message, "success");
    },
  );
}

elements.navItems.forEach((item) => {
  item.addEventListener("click", () => setActivePanel(item.dataset.panelTarget));
});

elements.searchButton.addEventListener("click", () =>
  runAction(
    () => requestJson("/api/influx/search", { method: "POST", body: "{}" }),
    (payload) => {
      renderInfluxStatus(payload);
      showMessage(payload.message, toneFromInflux(payload));
    },
  ),
);

elements.savePathButton.addEventListener("click", () =>
  runAction(
    () =>
      requestJson("/api/influx/path", {
        method: "POST",
        body: JSON.stringify({ path: elements.selectedPathInput.value }),
      }),
    (payload) => {
      renderInfluxStatus(payload);
      showMessage(payload.message, toneFromInflux(payload));
    },
  ),
);

elements.startButton.addEventListener("click", () =>
  runAction(
    () => requestJson("/api/influx/start", { method: "POST", body: "{}" }),
    (payload) => {
      renderInfluxStatus(payload);
      showMessage(payload.message, toneFromInflux(payload));
    },
  ),
);

elements.refreshStatusButton.addEventListener("click", () => refreshInfluxStatus());

elements.uploadServerRefreshButton.addEventListener("click", () => refreshUploadServerStatus());

elements.candidateList.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-path]");
  if (!button) {
    return;
  }

  elements.selectedPathInput.value = button.dataset.path || "";
  runAction(
    () =>
      requestJson("/api/influx/path", {
        method: "POST",
        body: JSON.stringify({ path: elements.selectedPathInput.value }),
      }),
    (payload) => {
      renderInfluxStatus(payload);
      showMessage(payload.message, toneFromInflux(payload));
    },
  );
});

elements.dbcUploadOpenButton.addEventListener("click", () => {
  const file = elements.dbcUploadInput.files?.[0];
  if (!file) {
    showMessage("Select a .dbc file first.", "error");
    return;
  }

  runAction(
    async () => {
      const formData = new FormData();
      formData.append("file", file);
      return requestMultipart("/api/dbc/upload-open", formData);
    },
    async (details) => {
      state.selectedMessageName = details.messages[0]?.name || "";
      state.selectedSignalName = "";
      renderDbcDetails(details);
      elements.dbcSelectedFile.textContent = details.file_name;
      elements.dbcUploadInput.value = "";
      await refreshDbcStatus({ silent: true });
      showMessage(details.message, "success");
    },
  );
});

elements.dbcUseFolderButton.addEventListener("click", () =>
  runAction(
    () =>
      requestJson("/api/dbc/directory", {
        method: "POST",
        body: JSON.stringify({ path: elements.dbcDirectoryInput.value }),
      }),
    (payload) => {
      renderDbcStatus(payload);
      showMessage(payload.message, toneFromDbc(payload));
    },
  ),
);

elements.dbcScanButton.addEventListener("click", () =>
  runAction(
    () => requestJson("/api/dbc/scan", { method: "POST", body: "{}" }),
    (payload) => {
      renderDbcStatus(payload);
      showMessage(payload.message, toneFromDbc(payload));
    },
  ),
);

elements.dbcRefreshButton.addEventListener("click", () => refreshDbcStatus());

elements.dbcFileList.addEventListener("click", (event) => {
  const button = event.target.closest("button[data-file-name]");
  if (!button) {
    return;
  }
  openDbcFile(button.dataset.fileName || "");
});

elements.dbcMessageTree.addEventListener("click", (event) => {
  const messageButton = event.target.closest("button[data-message-name]:not([data-signal-name])");
  if (messageButton) {
    selectMessage(messageButton.dataset.messageName || "");
    return;
  }

  const signalButton = event.target.closest("button[data-signal-name]");
  if (signalButton) {
    selectSignal(signalButton.dataset.messageName || "", signalButton.dataset.signalName || "");
  }
});

elements.dbcMessageForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!state.dbcDetails || !elements.dbcMessageCurrent.value) {
    showMessage("Open a DBC file and select a message first.", "error");
    return;
  }

  const payload = {
    file_name: state.dbcDetails.file_name,
    current_message_name: elements.dbcMessageCurrent.value,
    new_name: elements.dbcMessageName.value.trim() || elements.dbcMessageCurrent.value,
    comment: elements.dbcMessageComment.value,
  };

  runAction(
    () =>
      requestJson("/api/dbc/message", {
        method: "POST",
        body: JSON.stringify(payload),
      }),
    (details) => {
      state.selectedMessageName = payload.new_name;
      renderDbcDetails(details);
      showMessage(details.message, "success");
    },
  );
});

elements.dbcSignalForm.addEventListener("submit", (event) => {
  event.preventDefault();
  if (!state.dbcDetails || !elements.dbcSignalCurrent.value || !elements.dbcMessageCurrent.value) {
    showMessage("Open a DBC file and select a signal first.", "error");
    return;
  }

  const scale = Number.parseFloat(elements.dbcSignalScale.value);
  const offset = Number.parseFloat(elements.dbcSignalOffset.value);
  if (Number.isNaN(scale) || Number.isNaN(offset)) {
    showMessage("Scale and offset must be valid numbers.", "error");
    return;
  }

  const payload = {
    file_name: state.dbcDetails.file_name,
    message_name: elements.dbcMessageCurrent.value,
    current_signal_name: elements.dbcSignalCurrent.value,
    new_name: elements.dbcSignalName.value.trim() || elements.dbcSignalCurrent.value,
    scale,
    offset,
    comment: elements.dbcSignalComment.value,
  };

  runAction(
    () =>
      requestJson("/api/dbc/signal", {
        method: "POST",
        body: JSON.stringify(payload),
      }),
    (details) => {
      state.selectedMessageName = payload.message_name;
      state.selectedSignalName = payload.new_name;
      renderDbcDetails(details);
      showMessage(details.message, "success");
    },
  );
});

setActivePanel("dbc");
refreshInfluxStatus({ silent: true });
refreshDbcStatus({ silent: true });
refreshUploadServerStatus({ silent: true });
window.setInterval(() => refreshInfluxStatus({ silent: true }), 5000);
window.setInterval(() => refreshUploadServerStatus({ silent: true }), 5000);
