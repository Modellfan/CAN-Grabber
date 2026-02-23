void configure_http_server() {
  s_server.on("/", HTTP_GET, []() {
    s_server.send_P(200, "text/html", kIndexHtml);
  });

  s_server.on("/status", HTTP_GET, []() {
    UploadStats snap = read_stats_snapshot();
    char body[256];
    int n = snprintf(body, sizeof(body),
                     "{\"sent\":%llu,\"total\":%llu,\"rate\":%u,\"state\":\"%s\",\"error\":%ld}",
                     static_cast<unsigned long long>(snap.bytesSent),
                     static_cast<unsigned long long>(snap.totalBytes),
                     static_cast<unsigned>(snap.lastRateBps),
                     state_name(snap.state),
                     static_cast<long>(snap.errorCode));
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
    s_abort_requested.store(false, std::memory_order_relaxed);
    s_upload_requested.store(true, std::memory_order_relaxed);
    s_server.send(200, "text/plain", "ok");
  });

  s_server.on("/abort", HTTP_GET, []() {
    s_abort_requested.store(true, std::memory_order_relaxed);
    s_server.send(200, "text/plain", "ok");
  });

  s_server.begin();
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
