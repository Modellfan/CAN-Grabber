void server_task(void*) {
  for (;;) {
    s_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}


void configure_http_server() {
  auto add_uploader_contract = [](JsonObject uploader, const UploaderContractStats& up) {
    uploader["initialized"] = up.initialized;
    uploader["uploading"] = up.uploading;
    uploader["upload_speed_bps"] = up.upload_speed_bytes_per_sec;
    uploader["current_file_size_bytes"] = up.current_file_size_bytes;
    uploader["current_file_sent_bytes"] = up.current_file_sent_bytes;
    uploader["total_uploaded_files"] = up.total_uploaded_files;
    uploader["total_uploaded_bytes"] = up.total_uploaded_bytes;
    uploader["uploaded_files"] = up.uploaded_files;
    uploader["outstanding_files"] = up.outstanding_files;
    uploader["outstanding_bytes"] = up.outstanding_bytes;
    uploader["last_error"] = up.last_error;
    uploader["last_error_interrupted"] = up.last_error_interrupted;
    uploader["last_error_connect"] = up.last_error_connect;
    uploader["last_error_message"] = up.last_error_message;
    uploader["server_reachable_known"] = up.server_reachable_known;
    uploader["server_reachable"] = up.server_reachable;
    uploader["server_rtt_ms"] = up.server_rtt_ms;
    uploader["server_reach_message"] = up.server_reach_message;
  };

  s_server.on("/", HTTP_GET, []() {
    s_server.send_P(200, "text/html", kIndexHtml);
  });

  s_server.on("/status", HTTP_GET, [add_uploader_contract]() {
    UploadStats snap = read_stats_snapshot();
    ProbeState probe = {};
    read_probe_snapshot(&probe);
    UploaderContractStats up = read_uploader_contract_stats();
    const uint32_t now_ms = millis();
    const uint32_t probe_age_ms = (probe.last_probe_ms == 0) ? 0 : (now_ms - probe.last_probe_ms);
    IPAddress probe_ip(probe.ip_raw);
    String probe_ip_text = (probe.has_ip && probe.ip_raw != 0) ? probe_ip.toString() : String("n/a");
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["sent"] = static_cast<unsigned long long>(snap.bytesSent);
    root["total"] = static_cast<unsigned long long>(snap.totalBytes);
    root["rate"] = static_cast<unsigned>(snap.lastRateBps);
    root["state"] = state_name(snap.state);
    root["error"] = static_cast<long>(snap.errorCode);
    root["probe_ok"] = probe.ok ? 1U : 0U;
    root["probe_ip"] = probe_ip_text;
    root["probe_age_ms"] = static_cast<unsigned long>(probe_age_ms);
    root["probe_rtt_ms"] = static_cast<unsigned long>(probe.last_rtt_ms);
    root["probe_err"] = static_cast<long>(probe.last_error);
    JsonObject uploader = root["uploader"].to<JsonObject>();
    add_uploader_contract(uploader, up);

    String body;
    serializeJson(doc, body);
    s_server.send(200, "application/json", body);
  });

  s_server.on("/api/status", HTTP_GET, [add_uploader_contract]() {
    UploaderContractStats up = read_uploader_contract_stats();
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["uptime_sec"] = millis() / 1000;
    root["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    root["ip"] = WiFi.localIP().toString();
    root["ssid"] = WiFi.SSID();
    JsonObject uploader = root["uploader"].to<JsonObject>();
    add_uploader_contract(uploader, up);
    String body;
    serializeJson(doc, body);
    s_server.send(200, "application/json", body);
  });

  s_server.on("/start", HTTP_GET, []() {
    if (!ENABLE_UPLOAD_TASK) {
      s_server.send(503, "application/json", "{\"error\":\"upload_task_disabled\"}");
      return;
    }
    s_abort_requested.store(false, std::memory_order_relaxed);
    queue_pending();
    s_upload_requested.store(true, std::memory_order_relaxed);
    s_server.send(200, "text/plain", "ok");
  });

  s_server.on("/abort", HTTP_GET, []() {
    s_abort_requested.store(true, std::memory_order_relaxed);
    s_server.send(200, "text/plain", "ok");
  });

  s_server.begin();
}
