void prefetch_task(void*) {
  // Prefetch path is intentionally disabled until ring/file ownership is made
  // thread-safe (queue + dedicated producer-owned File handle).
  vTaskDelete(nullptr);
}

bool write_all_fair(WiFiClient& client,
                    const uint8_t* data,
                    size_t len,
                    uint64_t* sent_total,
                    uint64_t total_bytes,
                    uint32_t* rate_bps,
                    uint32_t* rate_window_start_ms,
                    uint64_t* rate_window_start_sent,
                    uint32_t* tokens,
                    uint32_t* last_token_ms) {
  size_t off = 0;
  uint32_t busy_start_us = micros();
  uint32_t last_progress_ms = millis();
  while (off < len && !s_abort_requested.load(std::memory_order_relaxed)) {
    if (MAX_UPLOAD_BPS > 0 && tokens != nullptr && last_token_ms != nullptr) {
      uint32_t now_ms = millis();
      uint32_t dt = now_ms - *last_token_ms;
      if (dt > 0) {
        uint64_t add = (static_cast<uint64_t>(MAX_UPLOAD_BPS) * dt) / 1000ULL;
        *tokens = static_cast<uint32_t>(min<uint64_t>(MAX_UPLOAD_BPS, static_cast<uint64_t>(*tokens) + add));
        *last_token_ms = now_ms;
      }
      if (*tokens == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
    }

    size_t to_send = len - off;
    if (to_send > CHUNK_SIZE) {
      to_send = CHUNK_SIZE;
    }
    if (MAX_UPLOAD_BPS > 0 && tokens != nullptr) {
      to_send = min<size_t>(to_send, *tokens);
      if (to_send == 0) {
        vTaskDelay(pdMS_TO_TICKS(1));
        continue;
      }
    }

    int wrote = client.write(data + off, to_send);
    if (wrote < 0) {
      return false;
    }
    if (wrote == 0) {
      if (!client.connected()) {
        return false;
      }
      if ((millis() - last_progress_ms) > WRITE_STALL_TIMEOUT_MS) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(ZERO_WRITE_RETRY_DELAY_MS));
      continue;
    }

    off += static_cast<size_t>(wrote);
    *sent_total += static_cast<uint64_t>(wrote);
    if (MAX_UPLOAD_BPS > 0 && tokens != nullptr) {
      *tokens -= static_cast<uint32_t>(wrote);
    }
    last_progress_ms = millis();

    uint32_t now_ms = millis();
    if ((now_ms - *rate_window_start_ms) >= STATUS_RATE_WINDOW_MS) {
      uint64_t sent_delta = *sent_total - *rate_window_start_sent;
      uint32_t dt_ms = now_ms - *rate_window_start_ms;
      *rate_bps = (dt_ms > 0) ? static_cast<uint32_t>((sent_delta * 1000ULL) / dt_ms) : 0;
      *rate_window_start_ms = now_ms;
      *rate_window_start_sent = *sent_total;
    }
    update_stats(UploadState::SENDING, 0, *sent_total, total_bytes, *rate_bps);

    taskYIELD();
    if ((micros() - busy_start_us) > UPLOAD_TIMESLICE_US) {
      vTaskDelay(pdMS_TO_TICKS(1));
      busy_start_us = micros();
    }
  }
  return !s_abort_requested.load(std::memory_order_relaxed);
}

bool stream_file_body(WiFiClient& client, File& file, uint64_t total_bytes, const String& prefix, const String& suffix) {
  uint64_t sent_total = 0;
  uint32_t rate_bps = 0;
  uint32_t rate_window_start_ms = millis();
  uint64_t rate_window_start_sent = 0;
  uint32_t tokens = MAX_UPLOAD_BPS;
  uint32_t last_token_ms = millis();

  if (!write_all_fair(client,
                      reinterpret_cast<const uint8_t*>(prefix.c_str()),
                      prefix.length(),
                      &sent_total,
                      total_bytes,
                      &rate_bps,
                      &rate_window_start_ms,
                      &rate_window_start_sent,
                      &tokens,
                      &last_token_ms)) {
    return false;
  }

  if (!USE_PREFETCH_TASK) {
    static uint8_t chunk[CHUNK_SIZE];
    while (!s_abort_requested.load(std::memory_order_relaxed)) {
      size_t n = file.read(chunk, sizeof(chunk));
      if (n == 0) {
        break;
      }
      if (!write_all_fair(client, chunk, n, &sent_total, total_bytes, &rate_bps,
                          &rate_window_start_ms, &rate_window_start_sent, &tokens, &last_token_ms)) {
        return false;
      }
    }
  } else {
    // Safe prefetch producer/consumer queue is not implemented yet.
    return false;
  }

  if (!write_all_fair(client,
                      reinterpret_cast<const uint8_t*>(suffix.c_str()),
                      suffix.length(),
                      &sent_total,
                      total_bytes,
                      &rate_bps,
                      &rate_window_start_ms,
                      &rate_window_start_sent,
                      &tokens,
                      &last_token_ms)) {
    return false;
  }

  update_stats(UploadState::SENDING, 0, sent_total, total_bytes, rate_bps);
  return !s_abort_requested.load(std::memory_order_relaxed);
}

bool send_request_headers(WiFiClient& client, const String& headers) {
  if (headers.length() == 0) {
    return false;
  }
  size_t off = 0;
  const size_t n = headers.length();
  const uint8_t* bytes = reinterpret_cast<const uint8_t*>(headers.c_str());
  while (off < n && !s_abort_requested.load(std::memory_order_relaxed)) {
    int wrote = client.write(bytes + off, n - off);
    if (wrote < 0) {
      return false;
    }
    if (wrote == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    off += static_cast<size_t>(wrote);
    taskYIELD();
  }
  return off == n;
}

int read_http_status(WiFiClient& client) {
  char line[64] = {0};
  size_t idx = 0;
  uint32_t start = millis();
  while ((millis() - start) < RESPONSE_TIMEOUT_MS) {
    while (client.available() > 0) {
      char c = static_cast<char>(client.read());
      if (c == '\n') {
        line[idx] = '\0';
        int code = 0;
        sscanf(line, "HTTP/%*s %d", &code);
        return code;
      }
      if (c != '\r' && idx + 1 < sizeof(line)) {
        line[idx++] = c;
      }
    }
    if (!client.connected()) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return 0;
}

void upload_task(void*) {
  for (;;) {
    if (!s_upload_requested.load(std::memory_order_relaxed)) {
      const uint32_t now_ms = millis();
      const uint32_t hold_until_ms = s_terminal_state_until_ms.load(std::memory_order_relaxed);
      UploadStats held = read_stats_snapshot();
      if ((held.state == UploadState::DONE || held.state == UploadState::ERROR) && hold_until_ms != 0 &&
          static_cast<int32_t>(now_ms - hold_until_ms) < 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }
      UploadStats snap = read_stats_snapshot();
      update_stats(UploadState::IDLE, 0, 0, snap.totalBytes, 0);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }
    s_abort_requested.store(false, std::memory_order_relaxed);
    const uint32_t run_id = s_run_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    s_active_run_id.store(run_id, std::memory_order_relaxed);
    const uint32_t run_start_ms = millis();
    update_stats(UploadState::CONNECTING, 0, 0, 0, 0);

    File file = SD.open(kUploadPath, FILE_READ);
    if (!file) {
      update_stats(UploadState::ERROR, -10, 0, 0, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_upload_requested.store(false, std::memory_order_relaxed);
      continue;
    }
    uint64_t file_bytes = file.size();
    String req_headers;
    String req_prefix;
    String req_suffix;
    if (!build_multipart_request(s_target, file_bytes, &req_headers, &req_prefix, &req_suffix)) {
      file.close();
      update_stats(UploadState::ERROR, -16, 0, 0, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_upload_requested.store(false, std::memory_order_relaxed);
      continue;
    }
    const uint64_t total = static_cast<uint64_t>(req_prefix.length()) + file_bytes + static_cast<uint64_t>(req_suffix.length());
    update_stats(UploadState::CONNECTING, 0, 0, total, 0);
    print_upload_log("start", run_id, 0, millis() - run_start_ms, read_stats_snapshot());

    WiFiClient client;

    if (!client.connect(s_target.host, s_target.port, CONNECT_TIMEOUT_MS)) {
      file.close();
      update_stats(UploadState::ERROR, -11, 0, total, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_upload_requested.store(false, std::memory_order_relaxed);
      continue;
    }

    if (!send_request_headers(client, req_headers)) {
      client.stop();
      file.close();
      update_stats(UploadState::ERROR, -12, 0, total, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_upload_requested.store(false, std::memory_order_relaxed);
      continue;
    }

    bool ok = stream_file_body(client, file, total, req_prefix, req_suffix);
    file.close();
    if (!ok) {
      client.stop();
      update_stats(UploadState::ERROR, -13, read_stats_snapshot().bytesSent, total, read_stats_snapshot().lastRateBps);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_upload_requested.store(false, std::memory_order_relaxed);
      continue;
    }

    update_stats(UploadState::FINALIZING, 0, read_stats_snapshot().bytesSent, total, read_stats_snapshot().lastRateBps);
    client.setTimeout(RESPONSE_TIMEOUT_MS);
    int status = read_http_status(client);
    client.stop();
    if (s_abort_requested.load(std::memory_order_relaxed)) {
      update_stats(UploadState::ERROR, -14, read_stats_snapshot().bytesSent, total, read_stats_snapshot().lastRateBps);
    } else if (status >= 200 && status < 300) {
      update_stats(UploadState::DONE, 0, total, total, read_stats_snapshot().lastRateBps);
    } else {
      update_stats(UploadState::ERROR, status == 0 ? -15 : status, read_stats_snapshot().bytesSent, total,
                   read_stats_snapshot().lastRateBps);
    }
    print_upload_log("end", run_id, status, millis() - run_start_ms, read_stats_snapshot());
    s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
    s_active_run_id.store(0, std::memory_order_relaxed);
    s_upload_requested.store(false, std::memory_order_relaxed);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}

void server_task(void*) {
  for (;;) {
    s_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void monitor_task(void*) {
  uint32_t last_progress_log_ms = 0;
  uint32_t last_progress_seq = 0;
  for (;;) {
    if (WiFi.status() != WL_CONNECTED) {
      connect_wifi();
    }
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
