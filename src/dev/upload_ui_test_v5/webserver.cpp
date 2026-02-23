// ====================================================================================================
// Task Section
// ====================================================================================================
// Web API setup and HTTP server task for the v5 upload tester.
// Dedicated HTTP event loop task entrypoint (kept at file start by convention).
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
    ProbeStats probe = read_probe_snapshot();
    uint32_t queue_ready = 0;
    uint32_t queue_delayed = 0;
    queue_state_summary(&queue_ready, &queue_delayed);

    const bool upload_active =
        (snap.state == UploadState::CONNECTING || snap.state == UploadState::SENDING || snap.state == UploadState::FINALIZING);
    const bool probe_ok = upload_active ? true : probe.last_ok;
    const char* probe_err = upload_active ? "" : probe.last_error;
    uint32_t outstanding = queue_ready + queue_delayed + (upload_active ? 1U : 0U);
    if (outstanding > kTestFileCount) {
      outstanding = kTestFileCount;
    }
    const uint32_t uploaded = static_cast<uint32_t>(kTestFileCount) - outstanding;
    const uint64_t total_target_bytes = static_cast<uint64_t>(kTestFileCount) * static_cast<uint64_t>(kCreateBytes);
    uint64_t done_bytes = static_cast<uint64_t>(uploaded) * static_cast<uint64_t>(kCreateBytes);
    if (upload_active) {
      uint64_t in_flight = snap.bytesSent;
      if (in_flight > kCreateBytes) {
        in_flight = kCreateBytes;
      }
      done_bytes += in_flight;
    }
    if (done_bytes > total_target_bytes) {
      done_bytes = total_target_bytes;
    }
    const uint64_t left_bytes = total_target_bytes - done_bytes;
    const uint32_t eta_s =
        (snap.lastRateBps > 0 && left_bytes > 0)
            ? static_cast<uint32_t>((left_bytes + static_cast<uint64_t>(snap.lastRateBps) - 1ULL) /
                                    static_cast<uint64_t>(snap.lastRateBps))
            : 0;
    const uint32_t probe_age_ms =
        (probe.last_update_ms == 0 || static_cast<int32_t>(millis() - probe.last_update_ms) < 0)
            ? 0
            : static_cast<uint32_t>(millis() - probe.last_update_ms);

    char body[1024];
    int n = snprintf(body, sizeof(body),
                     "{\"sent\":%llu,\"total\":%llu,\"rate\":%u,\"state\":\"%s\",\"error\":%ld,"
                     "\"sd_ok\":%u,"
                     "\"probe_ok\":%u,\"probe_ms\":%lu,\"probe_ip\":\"%s\",\"probe_err\":\"%s\","
                     "\"probe_age_ms\":%lu,\"cache_hits\":%lu,\"cache_misses\":%lu,"
                     "\"uploaded_count\":%lu,\"outstanding_count\":%lu,\"queue_ready\":%lu,\"queue_delayed\":%lu,"
                     "\"done_bytes\":%llu,\"left_bytes\":%llu,\"eta_s\":%lu}",
                     static_cast<unsigned long long>(snap.bytesSent),
                     static_cast<unsigned long long>(snap.totalBytes),
                     static_cast<unsigned>(snap.lastRateBps),
                     state_name(snap.state),
                     static_cast<long>(snap.errorCode),
                     sd_available() ? 1U : 0U,
                     probe_ok ? 1U : 0U,
                     static_cast<unsigned long>(probe.last_latency_ms),
                     probe.last_ip,
                     probe_err,
                     static_cast<unsigned long>(probe_age_ms),
                     static_cast<unsigned long>(probe.cache_hits),
                     static_cast<unsigned long>(probe.cache_misses),
                     static_cast<unsigned long>(uploaded),
                     static_cast<unsigned long>(outstanding),
                     static_cast<unsigned long>(queue_ready),
                     static_cast<unsigned long>(queue_delayed),
                     static_cast<unsigned long long>(done_bytes),
                     static_cast<unsigned long long>(left_bytes),
                     static_cast<unsigned long>(eta_s));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) {
      s_server.send(500, "application/json", "{\"error\":\"format\"}");
      return;
    }
    s_server.send(200, "application/json", body);
  });

  s_server.on("/start", HTTP_GET, []() {
    if (!ENABLE_UPLOAD_TASK) {
      s_server.send(503, "application/json", "{\"error\":\"upload_task_disabled\"}");
      return;
    }
    if (!sd_available()) {
      s_server.send(503, "application/json", "{\"error\":\"sd_unavailable\"}");
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

  s_server.on("/reset_upload_state", HTTP_GET, []() {
    s_abort_requested.store(true, std::memory_order_relaxed);
    const bool ok = reset_upload_state_and_queue();
    s_server.send(ok ? 200 : 500,
                  "application/json",
                  ok ? "{\"ok\":true,\"state_file_removed\":true}" : "{\"ok\":false,\"state_file_removed\":false}");
  });

  s_server.on("/recover_sd", HTTP_GET, []() {
    s_server.send(501, "application/json", "{\"ok\":false,\"reason\":\"startup_only\"}");
  });

  s_server.on("/test/retry_policy", HTTP_GET, []() {
    const bool ok = run_retry_policy_selftest();
    s_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  s_server.on("/test/reachability_probe", HTTP_GET, []() {
    const bool ok = run_reachability_probe_selftest();
    s_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  s_server.on("/test/reachability_cache", HTTP_GET, []() {
    const bool ok = run_reachability_cache_selftest();
    s_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  s_server.on("/test/server_contract", HTTP_GET, []() {
    const bool ok = run_server_contract_selftest();
    s_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  s_server.on("/bookkeeping", HTTP_GET, []() {
    uint32_t uploaded = 0;
    uint32_t outstanding = 0;
    uint32_t queue_ready = 0;
    uint32_t queue_delayed = 0;
    uploaded_state_summary(&uploaded, &outstanding);
    queue_state_summary(&queue_ready, &queue_delayed);
    char body[160];
    const int n = snprintf(body,
                           sizeof(body),
                           "{\"uploaded\":%lu,\"outstanding\":%lu,\"queue_ready\":%lu,\"queue_delayed\":%lu}",
                           static_cast<unsigned long>(uploaded),
                           static_cast<unsigned long>(outstanding),
                           static_cast<unsigned long>(queue_ready),
                           static_cast<unsigned long>(queue_delayed));
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(body)) {
      s_server.send(500, "application/json", "{\"error\":\"format\"}");
      return;
    }
    s_server.send(200, "application/json", body);
  });

  s_server.on("/test/uploaded_state_bookkeeping", HTTP_GET, []() {
    const bool ok = run_uploaded_state_bookkeeping_selftest();
    s_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  s_server.on("/test/recover_sd", HTTP_GET, []() {
    s_server.send(501, "application/json", "{\"ok\":false,\"reason\":\"startup_only\"}");
  });

  s_server.on("/test/reset_upload_state", HTTP_GET, []() {
    const bool ok = run_reset_upload_state_selftest();
    s_server.send(ok ? 200 : 500, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
  });

  s_server.begin();
}
