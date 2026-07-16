// ====================================================================================================
// Task Section
// ====================================================================================================
// Serial command interface for main-loop command handling and diagnostics.
void print_serial_help() {
  Serial.println();
  Serial.println("Serial commands:");
  Serial.println("  help   -> print commands");
  Serial.println("  start  -> start upload");
  Serial.println("  abort  -> abort upload");
  Serial.println("  reset_upload_state -> clear persisted uploaded state and re-queue test files");
  Serial.println("  recover_sd -> disabled at runtime (startup-only)");
  Serial.println("  bookkeeping -> print uploaded/outstanding/queue counters");
  Serial.println("  test_reachability_probe -> run reachability probe self test");
  Serial.println("  test_reachability_cache -> run cached reachability resolution self test");
  Serial.println("  test_retry_policy -> run retry-policy self test");
  Serial.println("  test_server_contract -> run server-contract self test");
  Serial.println("  test_uploaded_state_bookkeeping -> run uploaded-state bookkeeping self test");
  Serial.println("  test_recover_sd -> disabled at runtime (startup-only)");
  Serial.println("  test_reset_upload_state -> run reset-upload-state self test");
  Serial.println("  status -> print current upload stats");
  Serial.println("  reset  -> reboot MCU");
}

void print_status_line() {
  UploadStats snap = read_stats_snapshot();
  Serial.print("state=");
  Serial.print(state_name(snap.state));
  Serial.print(" sent=");
  Serial.print(static_cast<unsigned long long>(snap.bytesSent));
  Serial.print(" total=");
  Serial.print(static_cast<unsigned long long>(snap.totalBytes));
  Serial.print(" rate_bps=");
  Serial.print(snap.lastRateBps);
  Serial.print(" error=");
  Serial.println(static_cast<long>(snap.errorCode));
}

// Emit a compact single-line upload trace record to serial.
void print_upload_log(const char* tag, uint32_t run_id, int http_status, uint32_t elapsed_ms, const UploadStats& snap) {
  Serial.printf("UPLOAD_LOG tag=%s run=%lu ms=%lu state=%s sent=%llu total=%llu rate_bps=%lu error=%ld http=%d rssi=%d heap=%lu\n",
                tag,
                static_cast<unsigned long>(run_id),
                static_cast<unsigned long>(elapsed_ms),
                state_name(snap.state),
                static_cast<unsigned long long>(snap.bytesSent),
                static_cast<unsigned long long>(snap.totalBytes),
                static_cast<unsigned long>(snap.lastRateBps),
                static_cast<long>(snap.errorCode),
                http_status,
                WiFi.RSSI(),
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

// Print runtime task layout and data-flow summary to serial console.
void print_architecture() {
  Serial.println("Architecture:");
  Serial.println(" A) Upload Manager Task: prio 5, core 1. Raw HTTP write loop + fairness.");
  Serial.println(" B) HTTP Server Task: prio 3, core 0. Handles /, /status, /start, /abort.");
  Serial.println(" C) Prefetch Task (optional): prio 2, core 1. Fills fixed ring buffer.");
  Serial.println(" D) Monitor Task: prio 1, core 0. Wi-Fi reconnect/watchdog.");
  Serial.println("Data flow: SD file -> optional ring -> upload loop -> remote server.");
  Serial.println("Fairness: chunked writes, zero-write delay, taskYIELD, 2ms timeslice delay.");
}

// Non-blocking serial parser. Each complete command maps to one action.
void handle_serial() {
  static char cmd[48];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      cmd[len] = '\0';
      if (len > 0) {
        if (strcmp(cmd, "help") == 0) {
          print_serial_help();
        } else if (strcmp(cmd, "start") == 0) {
          if (!ENABLE_UPLOAD_TASK) {
            Serial.println("upload task disabled");
            continue;
          }
          if (!sd_available()) {
            Serial.println("upload blocked: sd unavailable");
            continue;
          }
          s_abort_requested.store(false, std::memory_order_relaxed);
          queue_pending();
          s_upload_requested.store(true, std::memory_order_relaxed);
          Serial.println("upload start requested");
        } else if (strcmp(cmd, "abort") == 0) {
          s_abort_requested.store(true, std::memory_order_relaxed);
          Serial.println("upload abort requested");
        } else if (strcmp(cmd, "reset_upload_state") == 0) {
          s_abort_requested.store(true, std::memory_order_relaxed);
          const bool ok = reset_upload_state_and_queue();
          Serial.println(ok ? "reset_upload_state PASS" : "reset_upload_state FAIL");
        } else if (strcmp(cmd, "recover_sd") == 0) {
          Serial.println("recover_sd disabled: startup-only");
        } else if (strcmp(cmd, "bookkeeping") == 0) {
          uint32_t uploaded = 0;
          uint32_t outstanding = 0;
          uint32_t queue_ready = 0;
          uint32_t queue_delayed = 0;
          uploaded_state_summary(&uploaded, &outstanding);
          queue_state_summary(&queue_ready, &queue_delayed);
          Serial.printf("bookkeeping uploaded=%lu outstanding=%lu queue_ready=%lu queue_delayed=%lu\n",
                        static_cast<unsigned long>(uploaded),
                        static_cast<unsigned long>(outstanding),
                        static_cast<unsigned long>(queue_ready),
                        static_cast<unsigned long>(queue_delayed));
        } else if (strcmp(cmd, "test_reachability_probe") == 0) {
          const bool ok = run_reachability_probe_selftest();
          Serial.println(ok ? "test_reachability_probe PASS" : "test_reachability_probe FAIL");
        } else if (strcmp(cmd, "test_reachability_cache") == 0) {
          const bool ok = run_reachability_cache_selftest();
          Serial.println(ok ? "test_reachability_cache PASS" : "test_reachability_cache FAIL");
        } else if (strcmp(cmd, "test_retry_policy") == 0) {
          const bool ok = run_retry_policy_selftest();
          Serial.println(ok ? "test_retry_policy PASS" : "test_retry_policy FAIL");
        } else if (strcmp(cmd, "test_server_contract") == 0) {
          const bool ok = run_server_contract_selftest();
          Serial.println(ok ? "test_server_contract PASS" : "test_server_contract FAIL");
        } else if (strcmp(cmd, "test_uploaded_state_bookkeeping") == 0) {
          const bool ok = run_uploaded_state_bookkeeping_selftest();
          Serial.println(ok ? "test_uploaded_state_bookkeeping PASS" : "test_uploaded_state_bookkeeping FAIL");
        } else if (strcmp(cmd, "test_recover_sd") == 0) {
          Serial.println("test_recover_sd disabled: startup-only");
        } else if (strcmp(cmd, "test_reset_upload_state") == 0) {
          const bool ok = run_reset_upload_state_selftest();
          Serial.println(ok ? "test_reset_upload_state PASS" : "test_reset_upload_state FAIL");
        } else if (strcmp(cmd, "status") == 0) {
          print_status_line();
        } else if (strcmp(cmd, "reset") == 0) {
          Serial.println("Rebooting MCU...");
          delay(50);
          ESP.restart();
        } else {
          print_serial_help();
        }
      }
      len = 0;
    } else if (len + 1 < sizeof(cmd)) {
      cmd[len++] = c;
    }
  }
}
