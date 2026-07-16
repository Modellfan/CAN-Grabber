void server_task(void*) {
  for (;;) {
    s_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void configure_http_server() {
  s_server.on("/", HTTP_GET, []() {
    s_server.send_P(200, "text/html", kIndexHtml);
  });

  s_server.on("/status", HTTP_GET, []() {
    UploadStats snap = read_stats_snapshot();
    ProbeState probe = {};
    read_probe_snapshot(&probe);

    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["sent"] = static_cast<unsigned long long>(snap.bytes_sent);
    root["total"] = static_cast<unsigned long long>(snap.total_bytes);
    root["rate"] = snap.rate_bps;
    root["state"] = state_name(snap.state);
    root["error"] = snap.error_code;
    root["zone"] = zone_name(s_backlog_zone.load(std::memory_order_relaxed));
    root["backlog"] = s_last_backlog_bytes.load(std::memory_order_relaxed);
    root["log_bps"] = s_log_bytes_per_sec.load(std::memory_order_relaxed);
    root["log_total"] = static_cast<unsigned long long>(s_log_total_bytes.load(std::memory_order_relaxed));
    root["log_pops"] = s_log_pop_count.load(std::memory_order_relaxed);
    root["probe_ok"] = probe.ok ? 1U : 0U;
    root["probe_rtt_ms"] = probe.last_rtt_ms;
    root["sim_fps"] = s_sim_fps.load(std::memory_order_relaxed);

    JsonObject can_obj = root["can"].to<JsonObject>();
    for (uint8_t bus = 0; bus < config::kMaxBuses; ++bus) {
      char key[8];
      snprintf(key, sizeof(key), "b%u", static_cast<unsigned>(bus));
      JsonObject b = can_obj[key].to<JsonObject>();
      b["depth"] = can::queue_depth(bus);
      b["drops"] = can::drop_count(bus);
      b["high_water"] = can::high_water(bus);
      b["produced"] = can::load_test_produced(bus);
    }

    String body;
    serializeJson(doc, body);
    s_server.send(200, "application/json", body);
  });

  s_server.on("/api/status", HTTP_GET, []() {
    JsonDocument doc;
    JsonObject root = doc.to<JsonObject>();
    root["uptime_sec"] = millis() / 1000;
    root["wifi_connected"] = (WiFi.status() == WL_CONNECTED);
    root["ip"] = WiFi.localIP().toString();
    root["ssid"] = WiFi.SSID();

    UploadStats snap = read_stats_snapshot();
    JsonObject uploader = root["uploader"].to<JsonObject>();
    uploader["initialized"] = true;
    uploader["uploading"] = (snap.state == UploadState::CONNECTING || snap.state == UploadState::SENDING ||
                              snap.state == UploadState::FINALIZING);
    uploader["upload_speed_bps"] = snap.rate_bps;
    uploader["current_file_size_bytes"] = static_cast<unsigned long long>(snap.total_bytes);
    uploader["current_file_sent_bytes"] = static_cast<unsigned long long>(snap.bytes_sent);
    uploader["total_uploaded_files"] = 0;
    uploader["total_uploaded_bytes"] = static_cast<unsigned long long>(snap.bytes_sent);
    uploader["uploaded_files"] = 0;
    uploader["outstanding_files"] = 0;
    uploader["outstanding_bytes"] = 0;
    uploader["last_error"] = (snap.state == UploadState::ERROR);
    uploader["last_error_interrupted"] = false;
    uploader["last_error_connect"] = false;
    uploader["last_error_message"] = "";
    uploader["server_reachable_known"] = true;
    uploader["server_reachable"] = probe_server_reachability(false);
    ProbeState probe = {};
    read_probe_snapshot(&probe);
    uploader["server_rtt_ms"] = static_cast<int32_t>(probe.last_rtt_ms);
    uploader["server_reach_message"] = probe.ok ? "reachable" : "unreachable";

    String body;
    serializeJson(doc, body);
    s_server.send(200, "application/json", body);
  });

  s_server.on("/start", HTTP_GET, []() {
    s_abort_requested.store(false, std::memory_order_relaxed);
    queue_pending_scan();
    s_upload_requested.store(true, std::memory_order_relaxed);
    s_server.send(200, "text/plain", "ok");
  });

  s_server.on("/abort", HTTP_GET, []() {
    s_abort_requested.store(true, std::memory_order_relaxed);
    stream_set_request_stop();
    s_server.send(200, "text/plain", "ok");
  });

  s_server.on("/sim/load", HTTP_GET, []() {
    uint32_t fps = 4000;
    if (s_server.hasArg("fps")) {
      fps = static_cast<uint32_t>(strtoul(s_server.arg("fps").c_str(), nullptr, 10));
    }
    can::set_load_test_fps(fps);
    s_sim_fps.store(fps, std::memory_order_relaxed);
    char body[96];
    snprintf(body, sizeof(body), "{\"ok\":true,\"sim_fps\":%lu}", static_cast<unsigned long>(fps));
    s_server.send(200, "application/json", body);
  });

  s_server.begin();
}
