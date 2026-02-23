void rotate_log_file_if_needed(uint8_t bus_id) {
  LogFileState& state = s_log_files[bus_id];
  if (!state.active) {
    char path[80];
    const uint32_t now_ms = millis();
    snprintf(path, sizeof(path), "/v3_log_%lu_bus%u.sav", static_cast<unsigned long>(now_ms), static_cast<unsigned>(bus_id));
    state.file = SD.open(path, FILE_WRITE);
    if (!state.file) {
      return;
    }
    state.active = true;
    state.start_ms = now_ms;
    state.bytes_written = 0;
    strncpy(state.path, path, sizeof(state.path) - 1);
    state.path[sizeof(state.path) - 1] = '\0';
    const char* header = "# SavvyCAN ASCII log\n";
    const size_t hlen = strlen(header);
    if (state.file.write(reinterpret_cast<const uint8_t*>(header), hlen) == hlen) {
      state.bytes_written += hlen;
    }
  }

  const uint32_t max_size = config::get().global.max_file_size_bytes;
  if (max_size > 0 && state.bytes_written >= max_size) {
    state.file.flush();
    state.file.close();
    state.active = false;
  }
}

void drain_log_blocks(bool aggressive) {
  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    uint32_t per_bus_written = 0;
    for (;;) {
      can::LogBlock block{};
      if (!can::acquire_log_block(bus_id, &block)) {
        break;
      }
      rotate_log_file_if_needed(bus_id);
      LogFileState& state = s_log_files[bus_id];
      size_t out = 0;
      if (state.active) {
        out = state.file.write(block.data, block.len);
        if (out == block.len) {
          state.bytes_written += out;
          s_log_total_bytes.fetch_add(out, std::memory_order_relaxed);
          s_log_pop_count.fetch_add(block.frames, std::memory_order_relaxed);
        }
      }
      can::release_log_block(bus_id, block.index, (out == block.len) ? block.frames : 0);
      per_bus_written += static_cast<uint32_t>(out);
      if (!aggressive && per_bus_written >= CHUNK_SIZE) {
        break;
      }
      taskYIELD();
    }
  }
}

bool process_mark_uploaded_queue() {
  if (s_mark_uploaded_q == nullptr) {
    return false;
  }
  bool any = false;
  MarkUploadedReq req = {};
  while (xQueueReceive(s_mark_uploaded_q, &req, 0) == pdTRUE) {
    mark_uploaded_path(req.path);
    any = true;
  }
  return any;
}

void maybe_scan_pending_files() {
  const uint32_t now_ms = millis();
  const uint32_t last_ms = s_last_auto_scan_ms.load(std::memory_order_relaxed);
  if (last_ms != 0 && (now_ms - last_ms) < kAutoScanIntervalMs) {
    return;
  }
  s_last_auto_scan_ms.store(now_ms, std::memory_order_relaxed);
  queue_pending_scan();
}

bool fill_upload_chunk_if_allowed(BacklogZone zone) {
  StreamControl stream = stream_snapshot();
  static File file;
  static bool file_open = false;

  if (stream.request_stop) {
    if (file_open) {
      file.close();
      file_open = false;
    }
    stream_clear_all();
    return false;
  }

  if (stream.request_open && !file_open) {
    file = SD.open(stream.path, FILE_READ);
    if (!file) {
      stream_clear_all();
      return false;
    }
    file_open = true;
    stream_set_active(true);
  }

  if (!file_open || zone == BacklogZone::RED) {
    return false;
  }

  uint8_t idx = 0;
  if (xQueueReceive(s_free_chunks, &idx, 0) != pdTRUE) {
    return false;
  }

  const size_t n = file.read(s_chunk_pool[idx], CHUNK_SIZE);
  ChunkPacket pkt = {};
  pkt.index = idx;
  pkt.len = static_cast<uint16_t>(n);
  pkt.eof = (n == 0) ? 1 : 0;
  if (xQueueSend(s_filled_chunks, &pkt, 0) != pdTRUE) {
    xQueueSend(s_free_chunks, &idx, 0);
    return false;
  }

  if (n == 0) {
    file.close();
    file_open = false;
    stream_set_active(false);
  }
  return true;
}

void storage_io_task(void*) {
  uint32_t last_log_window_ms = millis();
  uint64_t last_log_total = 0;
  for (;;) {
    uint32_t backlog = 0;
    for (uint8_t bus = 0; bus < config::kMaxBuses; ++bus) {
      backlog = max(backlog, can::queue_depth(bus));
    }
    BacklogZone zone = select_backlog_zone(backlog);

    if (zone == BacklogZone::RED) {
      drain_log_blocks(true);
    } else if (zone == BacklogZone::YELLOW) {
      drain_log_blocks(false);
      fill_upload_chunk_if_allowed(zone);
    } else {
      drain_log_blocks(false);
      while (fill_upload_chunk_if_allowed(zone)) {
        taskYIELD();
      }
    }

    process_mark_uploaded_queue();
    maybe_scan_pending_files();

    const uint32_t now_ms = millis();
    if ((now_ms - last_log_window_ms) >= 1000) {
      const uint64_t total = s_log_total_bytes.load(std::memory_order_relaxed);
      const uint64_t delta = total - last_log_total;
      s_log_bytes_per_sec.store(static_cast<uint32_t>(delta), std::memory_order_relaxed);
      last_log_total = total;
      last_log_window_ms = now_ms;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

bool write_all_fair(WiFiClient& client,
                    const uint8_t* data,
                    size_t len,
                    uint64_t* sent_total,
                    uint64_t total_bytes,
                    uint32_t* rate_bps,
                    uint32_t* rate_window_start_ms,
                    uint64_t* rate_window_start_sent) {
  size_t off = 0;
  uint32_t busy_start_us = micros();
  uint32_t last_progress_ms = millis();
  while (off < len && !s_abort_requested.load(std::memory_order_relaxed)) {
    size_t to_send = min(static_cast<size_t>(CHUNK_SIZE), len - off);
    int wrote = client.write(data + off, to_send);
    if (wrote < 0) {
      return false;
    }
    if (wrote == 0) {
      if (!client.connected()) {
        return false;
      }
      if ((millis() - last_progress_ms) > kWriteStallTimeoutMs) {
        return false;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    off += static_cast<size_t>(wrote);
    *sent_total += static_cast<uint64_t>(wrote);
    last_progress_ms = millis();

    const uint32_t now_ms = millis();
    if ((now_ms - *rate_window_start_ms) >= kStatusRateWindowMs) {
      const uint64_t sent_delta = *sent_total - *rate_window_start_sent;
      const uint32_t dt_ms = now_ms - *rate_window_start_ms;
      *rate_bps = (dt_ms > 0) ? static_cast<uint32_t>((sent_delta * 1000ULL) / dt_ms) : 0;
      *rate_window_start_ms = now_ms;
      *rate_window_start_sent = *sent_total;
    }
    update_stats(UploadState::SENDING, 0, *sent_total, total_bytes, *rate_bps);

    taskYIELD();
    if ((micros() - busy_start_us) > kUploadTimesliceUs) {
      vTaskDelay(pdMS_TO_TICKS(1));
      busy_start_us = micros();
    }
  }
  return !s_abort_requested.load(std::memory_order_relaxed);
}

bool send_request_headers(WiFiClient& client, const String& headers) {
  const uint8_t* data = reinterpret_cast<const uint8_t*>(headers.c_str());
  size_t off = 0;
  while (off < headers.length()) {
    int wrote = client.write(data + off, headers.length() - off);
    if (wrote < 0) {
      return false;
    }
    if (wrote == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }
    off += static_cast<size_t>(wrote);
  }
  return true;
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

bool read_http_response(WiFiClient& client, int* out_status, uint32_t* out_retry_after_ms, String* out_body) {
  if (out_status == nullptr || out_retry_after_ms == nullptr || out_body == nullptr) {
    return false;
  }
  *out_status = 0;
  *out_retry_after_ms = 0;
  out_body->remove(0);
  String line;
  bool got_status = false;
  uint32_t start = millis();
  while ((millis() - start) < kResponseTimeoutMs) {
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
        } else if (line.length() == 0) {
          while (client.connected() || client.available()) {
            while (client.available()) {
              out_body->concat(static_cast<char>(client.read()));
            }
            vTaskDelay(pdMS_TO_TICKS(1));
          }
          return true;
        } else {
          const uint32_t retry_after = parse_retry_after_ms(line);
          if (retry_after > 0) {
            *out_retry_after_ms = retry_after;
          }
        }
        line = "";
      } else {
        line += c;
      }
    }
    if (!client.connected() && !client.available()) {
      return got_status;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
  return got_status;
}

void parse_server_json(const String& body, char* out_code, size_t out_code_len, char* out_msg, size_t out_msg_len) {
  if (out_code != nullptr && out_code_len > 0) {
    out_code[0] = '\0';
  }
  if (out_msg != nullptr && out_msg_len > 0) {
    out_msg[0] = '\0';
  }
  JsonDocument doc;
  if (deserializeJson(doc, body)) {
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

void upload_network_task(void*) {
  for (;;) {
    char path[96] = {0};
    if (!s_upload_requested.load(std::memory_order_relaxed)) {
      const uint32_t now_ms = millis();
      const uint32_t hold_until_ms = s_terminal_state_until_ms.load(std::memory_order_relaxed);
      UploadStats held = read_stats_snapshot();
      if ((held.state == UploadState::DONE || held.state == UploadState::ERROR) && hold_until_ms != 0 &&
          static_cast<int32_t>(now_ms - hold_until_ms) < 0) {
        vTaskDelay(pdMS_TO_TICKS(20));
        continue;
      }
      update_stats(UploadState::IDLE, 0, 0, held.total_bytes, 0);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    portENTER_CRITICAL(&s_queue_mux);
    const bool has_item = queue_pop_next(path, sizeof(path));
    portEXIT_CRITICAL(&s_queue_mux);
    if (!has_item) {
      s_upload_requested.store(false, std::memory_order_relaxed);
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    s_abort_requested.store(false, std::memory_order_relaxed);
    const uint32_t run_id = s_run_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    s_active_run_id.store(run_id, std::memory_order_relaxed);
    const uint32_t run_start_ms = millis();

    File f = SD.open(path, FILE_READ);
    uint64_t file_size = 0;
    if (f) {
      file_size = static_cast<uint64_t>(f.size());
      f.close();
    }
    if (file_size == 0) {
      update_stats(UploadState::ERROR, -10, 0, 0, 0);
      s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
      s_active_run_id.store(0, std::memory_order_relaxed);
      continue;
    }

    strncpy(s_active_upload_path, path, sizeof(s_active_upload_path) - 1);
    s_active_upload_path[sizeof(s_active_upload_path) - 1] = '\0';

    String req_headers;
    String req_prefix;
    String req_suffix;
    if (!build_multipart_request(s_target, path, file_size, &req_headers, &req_prefix, &req_suffix)) {
      update_stats(UploadState::ERROR, -16, 0, 0, 0);
      s_active_run_id.store(0, std::memory_order_relaxed);
      continue;
    }

    const uint64_t total = static_cast<uint64_t>(req_prefix.length()) + file_size + static_cast<uint64_t>(req_suffix.length());
    update_stats(UploadState::CONNECTING, 0, 0, total, 0);
    print_upload_log("start", run_id, 0, millis() - run_start_ms, read_stats_snapshot());

    WiFiClient client;
    if (!connect_upload_client(client, kConnectTimeoutMs)) {
      update_stats(UploadState::ERROR, -11, 0, total, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
      continue;
    }

    if (!send_request_headers(client, req_headers)) {
      client.stop();
      update_stats(UploadState::ERROR, -12, 0, total, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
      continue;
    }

    uint64_t sent_total = 0;
    uint32_t rate_bps = 0;
    uint32_t rate_window_start_ms = millis();
    uint64_t rate_window_start_sent = 0;

    if (!write_all_fair(client,
                        reinterpret_cast<const uint8_t*>(req_prefix.c_str()),
                        req_prefix.length(),
                        &sent_total,
                        total,
                        &rate_bps,
                        &rate_window_start_ms,
                        &rate_window_start_sent)) {
      client.stop();
      update_stats(UploadState::ERROR, -13, sent_total, total, rate_bps);
      s_active_run_id.store(0, std::memory_order_relaxed);
      continue;
    }

    stream_set_request_open(path, file_size);
    bool stream_ok = true;
    while (!s_abort_requested.load(std::memory_order_relaxed)) {
      ChunkPacket pkt{};
      if (xQueueReceive(s_filled_chunks, &pkt, pdMS_TO_TICKS(1000)) != pdTRUE) {
        stream_ok = false;
        break;
      }
      if (pkt.eof) {
        if (pkt.index < CHUNK_COUNT) {
          xQueueSend(s_free_chunks, &pkt.index, 0);
        }
        break;
      }
      if (pkt.index >= CHUNK_COUNT || pkt.len == 0 || pkt.len > CHUNK_SIZE) {
        stream_ok = false;
        break;
      }
      if (!write_all_fair(client,
                          s_chunk_pool[pkt.index],
                          pkt.len,
                          &sent_total,
                          total,
                          &rate_bps,
                          &rate_window_start_ms,
                          &rate_window_start_sent)) {
        stream_ok = false;
        xQueueSend(s_free_chunks, &pkt.index, 0);
        break;
      }
      xQueueSend(s_free_chunks, &pkt.index, 0);
    }
    stream_set_request_stop();

    if (!stream_ok) {
      client.stop();
      update_stats(UploadState::ERROR, -14, sent_total, total, rate_bps);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
      continue;
    }

    if (!write_all_fair(client,
                        reinterpret_cast<const uint8_t*>(req_suffix.c_str()),
                        req_suffix.length(),
                        &sent_total,
                        total,
                        &rate_bps,
                        &rate_window_start_ms,
                        &rate_window_start_sent)) {
      client.stop();
      update_stats(UploadState::ERROR, -15, sent_total, total, rate_bps);
      s_active_run_id.store(0, std::memory_order_relaxed);
      s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
      continue;
    }

    update_stats(UploadState::FINALIZING, 0, sent_total, total, rate_bps);
    client.setTimeout(kResponseTimeoutMs);
    int status = 0;
    uint32_t retry_after_ms = 0;
    String response_body;
    read_http_response(client, &status, &retry_after_ms, &response_body);
    (void)retry_after_ms;
    char server_code[40] = {0};
    char server_message[96] = {0};
    parse_server_json(response_body, server_code, sizeof(server_code), server_message, sizeof(server_message));
    client.stop();

    if (is_success_contract(status, server_code)) {
      update_stats(UploadState::DONE, 0, total, total, rate_bps);
      enqueue_mark_uploaded(path);
    } else {
      update_stats(UploadState::ERROR, (status == 0) ? -17 : -18, sent_total, total, rate_bps);
      Serial.printf("UPLOAD_CONTRACT_FAIL status=%d code=%s msg=%s\n", status, server_code, server_message);
    }

    print_upload_log("end", run_id, status, millis() - run_start_ms, read_stats_snapshot());
    s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
    s_active_run_id.store(0, std::memory_order_relaxed);
    s_active_upload_path[0] = '\0';
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
