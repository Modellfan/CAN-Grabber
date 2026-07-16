// ====================================================================================================
// Task Section
// ====================================================================================================
// Upload queue state, scheduling, and uploaded-state bookkeeping.

//## Constants
constexpr size_t kQueueLen = 32;
constexpr uint32_t kAutoScanIntervalMs = 4000;
constexpr uint32_t kBaseBackoffMs = 2000;
constexpr uint32_t kMaxBackoffMs = 60000;
constexpr size_t kTestFileCount = 4;
constexpr const char* kTestFilePaths[kTestFileCount] = {
    "/sd_http_post_8mb_01.bin",
    "/sd_http_post_8mb_02.bin",
    "/sd_http_post_8mb_03.bin",
    "/sd_http_post_8mb_04.bin",
};
constexpr char kUploadedStatePath[] = "/upload_state_v5.txt";
constexpr char kUploadedMetaPath[] = "/upload_state_v5_meta.log";
constexpr size_t kCreateBytes = 8UL * 1024UL * 1024UL;

//## Type Definition
struct QueueItem {
  bool used;
  bool manual;
  uint8_t retries;
  uint32_t next_attempt_ms;
  char path[96];
};

QueueItem s_queue[kQueueLen] = {};
portMUX_TYPE s_queue_mux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> s_last_auto_scan_ms{0};

// Check whether a file path is already recorded as uploaded.
bool uploaded_state_contains(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (!sd_available()) {
    return false;
  }
  if (!SD.exists(kUploadedStatePath)) {
    return false;
  }
  File f = SD.open(kUploadedStatePath, FILE_READ);
  if (!f) {
    s_sd_available.store(false, std::memory_order_relaxed);
    return false;
  }
  String line;
  while (f.available() > 0) {
    const char c = static_cast<char>(f.read());
    if (c == '\r') {
      continue;
    }
    if (c == '\n') {
      line.trim();
      if (line.equals(path)) {
        f.close();
        return true;
      }
      line = "";
      continue;
    }
    line += c;
    if (line.length() > 120) {
      line.remove(0);
    }
  }
  line.trim();
  const bool found = line.equals(path);
  f.close();
  return found;
}

// Append a path to uploaded-state file if not already present.
bool mark_uploaded_path(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (!sd_available()) {
    return false;
  }
  if (uploaded_state_contains(path)) {
    return true;
  }
  File f = SD.open(kUploadedStatePath, FILE_APPEND);
  if (!f) {
    s_sd_available.store(false, std::memory_order_relaxed);
    return false;
  }
  const size_t n1 = f.print(path);
  const size_t n2 = f.print("\n");
  f.close();
  return (n1 > 0 && n2 > 0);
}

// Append per-upload metadata record for post-run audit/debug.
bool append_uploaded_metadata(const char* path, uint64_t bytes, int http_status, const char* server_code) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (!sd_available()) {
    return false;
  }
  File f = SD.open(kUploadedMetaPath, FILE_APPEND);
  if (!f) {
    s_sd_available.store(false, std::memory_order_relaxed);
    return false;
  }
  const char* code = (server_code != nullptr) ? server_code : "";
  char line[224];
  const int n = snprintf(line,
                         sizeof(line),
                         "%lu|%s|%llu|%d|%s\n",
                         static_cast<unsigned long>(millis()),
                         path,
                         static_cast<unsigned long long>(bytes),
                         http_status,
                         code);
  bool ok = false;
  if (n > 0 && static_cast<size_t>(n) < sizeof(line)) {
    ok = (f.write(reinterpret_cast<const uint8_t*>(line), static_cast<size_t>(n)) == static_cast<size_t>(n));
  }
  f.close();
  return ok;
}

// Mark path uploaded and persist metadata row in one helper.
bool mark_uploaded_success(const char* path, uint64_t bytes, int http_status, const char* server_code) {
  if (!mark_uploaded_path(path)) {
    return false;
  }
  return append_uploaded_metadata(path, bytes, http_status, server_code);
}

// Count uploaded vs outstanding files based on uploaded-state metadata.
void uploaded_state_summary(uint32_t* uploaded_count, uint32_t* outstanding_count) {
  uint32_t uploaded = 0;
  uint32_t outstanding = 0;
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (uploaded_state_contains(kTestFilePaths[i])) {
      uploaded++;
    } else {
      outstanding++;
    }
  }
  if (uploaded_count != nullptr) {
    *uploaded_count = uploaded;
  }
  if (outstanding_count != nullptr) {
    *outstanding_count = outstanding;
  }
}

// Count queue items split by ready-now vs delayed-retry state.
void queue_state_summary(uint32_t* ready_count, uint32_t* delayed_count) {
  uint32_t ready = 0;
  uint32_t delayed = 0;
  const uint32_t now_ms = millis();
  portENTER_CRITICAL(&s_queue_mux);
  for (size_t i = 0; i < kQueueLen; ++i) {
    if (!s_queue[i].used) {
      continue;
    }
    if (s_queue[i].next_attempt_ms != 0 && static_cast<int32_t>(now_ms - s_queue[i].next_attempt_ms) < 0) {
      delayed++;
    } else {
      ready++;
    }
  }
  portEXIT_CRITICAL(&s_queue_mux);
  if (ready_count != nullptr) {
    *ready_count = ready;
  }
  if (delayed_count != nullptr) {
    *delayed_count = delayed;
  }
}

// Remove one queue slot and reset all of its scheduling fields.
void queue_remove(size_t index) {
  if (index >= kQueueLen) {
    return;
  }
  s_queue[index].used = false;
  s_queue[index].manual = false;
  s_queue[index].retries = 0;
  s_queue[index].next_attempt_ms = 0;
  s_queue[index].path[0] = '\0';
}

// Schedule next attempt for a queue item using backoff/jitter policy.
uint32_t queue_schedule_retry(size_t index, uint8_t retries, uint32_t retry_after_ms, bool add_jitter) {
  if (index >= kQueueLen) {
    return 0;
  }
  uint32_t backoff = retry_after_ms > 0 ? retry_after_ms : kBaseBackoffMs;
  if (retry_after_ms == 0) {
    for (uint8_t i = 1; i < retries; ++i) {
      if (backoff >= (kMaxBackoffMs / 2)) {
        backoff = kMaxBackoffMs;
        break;
      }
      backoff *= 2;
    }
  }
  if (backoff > kMaxBackoffMs) {
    backoff = kMaxBackoffMs;
  }
  if (add_jitter && retry_after_ms == 0) {
    const uint32_t jitter = random(0, 1000);
    backoff = (backoff <= (kMaxBackoffMs - jitter)) ? (backoff + jitter) : kMaxBackoffMs;
  }
  s_queue[index].retries = retries;
  s_queue[index].next_attempt_ms = millis() + backoff;
  return backoff;
}

// Add file path to queue or bump existing entry to immediate retry.
bool queue_add_or_bump(const char* path, bool manual) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  int free_index = -1;
  for (size_t i = 0; i < kQueueLen; ++i) {
    if (s_queue[i].used) {
      if (strcmp(s_queue[i].path, path) == 0) {
        s_queue[i].next_attempt_ms = 0;
        if (manual) {
          s_queue[i].manual = true;
        }
        return true;
      }
    } else if (free_index < 0) {
      free_index = static_cast<int>(i);
    }
  }
  if (free_index < 0) {
    return false;
  }
  QueueItem& item = s_queue[static_cast<size_t>(free_index)];
  strncpy(item.path, path, sizeof(item.path) - 1);
  item.path[sizeof(item.path) - 1] = '\0';
  item.used = true;
  item.manual = manual;
  item.retries = 0;
  item.next_attempt_ms = 0;
  return true;
}

// Return first queue item ready for execution at current time.
const QueueItem* queue_snapshot_ready(uint32_t now_ms, size_t* out_index) {
  if (out_index == nullptr) {
    return nullptr;
  }
  *out_index = 0;
  for (size_t i = 0; i < kQueueLen; ++i) {
    const QueueItem& item = s_queue[i];
    if (!item.used) {
      continue;
    }
    if (item.next_attempt_ms != 0 && static_cast<int32_t>(now_ms - item.next_attempt_ms) < 0) {
      continue;
    }
    *out_index = i;
    return &s_queue[i];
  }
  return nullptr;
}

// Enqueue all non-uploaded test files as pending work.
void queue_pending() {
  if (!sd_available()) {
    return;
  }
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (uploaded_state_contains(kTestFilePaths[i])) {
      continue;
    }
    portENTER_CRITICAL(&s_queue_mux);
    queue_add_or_bump(kTestFilePaths[i], false);
    portEXIT_CRITICAL(&s_queue_mux);
  }
}

// Clear uploaded-state files, reset queue, and reseed pending items.
bool reset_upload_state_and_queue() {
  if (!sd_available()) {
    return false;
  }
  bool state_file_removed = true;
  bool meta_file_removed = true;
  if (SD.exists(kUploadedStatePath)) {
    state_file_removed = SD.remove(kUploadedStatePath);
    if (!state_file_removed) {
      s_sd_available.store(false, std::memory_order_relaxed);
      return false;
    }
  }
  if (SD.exists(kUploadedMetaPath)) {
    meta_file_removed = SD.remove(kUploadedMetaPath);
    if (!meta_file_removed) {
      s_sd_available.store(false, std::memory_order_relaxed);
      return false;
    }
  }

  portENTER_CRITICAL(&s_queue_mux);
  for (size_t i = 0; i < kQueueLen; ++i) {
    queue_remove(i);
  }
  portEXIT_CRITICAL(&s_queue_mux);

  s_last_auto_scan_ms.store(0, std::memory_order_relaxed);
  queue_pending();
  return state_file_removed && meta_file_removed;
}

// Periodically trigger queue reseed to catch new pending files.
void queue_pending_periodic() {
  if (!sd_available()) {
    return;
  }
  const uint32_t now_ms = millis();
  const uint32_t last = s_last_auto_scan_ms.load(std::memory_order_relaxed);
  if (last != 0 && (now_ms - last) < kAutoScanIntervalMs) {
    return;
  }
  s_last_auto_scan_ms.store(now_ms, std::memory_order_relaxed);
  queue_pending();
}

