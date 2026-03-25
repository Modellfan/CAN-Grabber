

enum class UploadState : uint8_t {
  IDLE = 0,
  CONNECTING = 1,
  SENDING = 2,
  FINALIZING = 3,
  DONE = 4,
  ERROR = 5,
};

struct UploadStats {
  uint64_t bytesSent;
  uint64_t totalBytes;
  uint32_t lastRateBps;
  UploadState state;
  int32_t errorCode;
  uint32_t seq;
};

portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
UploadStats s_stats = {0, 0, 0, UploadState::IDLE, 0, 0};

// Convert upload state enum to a stable human-readable name.
const char* state_name(UploadState state) {
  switch (state) {
    case UploadState::IDLE:
      return "IDLE";
    case UploadState::CONNECTING:
      return "CONNECTING";
    case UploadState::SENDING:
      return "SENDING";
    case UploadState::FINALIZING:
      return "FINALIZING";
    case UploadState::DONE:
      return "DONE";
    case UploadState::ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
  }
}

// Atomically publish latest upload stats snapshot.
void update_stats(UploadState state, int32_t error_code, uint64_t sent, uint64_t total, uint32_t rate) {
  portENTER_CRITICAL(&s_stats_mux);
  s_stats.state = state;
  s_stats.errorCode = error_code;
  s_stats.bytesSent = sent;
  s_stats.totalBytes = total;
  s_stats.lastRateBps = rate;
  s_stats.seq++;
  portEXIT_CRITICAL(&s_stats_mux);
}

// Read the current upload stats atomically.
UploadStats read_stats_snapshot() {
  UploadStats snap{};
  portENTER_CRITICAL(&s_stats_mux);
  snap = s_stats;
  portEXIT_CRITICAL(&s_stats_mux);
  return snap;
}

void print_upload_log(const char* tag, uint32_t run_id, int http_status, uint32_t elapsed_ms, const UploadStats& snap);

//## Constants
constexpr uint32_t kProbeCacheTtlMs = 30000;
constexpr char kUploadPath[] = "/sd_http_post_8mb_01.bin";

//## Type Definition
struct ParsedUrl {
  bool valid;
  uint16_t port;
  char host[96];
  char path[192];
};

struct ProbeStats {
  uint32_t seq;
  bool last_ok;
  uint32_t last_latency_ms;
  uint32_t last_update_ms;
  uint32_t ok_count;
  uint32_t fail_count;
  char last_ip[24];
  char last_error[32];
  uint32_t cache_hits;
  uint32_t cache_misses;
};

ParsedUrl s_target = {};
portMUX_TYPE s_probe_mux = portMUX_INITIALIZER_UNLOCKED;
ProbeStats s_probe_stats = {0, false, 0, 0, 0, 0, "", "", 0, 0};
IPAddress s_probe_cached_ip(0, 0, 0, 0);
uint32_t s_probe_cached_ip_ms = 0;
bool s_probe_cached_ip_valid = false;

ProbeStats read_probe_snapshot() {
  ProbeStats snap{};
  portENTER_CRITICAL(&s_probe_mux);
  snap = s_probe_stats;
  portEXIT_CRITICAL(&s_probe_mux);
  return snap;
}

void update_probe_stats(bool ok, uint32_t latency_ms, const IPAddress& ip, const char* err, bool cache_hit) {
  char ipbuf[24];
  snprintf(ipbuf, sizeof(ipbuf), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
  ipbuf[sizeof(ipbuf) - 1] = '\0';
  portENTER_CRITICAL(&s_probe_mux);
  s_probe_stats.seq++;
  s_probe_stats.last_ok = ok;
  s_probe_stats.last_latency_ms = latency_ms;
  s_probe_stats.last_update_ms = millis();
  if (ok) {
    s_probe_stats.ok_count++;
  } else {
    s_probe_stats.fail_count++;
  }
  strncpy(s_probe_stats.last_ip, ipbuf, sizeof(s_probe_stats.last_ip) - 1);
  s_probe_stats.last_ip[sizeof(s_probe_stats.last_ip) - 1] = '\0';
  const char* e = (err != nullptr) ? err : "";
  strncpy(s_probe_stats.last_error, e, sizeof(s_probe_stats.last_error) - 1);
  s_probe_stats.last_error[sizeof(s_probe_stats.last_error) - 1] = '\0';
  if (cache_hit) {
    s_probe_stats.cache_hits++;
  } else {
    s_probe_stats.cache_misses++;
  }
  portEXIT_CRITICAL(&s_probe_mux);
}

bool resolve_target_no_cache(IPAddress* out_ip) {
  if (out_ip == nullptr || !s_target.valid) {
    return false;
  }
  return WiFi.hostByName(s_target.host, *out_ip);
}

bool resolve_target_cached(IPAddress* out_ip, bool* out_cache_hit) {
  if (out_ip == nullptr || !s_target.valid) {
    return false;
  }
  const uint32_t now_ms = millis();
  bool cache_hit = false;
  if (s_probe_cached_ip_valid && static_cast<int32_t>(now_ms - s_probe_cached_ip_ms) >= 0 &&
      (now_ms - s_probe_cached_ip_ms) <= kProbeCacheTtlMs) {
    *out_ip = s_probe_cached_ip;
    cache_hit = true;
  } else {
    if (!WiFi.hostByName(s_target.host, *out_ip)) {
      if (out_cache_hit != nullptr) {
        *out_cache_hit = false;
      }
      return false;
    }
    s_probe_cached_ip = *out_ip;
    s_probe_cached_ip_ms = now_ms;
    s_probe_cached_ip_valid = true;
  }
  if (out_cache_hit != nullptr) {
    *out_cache_hit = cache_hit;
  }
  return true;
}

bool probe_server_reachability_no_cache(uint32_t timeout_ms) {
  IPAddress ip(0, 0, 0, 0);
  uint32_t start_ms = millis();
  if (!resolve_target_no_cache(&ip)) {
    update_probe_stats(false, millis() - start_ms, ip, "dns", false);
    return false;
  }
  WiFiClient probe;
  if (!probe.connect(ip, s_target.port, timeout_ms)) {
    update_probe_stats(false, millis() - start_ms, ip, "connect", false);
    return false;
  }
  probe.stop();
  update_probe_stats(true, millis() - start_ms, ip, "", false);
  return true;
}

bool probe_server_reachability_cached(uint32_t timeout_ms) {
  IPAddress ip(0, 0, 0, 0);
  bool cache_hit = false;
  uint32_t start_ms = millis();
  if (!resolve_target_cached(&ip, &cache_hit)) {
    update_probe_stats(false, millis() - start_ms, ip, "dns", false);
    return false;
  }
  WiFiClient probe;
  if (!probe.connect(ip, s_target.port, timeout_ms)) {
    update_probe_stats(false, millis() - start_ms, ip, "connect", cache_hit);
    return false;
  }
  probe.stop();
  update_probe_stats(true, millis() - start_ms, ip, "", cache_hit);
  return true;
}

bool parse_url(const char* url, ParsedUrl* out) {
  if (url == nullptr || out == nullptr) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  const char* p = nullptr;
  if (strncmp(url, "http://", 7) == 0) {
    out->port = 80;
    p = url + 7;
  } else {
    return false;
  }
  const char* slash = strchr(p, '/');
  const char* host_end = slash ? slash : p + strlen(p);
  const char* colon = strchr(p, ':');
  if (colon != nullptr && colon < host_end) {
    const size_t host_len = static_cast<size_t>(colon - p);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
      return false;
    }
    memcpy(out->host, p, host_len);
    out->host[host_len] = '\0';
    const long port = strtol(colon + 1, nullptr, 10);
    if (port <= 0 || port > 65535) {
      return false;
    }
    out->port = static_cast<uint16_t>(port);
  } else {
    const size_t host_len = static_cast<size_t>(host_end - p);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
      return false;
    }
    memcpy(out->host, p, host_len);
    out->host[host_len] = '\0';
  }
  if (slash != nullptr) {
    strncpy(out->path, slash, sizeof(out->path) - 1);
  } else {
    strncpy(out->path, "/", sizeof(out->path) - 1);
  }
  out->valid = true;
  return true;
}

bool build_multipart_request(const ParsedUrl& target,
                             const char* upload_path,
                             uint64_t payload_size,
                             String* headers,
                             String* prefix,
                             String* suffix) {
  if (headers == nullptr || prefix == nullptr || suffix == nullptr) {
    return false;
  }
  const uint32_t now_ms = millis();
  String boundary = String("----CANGrabberSpeedTestBoundary") + String(now_ms);
  const char* path = (upload_path != nullptr && upload_path[0] != '\0') ? upload_path : kUploadPath;
  const char* upload_name = strrchr(path, '/');
  upload_name = (upload_name != nullptr && *(upload_name + 1) != '\0') ? (upload_name + 1) : path;
  char idempotency_key[128];
  snprintf(idempotency_key, sizeof(idempotency_key), "v5-%s-%lu", upload_name, static_cast<unsigned long>(payload_size));
  idempotency_key[sizeof(idempotency_key) - 1] = '\0';

  prefix->reserve(512);
  *prefix = "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"bus_id\"\r\n\r\n1\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"start_ms\"\r\n\r\n";
  *prefix += String(now_ms) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"end_ms\"\r\n\r\n";
  *prefix += String(now_ms + 1000UL) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"source\"\r\n\r\nesp32-http-upload-ui-test\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"size_bytes\"\r\n\r\n";
  *prefix += String(static_cast<unsigned long>(payload_size)) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"checksum\"\r\n\r\n0\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"path\"\r\n\r\n";
  *prefix += String(path) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"flags\"\r\n\r\n0\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\nesp32-s3-ui-test\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"updatefile\"; filename=\"";
  *prefix += upload_name;
  *prefix += "\"\r\n";
  *prefix += "Content-Type: application/octet-stream\r\n\r\n";

  *suffix = "\r\n--" + boundary + "--\r\n";

  uint64_t content_len = static_cast<uint64_t>(prefix->length()) + payload_size + static_cast<uint64_t>(suffix->length());
  char host_header[128];
  if (target.port == 80) {
    snprintf(host_header, sizeof(host_header), "%s", target.host);
  } else {
    snprintf(host_header, sizeof(host_header), "%s:%u", target.host, static_cast<unsigned>(target.port));
  }

  headers->reserve(560);
  *headers = "POST ";
  *headers += target.path;
  *headers += " HTTP/1.1\r\n";
  *headers += "Host: ";
  *headers += host_header;
  *headers += "\r\n";
  *headers += "Connection: close\r\n";
  *headers += "Content-Type: multipart/form-data; boundary=";
  *headers += boundary;
  *headers += "\r\n";
  *headers += "X-Idempotency-Key: ";
  *headers += idempotency_key;
  *headers += "\r\n";
  if (strlen(SD_HTTP_UPLOAD_TEST_API_TOKEN) > 0) {
    *headers += "X-Api-Token: ";
    *headers += SD_HTTP_UPLOAD_TEST_API_TOKEN;
    *headers += "\r\n";
  }
  *headers += "Content-Length: ";
  *headers += String(static_cast<unsigned long long>(content_len));
  *headers += "\r\n\r\n";
  return true;
}

bool ensure_single_test_file(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (!sd_available()) {
    return false;
  }
  if (SD.exists(path)) {
    File existing = SD.open(path, FILE_READ);
    if (existing && existing.size() == kCreateBytes) {
      existing.close();
      return true;
    }
    if (existing) {
      existing.close();
    }
    if (!SD.remove(path)) {
      s_sd_available.store(false, std::memory_order_relaxed);
      return false;
    }
  }
  File f = SD.open(path, FILE_WRITE);
  if (!f) {
    s_sd_available.store(false, std::memory_order_relaxed);
    return false;
  }
  static uint8_t block[4096];
  for (size_t i = 0; i < sizeof(block); ++i) {
    block[i] = static_cast<uint8_t>(i & 0xFF);
  }
  size_t written = 0;
  while (written < kCreateBytes) {
    size_t n = min(sizeof(block), kCreateBytes - written);
    if (f.write(block, n) != n) {
      s_sd_available.store(false, std::memory_order_relaxed);
      break;
    }
    written += n;
    if ((written % (1024UL * 1024UL)) == 0) {
      delay(0);
    }
  }
  f.close();
  return written == kCreateBytes;
}

bool ensure_test_file() {
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (!ensure_single_test_file(kTestFilePaths[i])) {
      Serial.print("Failed to prepare SD test file: ");
      Serial.println(kTestFilePaths[i]);
      return false;
    }
  }
  Serial.print("Prepared SD test files: ");
  Serial.println(static_cast<unsigned>(kTestFileCount));
  return true;
}

bool is_http_retryable(int status) {
  if (status == 429 || status == 503) {
    return true;
  }
  if (status >= 500) {
    return true;
  }
  return false;
}

// Write a byte span with fairness/rate limiting and live progress updates.
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

// Stream multipart prefix + file body + suffix to the connected client.
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

// Send prebuilt HTTP request headers, handling partial socket writes.
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

// Parse a Retry-After header value and return milliseconds (or 0).
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

// Read HTTP status line + headers; extracts status code and Retry-After.
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

// Read HTTP meta and optional body text from the server response.
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

// Parse server JSON contract fields ("code", "message") if available.
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

// Validate server business contract for a successful upload outcome.
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

// ====================================================================================================
// Task Section
// ====================================================================================================

// Prefetch task currently disabled until ring/file ownership is thread-safe.
void prefetch_task(void*) {
  // Prefetch path is intentionally disabled until ring/file ownership is made
  // thread-safe (queue + dedicated producer-owned File handle).
  vTaskDelete(nullptr);
}

// Main uploader worker: idle probing, queue dispatch, upload attempt, retry/finalize.
void upload_task(void*) {
  uint32_t last_idle_probe_ms = 0;
  for (;;) {
    const uint32_t now_ms = millis();
    if (!sd_available()) {
      s_upload_requested.store(false, std::memory_order_relaxed);
      update_stats(UploadState::IDLE, -30, 0, 0, 0);
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    QueueItem item = {};
    size_t index = 0;
    portENTER_CRITICAL(&s_queue_mux);
    const QueueItem* queued = queue_snapshot_ready(millis(), &index);
    if (queued != nullptr) {
      item = *queued;
    }
    portEXIT_CRITICAL(&s_queue_mux);

    if (!item.used && !s_upload_requested.load(std::memory_order_relaxed)) {
      if (last_idle_probe_ms == 0 || (now_ms - last_idle_probe_ms) >= 1000UL) {
        probe_server_reachability_cached(1500);
        last_idle_probe_ms = now_ms;
      }
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
      s_sd_available.store(false, std::memory_order_relaxed);
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
    IPAddress connect_ip(0, 0, 0, 0);
    if (!resolve_target_cached(&connect_ip, nullptr)) {
      file.close();
      update_stats(UploadState::ERROR, -19, 0, total, 0);
      print_upload_log("end", run_id, 0, millis() - run_start_ms, read_stats_snapshot());
      s_active_run_id.store(0, std::memory_order_relaxed);
      portENTER_CRITICAL(&s_queue_mux);
      queue_schedule_retry(index, static_cast<uint8_t>(item.retries + 1), 0, true);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    if (!client.connect(connect_ip, s_target.port, CONNECT_TIMEOUT_MS)) {
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

// Monitor connectivity and emit periodic upload progress logs.
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

// ====================================================================================================
// Self Tests
// ====================================================================================================
// Self-test for basic reachability probing.
bool run_reachability_probe_selftest() {
  return probe_server_reachability_cached(2000);
}

// Self-test for cached path; expects at least one cache hit.
bool run_reachability_cache_selftest() {
  bool first = probe_server_reachability_cached(2000);
  bool second = probe_server_reachability_cached(2000);
  if (!first || !second) {
    return false;
  }
  ProbeStats snap = read_probe_snapshot();
  return snap.cache_hits > 0;
}

// Minimal SD self-test used by diagnostics.
bool run_recover_sd_selftest() {
  if (!sd_available()) {
    return false;
  }
  if (SD.cardType() == CARD_NONE) {
    return false;
  }
  if (SD.exists(kTestFilePaths[0])) {
    File f = SD.open(kTestFilePaths[0], FILE_READ);
    if (!f) {
      return false;
    }
    f.close();
  }
  return true;
}

// Self-test uploaded-state mark/read/summary + metadata sidecar creation.
bool run_uploaded_state_bookkeeping_selftest() {
  if (!reset_upload_state_and_queue()) {
    return false;
  }
  const char* path = kTestFilePaths[0];
  if (!mark_uploaded_success(path, kCreateBytes, 201, "UPLOAD_ACCEPTED")) {
    return false;
  }
  if (!uploaded_state_contains(path)) {
    return false;
  }
  uint32_t uploaded = 0;
  uint32_t outstanding = 0;
  uploaded_state_summary(&uploaded, &outstanding);
  if (uploaded != 1 || outstanding != (kTestFileCount - 1)) {
    return false;
  }
  if (!SD.exists(kUploadedMetaPath)) {
    return false;
  }
  return true;
}

// Self-test reset flow by verifying state/meta files were removed.
bool run_reset_upload_state_selftest() {
  if (!reset_upload_state_and_queue()) {
    return false;
  }
  if (SD.exists(kUploadedStatePath)) {
    return false;
  }
  if (SD.exists(kUploadedMetaPath)) {
    return false;
  }
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (uploaded_state_contains(kTestFilePaths[i])) {
      return false;
    }
  }
  return true;
}

// Deterministic self-test for retryability and Retry-After parsing.
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

// Deterministic self-test for server success-contract parsing/validation.
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
