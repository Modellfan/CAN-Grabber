void monitor_task(void*) {
  uint32_t last_progress_log_ms = 0;
  uint32_t last_progress_seq = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connect_wifi();
    }
    probe_server_reachability(false);
    queue_pending_periodic();
    refresh_uploader_contract_stats();
    UploadStats snap = read_stats_snapshot();
    const uint32_t now_ms = millis();
    const UploadState st = snap.state;
    if ((st == UploadState::SENDING || st == UploadState::FINALIZING) && (now_ms - last_progress_log_ms) >= SERIAL_PROGRESS_INTERVAL_MS &&
        (snap.seq != last_progress_seq)) {
      const uint32_t run_id = s_active_run_id.load(std::memory_order_relaxed);
      if (run_id != 0) {
        print_upload_log("progress", run_id, 0, 0, snap);
      }
      last_progress_log_ms = now_ms;
      last_progress_seq = snap.seq;
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

void print_architecture() {
  Serial.println("Architecture:");
  Serial.println(" A) Upload Manager Task: prio 5, core 1. Raw HTTP write loop + fairness.");
  Serial.println(" B) HTTP Server Task: prio 3, core 0. Handles /, /status, /start, /abort.");
  Serial.println(" C) Prefetch Task (optional): prio 2, core 1. Fills fixed ring buffer.");
  Serial.println(" D) Monitor Task: prio 1, core 0. Wi-Fi reconnect/watchdog.");
  Serial.println("Data flow: SD file -> optional ring -> upload loop -> remote server.");
  Serial.println("Fairness: chunked writes, zero-write delay, taskYIELD, 2ms timeslice delay.");
}

void print_serial_help() {
  Serial.println();
  Serial.println("Serial commands:");
  Serial.println("  help   -> print commands");
  Serial.println("  start  -> start upload");
  Serial.println("  abort  -> abort upload");
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

void handle_serial() {
  static char cmd[24];
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
          s_abort_requested.store(false, std::memory_order_relaxed);
          s_upload_requested.store(true, std::memory_order_relaxed);
          Serial.println("upload start requested");
        } else if (strcmp(cmd, "abort") == 0) {
          s_abort_requested.store(true, std::memory_order_relaxed);
          Serial.println("upload abort requested");
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

} // namespace

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("SD HTTP upload + local reactive UI test");

  if (!parse_url(SD_HTTP_UPLOAD_TEST_URL, &s_target) || !s_target.valid) {
    Serial.print("Invalid SD_HTTP_UPLOAD_TEST_URL: ");
    Serial.println(SD_HTTP_UPLOAD_TEST_URL);
    return;
  }
  if (!connect_wifi()) {
    Serial.println("Wi-Fi connect failed");
    return;
  }
  Serial.print("Wi-Fi IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("Upload target: ");
  Serial.println(SD_HTTP_UPLOAD_TEST_URL);
  probe_server_reachability(true);

  if (!init_sd()) {
    Serial.println("SD init failed");
    return;
  }
  if (!ensure_test_file()) {
    Serial.println("Failed to create/validate test file");
    return;
  }
  uploader_set_initialized(true);
  uploader_clear_error();
  queue_pending();
  refresh_uploader_contract_stats();

  configure_http_server();
  print_architecture();
  print_serial_help();

  if (ENABLE_UPLOAD_TASK) {
    xTaskCreatePinnedToCore(upload_task, "upload_mgr", 8192, nullptr, UPLOAD_TASK_PRIORITY, &s_upload_task_handle, UPLOAD_CORE);
  }
  xTaskCreatePinnedToCore(server_task, "http_srv_evt", 4096, nullptr, SERVER_TASK_PRIORITY, &s_server_task_handle, SERVER_CORE);
  if (USE_PREFETCH_TASK) {
    xTaskCreatePinnedToCore(prefetch_task, "io_prefetch", 4096, nullptr, PREFETCH_TASK_PRIORITY, &s_prefetch_task_handle,
                            UPLOAD_CORE);
  }
  xTaskCreatePinnedToCore(monitor_task, "net_monitor", 3072, nullptr, MONITOR_TASK_PRIORITY, &s_monitor_task_handle, SERVER_CORE);
}

void loop() {
  handle_serial();
  delay(20);
}
