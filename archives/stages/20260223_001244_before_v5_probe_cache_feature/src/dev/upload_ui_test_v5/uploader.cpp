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

uint32_t parse_retry_after_ms(const String& header_line) {
  constexpr const char* kPrefix = "retry-after:";
  String lower = header_line;
  lower.toLowerCase();
  if (!lower.startsWith(kPrefix)) {
    return 0;
  }
  String value = header_line.substring(strlen(kPrefix));
  value.trim();
  const long seconds = value.toInt();
  if (seconds <= 0) {
    return 0;
  }
  return static_cast<uint32_t>(seconds) * 1000UL;
}

bool read_http_response_meta(WiFiClient& client, int* out_status, uint32_t* out_retry_after_ms) {
  if (out_status == nullptr || out_retry_after_ms == nullptr) {
    return false;
  }
  *out_status = 0;
  *out_retry_after_ms = 0;

  String line;
  bool got_status = false;
  uint32_t start = millis();
  while ((millis() - start) < RESPONSE_TIMEOUT_MS) {
    while (client.available() > 0) {
      const char c = static_cast<char>(client.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        line.trim();
        if (!got_status) {
          int code = 0;
          sscanf(line.c_str(), "HTTP/%*s %d", &code);
          *out_status = code;
          got_status = true;
        } else {
          if (line.length() == 0) {
            return true;
          }
          const uint32_t retry_after_ms = parse_retry_after_ms(line);
          if (retry_after_ms > 0) {
            *out_retry_after_ms = retry_after_ms;
          }
        }
        line = "";
        continue;
      }
      line += c;
    }
    if (!client.connected() && !client.available()) {
      return got_status;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return got_status;
}

bool read_http_response(WiFiClient& client, int* out_status, uint32_t* out_retry_after_ms, String* out_body) {
  if (!read_http_response_meta(client, out_status, out_retry_after_ms)) {
    return false;
  }
  if (out_body == nullptr) {
    return true;
  }
  out_body->reserve(256);
  uint32_t start = millis();
  uint32_t last_data = millis();
  while ((millis() - start) < RESPONSE_TIMEOUT_MS) {
    bool got_data = false;
    while (client.available() > 0) {
      const char c = static_cast<char>(client.read());
      out_body->concat(c);
      got_data = true;
    }
    if (got_data) {
      last_data = millis();
    }
    if (!client.connected() && !client.available()) {
      return true;
    }
    if ((millis() - last_data) > RESPONSE_TIMEOUT_MS) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(2));
  }
  return true;
}

void parse_server_json(const String& body, char* out_code, size_t out_code_len, char* out_msg, size_t out_msg_len) {
  if (out_code != nullptr && out_code_len > 0) {
    out_code[0] = '\0';
  }
  if (out_msg != nullptr && out_msg_len > 0) {
    out_msg[0] = '\0';
  }
  if (body.isEmpty()) {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return;
  }
  const char* code = doc["code"] | "";
  const char* msg = doc["message"] | "";
  if (out_code != nullptr && out_code_len > 1) {
    strncpy(out_code, code, out_code_len - 1);
    out_code[out_code_len - 1] = '\0';
  }
  if (out_msg != nullptr && out_msg_len > 1) {
    strncpy(out_msg, msg, out_msg_len - 1);
    out_msg[out_msg_len - 1] = '\0';
  }
}

bool is_success_contract(int status, const char* server_code) {
  if (server_code == nullptr) {
    return false;
  }
  if (status == 201 && strcmp(server_code, "UPLOAD_ACCEPTED") == 0) {
    return true;
  }
  if (status == 200 &&
      (strcmp(server_code, "DUPLICATE_CONTENT") == 0 || strcmp(server_code, "DUPLICATE_IDEMPOTENCY_KEY") == 0)) {
    return true;
  }
  return false;
}

bool run_retry_policy_selftest() {
  bool ok = true;
  struct Case {
    int status;
    bool expected_retryable;
  };
  const Case cases[] = {
      {429, true},
      {503, true},
      {500, true},
      {502, true},
      {201, false},
      {400, false},
      {404, false},
  };
  for (const auto& c : cases) {
    const bool got = is_http_retryable(c.status);
    if (got != c.expected_retryable) {
      ok = false;
      Serial.printf("RETRY_TEST fail status=%d got=%d expected=%d\n", c.status, got ? 1 : 0, c.expected_retryable ? 1 : 0);
    }
  }
  const uint32_t ra1 = parse_retry_after_ms(String("Retry-After: 3"));
  const uint32_t ra2 = parse_retry_after_ms(String("retry-after: 10"));
  const uint32_t ra3 = parse_retry_after_ms(String("x-retry: 1"));
  if (ra1 != 3000 || ra2 != 10000 || ra3 != 0) {
    ok = false;
    Serial.printf("RETRY_TEST retry_after fail ra1=%lu ra2=%lu ra3=%lu\n",
                  static_cast<unsigned long>(ra1),
                  static_cast<unsigned long>(ra2),
                  static_cast<unsigned long>(ra3));
  }
  Serial.printf("RETRY_TEST result=%s\n", ok ? "PASS" : "FAIL");
  return ok;
}

bool run_server_contract_selftest() {
  bool ok = true;
  if (!is_success_contract(201, "UPLOAD_ACCEPTED")) {
    ok = false;
  }
  if (!is_success_contract(200, "DUPLICATE_CONTENT")) {
    ok = false;
  }
  if (!is_success_contract(200, "DUPLICATE_IDEMPOTENCY_KEY")) {
    ok = false;
  }
  if (is_success_contract(201, "WRONG_CODE")) {
    ok = false;
  }
  if (is_success_contract(200, "UPLOAD_ACCEPTED")) {
    ok = false;
  }
  char code[40] = {0};
  char msg[96] = {0};
  parse_server_json(String("{\"code\":\"UPLOAD_ACCEPTED\",\"message\":\"ok\"}"), code, sizeof(code), msg, sizeof(msg));
  if (strcmp(code, "UPLOAD_ACCEPTED") != 0 || strcmp(msg, "ok") != 0) {
    ok = false;
  }
  Serial.printf("CONTRACT_TEST result=%s\n", ok ? "PASS" : "FAIL");
  return ok;
}

void upload_task(void*) {
  for (;;) {
    QueueItem item = {};
    size_t index = 0;
    portENTER_CRITICAL(&s_queue_mux);
    const QueueItem* queued = queue_snapshot_ready(millis(), &index);
    if (queued != nullptr) {
      item = *queued;
    }
    portEXIT_CRITICAL(&s_queue_mux);

    if (!item.used && !s_upload_requested.load(std::memory_order_relaxed)) {
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

    if (!item.used && s_upload_requested.load(std::memory_order_relaxed)) {
      // Manual start seeds queue with all pending files.
      s_upload_requested.store(false, std::memory_order_relaxed);
      queue_pending();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    s_abort_requested.store(false, std::memory_order_relaxed);
    const uint32_t run_id = s_run_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    s_active_run_id.store(run_id, std::memory_order_relaxed);
    const uint32_t run_start_ms = millis();
    update_stats(UploadState::CONNECTING, 0, 0, 0, 0);

    File file = SD.open(item.path, FILE_READ);
    if (!file) {
      if (recover_sd("upload_task/open_item")) {
        file = SD.open(item.path, FILE_READ);
      }
    }
    if (!file) {
      update_stats(UploadState::ERROR, -10, 0, 0, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      portENTER_CRITICAL(&s_queue_mux);
      queue_remove(index);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }
    uint64_t file_bytes = file.size();
    String req_headers;
    String req_prefix;
    String req_suffix;
    if (!build_multipart_request(s_target, item.path, file_bytes, &req_headers, &req_prefix, &req_suffix)) {
      file.close();
      update_stats(UploadState::ERROR, -16, 0, 0, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      portENTER_CRITICAL(&s_queue_mux);
      queue_remove(index);
      portEXIT_CRITICAL(&s_queue_mux);
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
      portENTER_CRITICAL(&s_queue_mux);
      queue_schedule_retry(index, static_cast<uint8_t>(item.retries + 1), 0, true);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    if (!send_request_headers(client, req_headers)) {
      client.stop();
      file.close();
      update_stats(UploadState::ERROR, -12, 0, total, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      portENTER_CRITICAL(&s_queue_mux);
      queue_schedule_retry(index, static_cast<uint8_t>(item.retries + 1), 0, true);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    bool ok = stream_file_body(client, file, total, req_prefix, req_suffix);
    file.close();
    if (!ok) {
      client.stop();
      update_stats(UploadState::ERROR, -13, read_stats_snapshot().bytesSent, total, read_stats_snapshot().lastRateBps);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      portENTER_CRITICAL(&s_queue_mux);
      queue_schedule_retry(index, static_cast<uint8_t>(item.retries + 1), 0, true);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    update_stats(UploadState::FINALIZING, 0, read_stats_snapshot().bytesSent, total, read_stats_snapshot().lastRateBps);
    client.setTimeout(RESPONSE_TIMEOUT_MS);
    int status = 0;
    uint32_t retry_after_ms = 0;
    String response_body;
    read_http_response(client, &status, &retry_after_ms, &response_body);
    char server_code[40] = {0};
    char server_message[96] = {0};
    parse_server_json(response_body, server_code, sizeof(server_code), server_message, sizeof(server_message));
    client.stop();
    bool success = false;
    bool retryable = false;
    if (s_abort_requested.load(std::memory_order_relaxed)) {
      update_stats(UploadState::ERROR, -14, read_stats_snapshot().bytesSent, total, read_stats_snapshot().lastRateBps);
      retryable = true;
    } else if (is_success_contract(status, server_code)) {
      update_stats(UploadState::DONE, 0, total, total, read_stats_snapshot().lastRateBps);
      mark_uploaded_success(item.path, file_bytes, status, server_code);
      success = true;
    } else {
      update_stats(UploadState::ERROR, status == 0 ? -15 : -17, read_stats_snapshot().bytesSent, total,
                   read_stats_snapshot().lastRateBps);
      retryable = (status == 0) || is_http_retryable(status);
      Serial.printf("UPLOAD_CONTRACT_FAIL status=%d code=%s msg=%s\n", status, server_code, server_message);
    }
    print_upload_log("end", run_id, status, millis() - run_start_ms, read_stats_snapshot());
    s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
    s_active_run_id.store(0, std::memory_order_relaxed);
    s_upload_requested.store(false, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_queue_mux);
    if (success) {
      queue_remove(index);
    } else if (retryable) {
      queue_schedule_retry(index, static_cast<uint8_t>(item.retries + 1), retry_after_ms, true);
    } else {
      queue_remove(index);
    }
    portEXIT_CRITICAL(&s_queue_mux);
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
    queue_pending_periodic();
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
