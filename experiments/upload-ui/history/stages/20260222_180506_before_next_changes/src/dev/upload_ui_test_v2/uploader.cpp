void upload_task(void*) {
  for (;;) {
    uploader_set_current_file_size(0);
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
      // One-shot manual start: enqueue all pending files and continue.
      s_upload_requested.store(false, std::memory_order_relaxed);
      queue_pending();
      vTaskDelay(pdMS_TO_TICKS(20));
      continue;
    }

    s_abort_requested.store(false, std::memory_order_relaxed);
    const uint32_t run_id = s_run_counter.fetch_add(1, std::memory_order_relaxed) + 1;
    s_active_run_id.store(run_id, std::memory_order_relaxed);
    const uint32_t run_start_ms = millis();
    uint32_t retry_after_ms = 0;
    update_stats(UploadState::CONNECTING, 0, 0, 0, 0);

    strncpy(s_active_upload_path, item.path, sizeof(s_active_upload_path) - 1);
    s_active_upload_path[sizeof(s_active_upload_path) - 1] = '\0';

    File file = SD.open(s_active_upload_path, FILE_READ);
    if (!file) {
      if (recover_sd("upload file open")) {
        file = SD.open(s_active_upload_path, FILE_READ);
      }
    }
    if (!file) {
      update_stats(UploadState::ERROR, -10, 0, 0, 0);
      uploader_set_error(false, false, "open_file_failed");
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      portENTER_CRITICAL(&s_queue_mux);
      queue_remove(index);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }
    uint64_t file_bytes = file.size();
    uploader_set_current_file_size(static_cast<uint32_t>(file_bytes));
    String req_headers;
    String req_prefix;
    String req_suffix;
    if (!build_multipart_request(s_target, s_active_upload_path, file_bytes, &req_headers, &req_prefix, &req_suffix)) {
      file.close();
      update_stats(UploadState::ERROR, -16, 0, 0, 0);
      uploader_set_error(false, false, "build_request_failed");
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

    if (!connect_upload_client(client, CONNECT_TIMEOUT_MS)) {
      file.close();
      update_stats(UploadState::ERROR, -11, 0, total, 0);
      uploader_set_error(false, true, "connect_failed");
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
      uploader_set_error(false, false, "send_headers_failed");
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
      uploader_set_error(s_abort_requested.load(std::memory_order_relaxed), false, "stream_failed");
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
      uploader_set_error(true, false, "aborted");
      retryable = true;
    } else if (is_success_contract(status, server_code)) {
      update_stats(UploadState::DONE, 0, total, total, read_stats_snapshot().lastRateBps);
      mark_uploaded_path(s_active_upload_path);
      uploader_note_success(static_cast<uint32_t>(file_bytes));
      uploader_clear_error();
      success = true;
    } else {
      update_stats(UploadState::ERROR, status == 0 ? -15 : -17, read_stats_snapshot().bytesSent, total,
                   read_stats_snapshot().lastRateBps);
      retryable = (status == 0) || is_http_retryable(status);
      if (status == 0) {
        uploader_set_error(false, true, "response_failed");
      } else {
        uploader_set_error(false, false, server_message[0] != '\0' ? server_message : "contract_failed");
      }
      Serial.printf("UPLOAD_CONTRACT_FAIL status=%d code=%s msg=%s\n", status, server_code, server_message);
    }
    print_upload_log("end", run_id, status, millis() - run_start_ms, read_stats_snapshot());
    s_terminal_state_until_ms.store(millis() + 5000UL, std::memory_order_relaxed);
    s_active_run_id.store(0, std::memory_order_relaxed);
    portENTER_CRITICAL(&s_queue_mux);
    if (success) {
      queue_remove(index);
    } else if (retryable) {
      queue_schedule_retry(index, static_cast<uint8_t>(item.retries + 1), retry_after_ms, true);
    } else {
      queue_remove(index);
    }
    portEXIT_CRITICAL(&s_queue_mux);
    s_active_upload_path[0] = '\0';
    uploader_set_current_file_size(0);
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
