} // namespace

// ====================================================================================================
// Task Section
// ====================================================================================================
// Boot sequence: validate config, initialize Wi-Fi/SD state, then start tasks.
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

  if (!init_sd()) {
    Serial.println("SD init failed; storage unavailable");
    Serial.println("Please power-reset the device and check SD card seating.");
  } else if (!ensure_test_file()) {
    s_sd_available.store(false, std::memory_order_relaxed);
    Serial.println("Failed to create/validate test file; storage unavailable");
    Serial.println("Please power-reset the device and check SD card seating.");
  } else {
    queue_pending();
  }

  configure_http_server();
  print_architecture();
  print_serial_help();

  // Task model:
  // - upload_task: queue processing + HTTP upload
  // - server_task: HTTP endpoints/UI
  // - monitor_task: connectivity + periodic queue scan
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

// Main loop is reserved for serial command polling.
void loop() {
  handle_serial();
  delay(20);
}
