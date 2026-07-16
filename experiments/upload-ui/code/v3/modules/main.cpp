void monitor_task(void*) {
  uint32_t last_progress_log_ms = 0;
  uint32_t last_progress_seq = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connect_wifi();
    }
    probe_server_reachability(false);

    UploadStats snap = read_stats_snapshot();
    const uint32_t now_ms = millis();
    if ((snap.state == UploadState::SENDING || snap.state == UploadState::FINALIZING) &&
        (now_ms - last_progress_log_ms) >= kSerialProgressIntervalMs &&
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
  Serial.println("Architecture (v3):");
  Serial.println(" A) CAN RX tasks: core 0, prio MAX-2 (from can_manager).");
  Serial.println(" B) Storage IO arbiter: core 1, prio MAX-4. ONLY task using SD/File.");
  Serial.println(" C) Upload network sender: core 1, prio MAX-5. WiFiClient only.");
  Serial.println(" D) HTTP server task: core 1, prio 4.");
  Serial.println(" E) Monitor task: core 1, prio 2.");
  Serial.println("Arbitration: GREEN/YELLOW/RED by max CAN queue_depth backlog with hysteresis.");
}

void print_serial_help() {
  Serial.println();
  Serial.println("Serial commands:");
  Serial.println("  help     -> print commands");
  Serial.println("  start    -> start uploads");
  Serial.println("  abort    -> abort upload stream");
  Serial.println("  status   -> print current stats");
  Serial.println("  fps <n>  -> set simulated CAN fps");
  Serial.println("  reset    -> reboot MCU");
}

void print_status_line() {
  UploadStats snap = read_stats_snapshot();
  Serial.printf("state=%s sent=%llu total=%llu rate_bps=%lu error=%ld zone=%s backlog=%lu log_bps=%lu\n",
                state_name(snap.state),
                static_cast<unsigned long long>(snap.bytes_sent),
                static_cast<unsigned long long>(snap.total_bytes),
                static_cast<unsigned long>(snap.rate_bps),
                static_cast<long>(snap.error_code),
                zone_name(s_backlog_zone.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(s_last_backlog_bytes.load(std::memory_order_relaxed)),
                static_cast<unsigned long>(s_log_bytes_per_sec.load(std::memory_order_relaxed)));
}

void handle_serial() {
  static char cmd[40];
  static size_t len = 0;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      cmd[len] = '\0';
      if (len > 0) {
        if (strcmp(cmd, "help") == 0) {
          print_serial_help();
        } else if (strcmp(cmd, "start") == 0) {
          s_abort_requested.store(false, std::memory_order_relaxed);
          queue_pending_scan();
          s_upload_requested.store(true, std::memory_order_relaxed);
          Serial.println("upload start requested");
        } else if (strcmp(cmd, "abort") == 0) {
          s_abort_requested.store(true, std::memory_order_relaxed);
          stream_set_request_stop();
          Serial.println("upload abort requested");
        } else if (strcmp(cmd, "status") == 0) {
          print_status_line();
        } else if (strncmp(cmd, "fps ", 4) == 0) {
          const uint32_t fps = static_cast<uint32_t>(strtoul(cmd + 4, nullptr, 10));
          can::set_load_test_fps(fps);
          s_sim_fps.store(fps, std::memory_order_relaxed);
          Serial.printf("sim fps set to %lu\n", static_cast<unsigned long>(fps));
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

void setup() {
  Serial.begin(115200);
  delay(250);
  Serial.println();
  Serial.println("SD HTTP upload + CAN/logger integration UI test v3");

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
  if (!ensure_test_files()) {
    Serial.println("Failed to create/validate test files");
    return;
  }

  s_free_chunks = xQueueCreate(CHUNK_COUNT, sizeof(uint8_t));
  s_filled_chunks = xQueueCreate(CHUNK_COUNT, sizeof(ChunkPacket));
  s_mark_uploaded_q = xQueueCreate(16, sizeof(MarkUploadedReq));
  if (s_free_chunks == nullptr || s_filled_chunks == nullptr || s_mark_uploaded_q == nullptr) {
    Serial.println("Queue allocation failed");
    return;
  }
  for (uint8_t i = 0; i < CHUNK_COUNT; ++i) {
    xQueueSend(s_free_chunks, &i, 0);
  }

  config::init();
  config::Config& cfg = config::get_mutable();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    cfg.buses[i].enabled = true;
    cfg.buses[i].logging = true;
  }
  can::init();
  can::set_load_test_fps(1000);
  s_sim_fps.store(1000, std::memory_order_relaxed);

  queue_pending_scan();
  configure_http_server();
  print_architecture();
  print_serial_help();

  xTaskCreatePinnedToCore(storage_io_task, "storage_io", 8192, nullptr, STORAGE_TASK_PRIO, &s_storage_task_handle, STORAGE_CORE);
  xTaskCreatePinnedToCore(upload_network_task, "upload_net", 8192, nullptr, UPLOAD_NET_TASK_PRIO, &s_upload_net_task_handle,
                          UPLOAD_NET_CORE);
  xTaskCreatePinnedToCore(server_task, "http_srv_evt", 4096, nullptr, SERVER_TASK_PRIO, &s_server_task_handle, SERVER_CORE);
  xTaskCreatePinnedToCore(monitor_task, "net_monitor", 4096, nullptr, MONITOR_TASK_PRIO, &s_monitor_task_handle, MONITOR_CORE);
}

void loop() {
  handle_serial();
  delay(20);
}
