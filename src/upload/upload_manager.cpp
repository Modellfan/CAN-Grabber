#include "upload/upload_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <SD.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config/app_config.h"
#include "storage/storage_manager.h"

namespace upload {

namespace {

struct QueueItem {
  char path[64];
  uint8_t retries;
  uint32_t next_attempt_ms;
  bool manual;
  bool used;
};

struct ParsedUrl {
  char host[64];
  char path[96];
  uint16_t port;
  bool valid;
};

enum class UploadError : uint8_t {
  kNone = 0,
  kInvalidUrl,
  kMissingFile,
  kOpenFailed,
  kConnectFailed,
  kReadFailed,
  kWriteFailed,
  kResponseTimeout,
  kBadStatusLine,
  kHttpStatusFailed,
  kRejectedResponse,
};

struct UploadResult {
  bool ok;
  UploadError error;
  bool interrupted;
  bool connect_problem;
  bool retryable;
  uint32_t sent_bytes;
  int http_status;
  uint32_t retry_after_ms;
  char server_code[48];
  char server_message[96];
};

constexpr size_t kQueueLen = 32;
constexpr uint32_t kBaseBackoffMs = 5000;
constexpr uint32_t kMaxBackoffMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kTaskSleepMs = 500;
constexpr uint32_t kPostAttemptSleepMs = 300;
constexpr uint32_t kPostConnectProblemSleepMs = 2000;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kResponseTimeoutMs = 10000;
constexpr uint32_t kWriteTimeoutMs = 8000;
constexpr size_t kUploadFileReadChunkBytes = 2048;
constexpr size_t kUploadWriteChunkBytes = 512;
constexpr uint32_t kUploadWriteBusyBudgetUs = 350;
#ifndef UPLOAD_DEBUG_WRITE
#define UPLOAD_DEBUG_WRITE 1
#endif
#ifndef UPLOAD_DEBUG_FLOW
#define UPLOAD_DEBUG_FLOW 1
#endif
constexpr size_t kMaxResponseBodyBytes = 2048;
constexpr uint32_t kPendingScanIntervalMs = 30000;
constexpr uint32_t kReachabilityProbeIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kReachabilityProbeFailBackoffMs = 10UL * 60UL * 1000UL;
constexpr uint32_t kResolvedIpTtlMs = 10UL * 60UL * 1000UL;
constexpr UBaseType_t kUploadTaskPriority = tskIDLE_PRIORITY + 1;
constexpr BaseType_t kUploadTaskCore = 1;

QueueItem s_queue[kQueueLen];
TaskHandle_t s_task = nullptr;
portMUX_TYPE s_queue_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_initialized = false;
uint32_t s_last_pending_scan_ms = 0;
portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
uint32_t s_uploaded_files_total = 0;
uint64_t s_uploaded_bytes_total = 0;
uint32_t s_speed_window_start_ms = 0;
uint32_t s_speed_window_bytes = 0;
uint32_t s_upload_speed_bps = 0;
bool s_uploading = false;
uint32_t s_current_file_size = 0;
uint32_t s_current_file_sent = 0;
bool s_last_error = false;
bool s_last_error_interrupted = false;
bool s_last_error_connect = false;
char s_last_error_message[96] = "";
bool s_server_reachable_known = false;
bool s_server_reachable = false;
int32_t s_server_rtt_ms = -1;
uint32_t s_last_probe_ms = 0;
uint32_t s_next_probe_ms = 0;
char s_server_reach_message[96] = "";
IPAddress s_cached_host_ip;
bool s_cached_host_ip_valid = false;
uint32_t s_cached_host_ip_ms = 0;
char s_cached_host[64] = "";
IPAddress s_last_probe_ip;
bool s_last_probe_ip_valid = false;
uint32_t s_last_probe_ip_ms = 0;
char s_last_probe_host[64] = "";

void note_sent_bytes(uint32_t bytes);

const QueueItem* queue_snapshot_ready(uint32_t now_ms, size_t* out_index) {
  const QueueItem* best = nullptr;
  size_t best_index = 0;
  for (size_t i = 0; i < kQueueLen; ++i) {
    const QueueItem& item = s_queue[i];
    if (!item.used) {
      continue;
    }
    if (now_ms < item.next_attempt_ms) {
      continue;
    }
    best = &item;
    best_index = i;
    break;
  }
  if (out_index != nullptr) {
    *out_index = best_index;
  }
  return best;
}

bool parse_http_url(const char* url, ParsedUrl* out) {
  if (url == nullptr || out == nullptr) {
    return false;
  }
  out->host[0] = '\0';
  out->path[0] = '\0';
  out->port = 80;
  out->valid = false;

  constexpr const char* kPrefix = "http://";
  if (strncmp(url, kPrefix, strlen(kPrefix)) != 0) {
    return false;
  }

  const char* p = url + strlen(kPrefix);
  if (*p == '\0') {
    return false;
  }

  const char* slash = strchr(p, '/');
  const char* host_end = (slash != nullptr) ? slash : (p + strlen(p));
  const char* colon = strchr(p, ':');
  if (colon != nullptr && colon < host_end) {
    const size_t host_len = static_cast<size_t>(colon - p);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
      return false;
    }
    strncpy(out->host, p, host_len);
    out->host[host_len] = '\0';
    const unsigned long port = strtoul(colon + 1, nullptr, 10);
    if (port == 0 || port > 65535) {
      return false;
    }
    out->port = static_cast<uint16_t>(port);
  } else {
    const size_t host_len = static_cast<size_t>(host_end - p);
    if (host_len == 0 || host_len >= sizeof(out->host)) {
      return false;
    }
    strncpy(out->host, p, host_len);
    out->host[host_len] = '\0';
  }

  if (slash != nullptr) {
    strncpy(out->path, slash, sizeof(out->path));
    out->path[sizeof(out->path) - 1] = '\0';
  } else {
    strncpy(out->path, "/", sizeof(out->path));
    out->path[sizeof(out->path) - 1] = '\0';
  }

  out->valid = true;
  return true;
}

bool ends_with_local_domain(const char* host) {
  if (host == nullptr) {
    return false;
  }
  const size_t len = strlen(host);
  if (len < 6) {
    return false;
  }
  return strcmp(host + (len - 6), ".local") == 0;
}

void cache_host_ip(const char* host, const IPAddress& ip) {
  if (host == nullptr || host[0] == '\0') {
    return;
  }
  strncpy(s_cached_host, host, sizeof(s_cached_host));
  s_cached_host[sizeof(s_cached_host) - 1] = '\0';
  s_cached_host_ip = ip;
  s_cached_host_ip_valid = true;
  s_cached_host_ip_ms = millis();
}

bool get_cached_host_ip(const char* host, IPAddress* out_ip) {
  if (!s_cached_host_ip_valid || host == nullptr || out_ip == nullptr) {
    return false;
  }
  if (strcmp(s_cached_host, host) != 0) {
    return false;
  }
  const uint32_t age = millis() - s_cached_host_ip_ms;
  if (age > kResolvedIpTtlMs) {
    return false;
  }
  *out_ip = s_cached_host_ip;
  return true;
}

void cache_last_probe_ip(const char* host, const IPAddress& ip) {
  if (host == nullptr || host[0] == '\0') {
    return;
  }
  strncpy(s_last_probe_host, host, sizeof(s_last_probe_host));
  s_last_probe_host[sizeof(s_last_probe_host) - 1] = '\0';
  s_last_probe_ip = ip;
  s_last_probe_ip_valid = true;
  s_last_probe_ip_ms = millis();
}

void invalidate_last_probe_ip(const char* host) {
  if (host == nullptr || host[0] == '\0') {
    s_last_probe_ip_valid = false;
    s_last_probe_host[0] = '\0';
    return;
  }
  if (s_last_probe_ip_valid && strcmp(s_last_probe_host, host) == 0) {
    s_last_probe_ip_valid = false;
    s_last_probe_host[0] = '\0';
  }
}

bool get_last_probe_ip(const char* host, IPAddress* out_ip) {
  if (!s_last_probe_ip_valid || host == nullptr || out_ip == nullptr) {
    return false;
  }
  if (strcmp(s_last_probe_host, host) != 0) {
    return false;
  }
  const uint32_t age = millis() - s_last_probe_ip_ms;
  if (age > kResolvedIpTtlMs) {
    return false;
  }
  bool server_reachable = false;
  portENTER_CRITICAL(&s_stats_mux);
  server_reachable = s_server_reachable_known && s_server_reachable;
  portEXIT_CRITICAL(&s_stats_mux);
  if (!server_reachable) {
    return false;
  }
  *out_ip = s_last_probe_ip;
  return true;
}

bool resolve_upload_host(const char* host, IPAddress* out_ip, char* out_method, size_t out_method_len) {
  if (host == nullptr || out_ip == nullptr) {
    return false;
  }
  if (out_method != nullptr && out_method_len > 0) {
    out_method[0] = '\0';
  }

  IPAddress ip;
  if (ip.fromString(host)) {
    *out_ip = ip;
    if (out_method != nullptr && out_method_len > 0) {
      strncpy(out_method, "literal-ip", out_method_len);
      out_method[out_method_len - 1] = '\0';
    }
    return true;
  }

  if (get_cached_host_ip(host, &ip)) {
    *out_ip = ip;
    if (out_method != nullptr && out_method_len > 0) {
      strncpy(out_method, "cached-ip", out_method_len);
      out_method[out_method_len - 1] = '\0';
    }
    return true;
  }

  if (ends_with_local_domain(host)) {
    String short_name(host);
    short_name.remove(short_name.length() - 6);
    if (short_name.length() > 0) {
      IPAddress mdns_ip = MDNS.queryHost(short_name.c_str());
      if (mdns_ip != INADDR_NONE) {
        *out_ip = mdns_ip;
        cache_host_ip(host, mdns_ip);
        if (out_method != nullptr && out_method_len > 0) {
          strncpy(out_method, "mdns", out_method_len);
          out_method[out_method_len - 1] = '\0';
        }
        return true;
      }
    }
  }

  if (WiFi.hostByName(host, ip)) {
    *out_ip = ip;
    cache_host_ip(host, ip);
    if (out_method != nullptr && out_method_len > 0) {
      strncpy(out_method, "dns", out_method_len);
      out_method[out_method_len - 1] = '\0';
    }
    return true;
  }

  return false;
}

bool read_line_with_timeout(WiFiClient& client,
                            String* out,
                            uint32_t timeout_ms) {
  if (out == nullptr) {
    return false;
  }
  out->remove(0);
  const uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    while (client.available()) {
      const char c = static_cast<char>(client.read());
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        return true;
      }
      out->concat(c);
    }
    if (!client.connected()) {
      return out->length() > 0;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return false;
}

bool write_all_with_timeout(WiFiClient& client,
                            const uint8_t* data,
                            size_t len,
                            uint32_t timeout_ms,
                            const char* stage) {
  if (data == nullptr) {
    return false;
  }
  size_t offset = 0;
  uint32_t last_progress = millis();
  uint32_t last_debug_log = 0;
  uint32_t busy_window_start_us = micros();
  while (offset < len) {
    if (!client.connected()) {
#if UPLOAD_DEBUG_WRITE
      Serial.printf(
          "[UPLOAD][#debug] write stage=%s disconnected offset=%lu/%lu wifi=%d avail=%d\n",
          stage != nullptr ? stage : "?",
          static_cast<unsigned long>(offset),
          static_cast<unsigned long>(len),
          static_cast<int>(WiFi.status()),
          client.availableForWrite());
#endif
      return false;
    }
    if (millis() - last_progress > timeout_ms) {
#if UPLOAD_DEBUG_WRITE
      Serial.printf(
          "[UPLOAD][#debug] write stage=%s timeout offset=%lu/%lu wifi=%d connected=%d avail=%d\n",
          stage != nullptr ? stage : "?",
          static_cast<unsigned long>(offset),
          static_cast<unsigned long>(len),
          static_cast<int>(WiFi.status()),
          client.connected() ? 1 : 0,
          client.availableForWrite());
#endif
      return false;
    }
    const int writable = client.availableForWrite();
    if (writable <= 0) {
#if UPLOAD_DEBUG_WRITE
      if (last_debug_log == 0 || (millis() - last_debug_log) >= 250) {
        Serial.printf(
            "[UPLOAD][#debug] write stage=%s stalled offset=%lu/%lu wifi=%d connected=%d avail=%d\n",
            stage != nullptr ? stage : "?",
            static_cast<unsigned long>(offset),
            static_cast<unsigned long>(len),
            static_cast<int>(WiFi.status()),
            client.connected() ? 1 : 0,
            writable);
        last_debug_log = millis();
      }
#endif
      // Some ESP32 core / socket states may report availableForWrite()==0 even
      // though a small write can still make progress. Fall through and try.
    }
    const size_t remaining = static_cast<size_t>(len - offset);
    const size_t chunk = (writable > 0)
                             ? min(min(static_cast<size_t>(writable), remaining), kUploadWriteChunkBytes)
                             : min(kUploadWriteChunkBytes, remaining);
    const size_t written = client.write(data + offset, chunk);
    if (written == 0) {
#if UPLOAD_DEBUG_WRITE
      if (last_debug_log == 0 || (millis() - last_debug_log) >= 250) {
        Serial.printf(
            "[UPLOAD][#debug] write stage=%s zero-write offset=%lu/%lu wifi=%d connected=%d avail=%d chunk=%lu\n",
            stage != nullptr ? stage : "?",
            static_cast<unsigned long>(offset),
            static_cast<unsigned long>(len),
            static_cast<int>(WiFi.status()),
            client.connected() ? 1 : 0,
            client.availableForWrite(),
            static_cast<unsigned long>(chunk));
        last_debug_log = millis();
      }
#endif
      vTaskDelay(pdMS_TO_TICKS(2));
      busy_window_start_us = micros();
      continue;
    }
    offset += written;
    last_progress = millis();
#if UPLOAD_DEBUG_WRITE
    Serial.printf(
        "[UPLOAD][#debug] write stage=%s progress written=%lu offset=%lu/%lu avail=%d connected=%d\n",
        stage != nullptr ? stage : "?",
        static_cast<unsigned long>(written),
        static_cast<unsigned long>(offset),
        static_cast<unsigned long>(len),
        client.availableForWrite(),
        client.connected() ? 1 : 0);
#endif
    if ((micros() - busy_window_start_us) >= kUploadWriteBusyBudgetUs) {
      vTaskDelay(pdMS_TO_TICKS(1));
      busy_window_start_us = micros();
    }
  }
  return true;
}

bool write_file_chunk_with_timeout(WiFiClient& client,
                                   const uint8_t* data,
                                   size_t len,
                                   uint32_t timeout_ms,
                                   uint32_t* out_written,
                                   const char* stage) {
  if (data == nullptr) {
    return false;
  }
  size_t offset = 0;
  uint32_t last_progress = millis();
  uint32_t last_debug_log = 0;
  uint32_t busy_window_start_us = micros();
  while (offset < len) {
    if (!client.connected()) {
#if UPLOAD_DEBUG_WRITE
      Serial.printf(
          "[UPLOAD][#debug] write stage=%s disconnected offset=%lu/%lu wifi=%d avail=%d\n",
          stage != nullptr ? stage : "?",
          static_cast<unsigned long>(offset),
          static_cast<unsigned long>(len),
          static_cast<int>(WiFi.status()),
          client.availableForWrite());
#endif
      return false;
    }
    if (millis() - last_progress > timeout_ms) {
#if UPLOAD_DEBUG_WRITE
      Serial.printf(
          "[UPLOAD][#debug] write stage=%s timeout offset=%lu/%lu wifi=%d connected=%d avail=%d\n",
          stage != nullptr ? stage : "?",
          static_cast<unsigned long>(offset),
          static_cast<unsigned long>(len),
          static_cast<int>(WiFi.status()),
          client.connected() ? 1 : 0,
          client.availableForWrite());
#endif
      return false;
    }
    const int writable = client.availableForWrite();
    if (writable <= 0) {
#if UPLOAD_DEBUG_WRITE
      if (last_debug_log == 0 || (millis() - last_debug_log) >= 250) {
        Serial.printf(
            "[UPLOAD][#debug] write stage=%s stalled offset=%lu/%lu wifi=%d connected=%d avail=%d\n",
            stage != nullptr ? stage : "?",
            static_cast<unsigned long>(offset),
            static_cast<unsigned long>(len),
            static_cast<int>(WiFi.status()),
            client.connected() ? 1 : 0,
            writable);
        last_debug_log = millis();
      }
#endif
      // Some ESP32 core / socket states may report availableForWrite()==0 even
      // though a small write can still make progress. Fall through and try.
    }
    const size_t remaining = static_cast<size_t>(len - offset);
    const size_t chunk = (writable > 0)
                             ? min(min(static_cast<size_t>(writable), remaining), kUploadWriteChunkBytes)
                             : min(kUploadWriteChunkBytes, remaining);
    const size_t written = client.write(data + offset, chunk);
    if (written == 0) {
#if UPLOAD_DEBUG_WRITE
      if (last_debug_log == 0 || (millis() - last_debug_log) >= 250) {
        Serial.printf(
            "[UPLOAD][#debug] write stage=%s zero-write offset=%lu/%lu wifi=%d connected=%d avail=%d chunk=%lu\n",
            stage != nullptr ? stage : "?",
            static_cast<unsigned long>(offset),
            static_cast<unsigned long>(len),
            static_cast<int>(WiFi.status()),
            client.connected() ? 1 : 0,
            client.availableForWrite(),
            static_cast<unsigned long>(chunk));
        last_debug_log = millis();
      }
#endif
      vTaskDelay(pdMS_TO_TICKS(2));
      busy_window_start_us = micros();
      continue;
    }
    offset += written;
    last_progress = millis();
    if (out_written != nullptr) {
      *out_written += static_cast<uint32_t>(written);
    }
    note_sent_bytes(static_cast<uint32_t>(written));
#if UPLOAD_DEBUG_WRITE
    if (offset == len || last_debug_log == 0 || (millis() - last_debug_log) >= 250) {
      Serial.printf(
          "[UPLOAD][#debug] write stage=%s progress written=%lu offset=%lu/%lu avail=%d connected=%d\n",
          stage != nullptr ? stage : "?",
          static_cast<unsigned long>(written),
          static_cast<unsigned long>(offset),
          static_cast<unsigned long>(len),
          client.availableForWrite(),
          client.connected() ? 1 : 0);
      last_debug_log = millis();
    }
#endif
    if ((micros() - busy_window_start_us) >= kUploadWriteBusyBudgetUs) {
      vTaskDelay(pdMS_TO_TICKS(1));
      busy_window_start_us = micros();
    }
  }
  return true;
}

void set_server_reachability(bool known,
                             bool reachable,
                             int32_t rtt_ms,
                             const char* message) {
  portENTER_CRITICAL(&s_stats_mux);
  s_server_reachable_known = known;
  s_server_reachable = reachable;
  s_server_rtt_ms = rtt_ms;
  if (message != nullptr) {
    strncpy(s_server_reach_message, message, sizeof(s_server_reach_message));
    s_server_reach_message[sizeof(s_server_reach_message) - 1] = '\0';
  } else {
    s_server_reach_message[0] = '\0';
  }
  portEXIT_CRITICAL(&s_stats_mux);
}

bool reachability_msg_is_wifi_not_connected() {
  bool is_match = false;
  portENTER_CRITICAL(&s_stats_mux);
  is_match = (strncmp(s_server_reach_message,
                      "wifi not connected",
                      sizeof(s_server_reach_message)) == 0);
  portEXIT_CRITICAL(&s_stats_mux);
  return is_match;
}

bool queue_has_pending_items() {
  bool has_pending = false;
  portENTER_CRITICAL(&s_queue_mux);
  for (size_t i = 0; i < kQueueLen; ++i) {
    if (s_queue[i].used) {
      has_pending = true;
      break;
    }
  }
  portEXIT_CRITICAL(&s_queue_mux);
  return has_pending;
}

bool should_probe_reachability() {
  if (s_uploading) {
    return true;
  }
  if (queue_has_pending_items()) {
    return true;
  }
  return false;
}

void probe_upload_server_reachability() {
  ParsedUrl parsed{};
  if (!parse_http_url(config::get().global.upload_url, &parsed) || !parsed.valid) {
    set_server_reachability(true, false, -1, "invalid upload URL");
    return;
  }

  const uint32_t now = millis();
  const bool wifi_connected = (WiFi.status() == WL_CONNECTED);
  const bool force_reprobe_after_reconnect =
      wifi_connected && reachability_msg_is_wifi_not_connected();
  if (!should_probe_reachability()) {
    return;
  }
  if (!force_reprobe_after_reconnect && s_next_probe_ms != 0 && now < s_next_probe_ms) {
    return;
  }
  if (!force_reprobe_after_reconnect && s_last_probe_ms != 0 &&
      (now - s_last_probe_ms) < kReachabilityProbeIntervalMs) {
    return;
  }
  s_last_probe_ms = now;
  s_next_probe_ms = now + kReachabilityProbeIntervalMs;
  if (!wifi_connected) {
    invalidate_last_probe_ip(parsed.host);
    set_server_reachability(true, false, -1, "wifi not connected");
    return;
  }

  IPAddress resolved;
  char method[16];
  if (!resolve_upload_host(parsed.host, &resolved, method, sizeof(method))) {
    char msg[96];
    snprintf(msg, sizeof(msg), "resolve failed for %s", parsed.host);
    msg[sizeof(msg) - 1] = '\0';
    invalidate_last_probe_ip(parsed.host);
    set_server_reachability(true, false, -1, msg);
    s_next_probe_ms = now + kReachabilityProbeFailBackoffMs;
    return;
  }

  WiFiClient probe;
  const uint32_t start_ms = millis();
  if (!probe.connect(resolved, parsed.port, 1200)) {
    char msg[96];
    snprintf(msg, sizeof(msg), "TCP connect failed %s:%u", parsed.host,
             static_cast<unsigned>(parsed.port));
    msg[sizeof(msg) - 1] = '\0';
    invalidate_last_probe_ip(parsed.host);
    set_server_reachability(true, false, -1, msg);
    s_next_probe_ms = now + kReachabilityProbeFailBackoffMs;
    return;
  }
  const int32_t rtt = static_cast<int32_t>(millis() - start_ms);
  probe.stop();
  cache_last_probe_ip(parsed.host, resolved);
  char msg[96];
  snprintf(msg,
           sizeof(msg),
           "reachable %s:%u via %s (%d ms)",
           resolved.toString().c_str(),
           static_cast<unsigned>(parsed.port),
           method,
           static_cast<int>(rtt));
  msg[sizeof(msg) - 1] = '\0';
  set_server_reachability(true, true, rtt, msg);
}

bool read_body_with_timeout(WiFiClient& client,
                            String* out,
                            uint32_t timeout_ms,
                            size_t max_len) {
  if (out == nullptr) {
    return false;
  }
  out->remove(0);
  const uint32_t start = millis();
  uint32_t last_data = start;
  while ((millis() - start) < timeout_ms) {
    bool got_data = false;
    while (client.available()) {
      const char c = static_cast<char>(client.read());
      if (out->length() < max_len) {
        out->concat(c);
      }
      got_data = true;
    }
    if (got_data) {
      last_data = millis();
    }
    if (!client.connected() && !client.available()) {
      return true;
    }
    if ((millis() - last_data) > timeout_ms) {
      return false;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
  }
  return !out->isEmpty();
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

void parse_server_json(const String& body, UploadResult* result) {
  if (result == nullptr || body.isEmpty()) {
    return;
  }
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    return;
  }
  const char* code = doc["code"] | "";
  const char* message = doc["message"] | "";
  strncpy(result->server_code, code, sizeof(result->server_code));
  result->server_code[sizeof(result->server_code) - 1] = '\0';
  strncpy(result->server_message, message, sizeof(result->server_message));
  result->server_message[sizeof(result->server_message) - 1] = '\0';
}

bool is_success_contract(const UploadResult& result) {
  if (result.http_status == 201 &&
      strcmp(result.server_code, "UPLOAD_ACCEPTED") == 0) {
    return true;
  }
  if (result.http_status == 200 &&
      (strcmp(result.server_code, "DUPLICATE_CONTENT") == 0 ||
       strcmp(result.server_code, "DUPLICATE_IDEMPOTENCY_KEY") == 0)) {
    return true;
  }
  return false;
}

bool is_http_retryable(const UploadResult& result) {
  if (result.http_status == 429 || result.http_status == 503) {
    return true;
  }
  if (result.http_status >= 500) {
    return true;
  }
  return false;
}

void build_idempotency_key(const storage::FileInfo& info,
                           const char* filename,
                           char* out,
                           size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return;
  }
  snprintf(out,
           out_len,
           "bus%u-%lu-%lu-%s-%lu-%lu",
           static_cast<unsigned>(info.bus_id),
           static_cast<unsigned long>(info.start_ms),
           static_cast<unsigned long>(info.end_ms),
           filename,
           static_cast<unsigned long>(info.size_bytes),
           static_cast<unsigned long>(info.checksum));
  out[out_len - 1] = '\0';
}

void note_sent_bytes(uint32_t bytes) {
  const uint32_t now = millis();
  portENTER_CRITICAL(&s_stats_mux);
  if (s_uploading) {
    s_current_file_sent += bytes;
  }
  if (s_speed_window_start_ms == 0) {
    s_speed_window_start_ms = now;
  }
  s_speed_window_bytes += bytes;
  const uint32_t elapsed = now - s_speed_window_start_ms;
  if (elapsed >= 1000) {
    s_upload_speed_bps =
        static_cast<uint32_t>((s_speed_window_bytes * 1000ULL) / elapsed);
    s_speed_window_bytes = 0;
    s_speed_window_start_ms = now;
  }
  portEXIT_CRITICAL(&s_stats_mux);
}

void set_last_error(const UploadResult& result) {
  const UploadError error = result.error;
  const int http_status = result.http_status;
  const char* msg = "unknown";
  switch (error) {
    case UploadError::kNone:
      msg = "";
      break;
    case UploadError::kInvalidUrl:
      msg = "invalid upload URL";
      break;
    case UploadError::kMissingFile:
      msg = "file missing on SD";
      break;
    case UploadError::kOpenFailed:
      msg = "cannot open file";
      break;
    case UploadError::kConnectFailed:
      msg = "cannot connect to upload server";
      break;
    case UploadError::kReadFailed:
      msg = "upload interrupted: file read failed";
      break;
    case UploadError::kWriteFailed:
      msg = "upload interrupted: network write failed";
      break;
    case UploadError::kResponseTimeout:
      msg = "upload interrupted: response timeout";
      break;
    case UploadError::kBadStatusLine:
      msg = "invalid HTTP response";
      break;
    case UploadError::kHttpStatusFailed:
      msg = "server rejected upload";
      break;
    case UploadError::kRejectedResponse:
      msg = "server response did not match upload contract";
      break;
  }
  if (error == UploadError::kHttpStatusFailed) {
    snprintf(s_last_error_message, sizeof(s_last_error_message), "%s (%d)", msg, http_status);
  } else if (error == UploadError::kWriteFailed) {
    snprintf(s_last_error_message,
             sizeof(s_last_error_message),
             "%s (wifi=%d connected=%d sent=%lu)",
             msg,
             static_cast<int>(WiFi.status()),
             result.connect_problem ? 0 : 1,
             static_cast<unsigned long>(result.sent_bytes));
  } else {
    strncpy(s_last_error_message, msg, sizeof(s_last_error_message));
    s_last_error_message[sizeof(s_last_error_message) - 1] = '\0';
  }
}

UploadResult send_file_multipart(const storage::FileInfo& info) {
#if UPLOAD_DEBUG_FLOW
  const uint32_t debug_start_ms = millis();
#endif
  UploadResult result{};
  result.ok = false;
  result.error = UploadError::kNone;
  result.http_status = 0;
  result.retryable = false;
  result.retry_after_ms = 0;
  result.server_code[0] = '\0';
  result.server_message[0] = '\0';
  const config::Config& cfg = config::get();
  ParsedUrl parsed{};
  if (!parse_http_url(cfg.global.upload_url, &parsed) || !parsed.valid) {
    Serial.println("[UPLOAD] Invalid upload_url (only http:// supported)");
    result.error = UploadError::kInvalidUrl;
    result.connect_problem = true;
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=start path=%s size=%lu host=%s port=%u pathUrl=%s wifi=%d heap=%lu\n",
                info.path,
                static_cast<unsigned long>(info.size_bytes),
                parsed.host,
                static_cast<unsigned>(parsed.port),
                parsed.path,
                static_cast<int>(WiFi.status()),
                static_cast<unsigned long>(ESP.getFreeHeap()));
#endif

  if (!SD.exists(info.path)) {
    result.error = UploadError::kMissingFile;
    return result;
  }

  File file = SD.open(info.path, FILE_READ);
  if (!file) {
    result.error = UploadError::kOpenFailed;
    return result;
  }

  const char* slash = strrchr(info.path, '/');
  const char* filename = slash ? (slash + 1) : info.path;
  char idempotency_key[128];
  build_idempotency_key(info, filename, idempotency_key, sizeof(idempotency_key));
  const String device_id = WiFi.macAddress();

  const String boundary = String("----CANGrabberBoundary") + String(millis());
  String prefix;
  prefix.reserve(512);
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"bus_id\"\r\n\r\n";
  prefix += String(info.bus_id) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"start_ms\"\r\n\r\n";
  prefix += String(info.start_ms) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"end_ms\"\r\n\r\n";
  prefix += String(info.end_ms) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"size_bytes\"\r\n\r\n";
  prefix += String(info.size_bytes) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"checksum\"\r\n\r\n";
  prefix += String(info.checksum) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"path\"\r\n\r\n";
  prefix += String(info.path) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"flags\"\r\n\r\n";
  prefix += String(info.flags) + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"source\"\r\n\r\n";
  prefix += "esp32-can-grabber\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\n";
  prefix += device_id + "\r\n";
  prefix += "--" + boundary + "\r\n";
  prefix += "Content-Disposition: form-data; name=\"updatefile\"; filename=\"";
  prefix += String(filename);
  prefix += "\"\r\n";
  prefix += "Content-Type: application/octet-stream\r\n\r\n";

  const String suffix = "\r\n--" + boundary + "--\r\n";
  const uint32_t content_length =
      static_cast<uint32_t>(prefix.length() + file.size() + suffix.length());
#if UPLOAD_DEBUG_FLOW
  Serial.printf(
      "[UPLOAD][#debug] flow=payload prefix=%lu file=%lu suffix=%lu contentLength=%lu\n",
      static_cast<unsigned long>(prefix.length()),
      static_cast<unsigned long>(file.size()),
      static_cast<unsigned long>(suffix.length()),
      static_cast<unsigned long>(content_length));
#endif

  IPAddress resolved_ip;
  char resolve_method[16];
  if (get_last_probe_ip(parsed.host, &resolved_ip)) {
    strncpy(resolve_method, "probe-ip", sizeof(resolve_method));
    resolve_method[sizeof(resolve_method) - 1] = '\0';
  } else if (!resolve_upload_host(parsed.host, &resolved_ip, resolve_method, sizeof(resolve_method))) {
    file.close();
    result.error = UploadError::kConnectFailed;
    result.connect_problem = true;
    snprintf(result.server_message,
             sizeof(result.server_message),
             "resolve %s failed",
             parsed.host);
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=resolved host=%s ip=%s method=%s\n",
                parsed.host,
                resolved_ip.toString().c_str(),
                resolve_method);
#endif

  WiFiClient client;
  client.setTimeout(kResponseTimeoutMs / 1000);
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=connect begin ip=%s port=%u timeoutMs=%lu\n",
                resolved_ip.toString().c_str(),
                static_cast<unsigned>(parsed.port),
                static_cast<unsigned long>(kConnectTimeoutMs));
#endif
  if (!client.connect(resolved_ip, parsed.port, kConnectTimeoutMs)) {
    file.close();
    result.error = UploadError::kConnectFailed;
    result.connect_problem = true;
    snprintf(result.server_message,
             sizeof(result.server_message),
             "connect %s(%s):%u failed",
             parsed.host,
             resolve_method,
             static_cast<unsigned>(parsed.port));
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=connect ok connected=%d localPort=%u\n",
                client.connected() ? 1 : 0,
                static_cast<unsigned>(client.localPort()));
#endif

  String headers;
  headers.reserve(512);
  headers += "POST ";
  headers += parsed.path;
  headers += " HTTP/1.1\r\n";
  headers += "Host: ";
  headers += parsed.host;
  headers += "\r\n";
  headers += "Connection: close\r\n";
  headers += "Content-Type: multipart/form-data; boundary=";
  headers += boundary;
  headers += "\r\n";
  headers += "X-Idempotency-Key: ";
  headers += idempotency_key;
  headers += "\r\n";
  if (cfg.global.api_token[0] != '\0') {
    headers += "X-Api-Token: ";
    headers += cfg.global.api_token;
    headers += "\r\n";
  }
  headers += "Content-Length: ";
  headers += String(static_cast<unsigned long>(content_length));
  headers += "\r\n\r\n";

  if (!write_all_with_timeout(client,
                              reinterpret_cast<const uint8_t*>(headers.c_str()),
                              headers.length(),
                              kWriteTimeoutMs,
                              "headers")) {
    file.close();
    const bool had_connect_problem =
        (WiFi.status() != WL_CONNECTED) || !client.connected();
    client.stop();
    result.error = UploadError::kWriteFailed;
    result.interrupted = true;
    result.connect_problem = had_connect_problem;
    strncpy(result.server_message, "write failed at headers", sizeof(result.server_message));
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=headers ok len=%lu elapsedMs=%lu\n",
                static_cast<unsigned long>(headers.length()),
                static_cast<unsigned long>(millis() - debug_start_ms));
#endif

  if (!write_all_with_timeout(client,
                              reinterpret_cast<const uint8_t*>(prefix.c_str()),
                              prefix.length(),
                              kWriteTimeoutMs,
                              "multipart-prefix")) {
    file.close();
    const bool had_connect_problem =
        (WiFi.status() != WL_CONNECTED) || !client.connected();
    client.stop();
    result.error = UploadError::kWriteFailed;
    result.interrupted = true;
    result.connect_problem = had_connect_problem;
    strncpy(result.server_message, "write failed at multipart-prefix", sizeof(result.server_message));
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=prefix ok len=%lu elapsedMs=%lu\n",
                static_cast<unsigned long>(prefix.length()),
                static_cast<unsigned long>(millis() - debug_start_ms));
#endif
  uint8_t buffer[kUploadFileReadChunkBytes];
  uint32_t file_chunks = 0;
  while (file.available()) {
    const size_t n = file.read(buffer, sizeof(buffer));
    if (n == 0) {
      file.close();
      client.stop();
      result.error = UploadError::kReadFailed;
      result.interrupted = true;
      strncpy(result.server_message, "read failed from SD", sizeof(result.server_message));
      result.server_message[sizeof(result.server_message) - 1] = '\0';
      return result;
    }
    if (!write_file_chunk_with_timeout(client,
                                       buffer,
                                       n,
                                       kWriteTimeoutMs,
                                       &result.sent_bytes,
                                       "file-chunk")) {
      file.close();
      const bool had_connect_problem =
          (WiFi.status() != WL_CONNECTED) || !client.connected();
      client.stop();
      result.error = UploadError::kWriteFailed;
      result.interrupted = true;
      result.connect_problem = had_connect_problem;
      strncpy(result.server_message, "write failed at file-chunk", sizeof(result.server_message));
      result.server_message[sizeof(result.server_message) - 1] = '\0';
      return result;
    }
    file_chunks++;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=file ok chunks=%lu sent=%lu elapsedMs=%lu\n",
                static_cast<unsigned long>(file_chunks),
                static_cast<unsigned long>(result.sent_bytes),
                static_cast<unsigned long>(millis() - debug_start_ms));
#endif
  file.close();
  if (!write_all_with_timeout(client,
                              reinterpret_cast<const uint8_t*>(suffix.c_str()),
                              suffix.length(),
                              kWriteTimeoutMs,
                              "multipart-suffix")) {
    const bool had_connect_problem =
        (WiFi.status() != WL_CONNECTED) || !client.connected();
    client.stop();
    result.error = UploadError::kWriteFailed;
    result.interrupted = true;
    result.connect_problem = had_connect_problem;
    strncpy(result.server_message, "write failed at multipart-suffix", sizeof(result.server_message));
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=suffix ok len=%lu elapsedMs=%lu\n",
                static_cast<unsigned long>(suffix.length()),
                static_cast<unsigned long>(millis() - debug_start_ms));
#endif

  String status_line;
  if (!read_line_with_timeout(client, &status_line, kResponseTimeoutMs)) {
    client.stop();
    result.error = UploadError::kResponseTimeout;
    result.interrupted = true;
    strncpy(result.server_message, "timeout waiting status-line", sizeof(result.server_message));
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=status-line '%s'\n", status_line.c_str());
#endif

  int status_code = 0;
  if (sscanf(status_line.c_str(), "HTTP/%*d.%*d %d", &status_code) != 1) {
    client.stop();
    result.error = UploadError::kBadStatusLine;
    result.interrupted = true;
    strncpy(result.server_message, "bad HTTP status-line", sizeof(result.server_message));
    result.server_message[sizeof(result.server_message) - 1] = '\0';
    return result;
  }
  result.http_status = status_code;

  String header_line;
  for (;;) {
    if (!read_line_with_timeout(client, &header_line, kResponseTimeoutMs)) {
      break;
    }
    if (header_line.length() == 0) {
      break;
    }
    const uint32_t retry_after = parse_retry_after_ms(header_line);
    if (retry_after > 0) {
      result.retry_after_ms = retry_after;
    }
  }

  String response_body;
  (void)read_body_with_timeout(client,
                               &response_body,
                               kResponseTimeoutMs,
                               kMaxResponseBodyBytes);
#if UPLOAD_DEBUG_FLOW
  Serial.printf("[UPLOAD][#debug] flow=response status=%d bodyLen=%lu elapsedMs=%lu\n",
                status_code,
                static_cast<unsigned long>(response_body.length()),
                static_cast<unsigned long>(millis() - debug_start_ms));
#endif
  parse_server_json(response_body, &result);

  client.stop();
  result.ok = is_success_contract(result);
  result.retryable = is_http_retryable(result);
  if (!result.ok) {
    if (status_code >= 200 && status_code < 300) {
      result.error = UploadError::kRejectedResponse;
    } else {
      result.error = UploadError::kHttpStatusFailed;
    }
    if (result.server_code[0] != '\0' || result.server_message[0] != '\0') {
      Serial.printf("[UPLOAD] Server response: code='%s' message='%s'\n",
                    result.server_code,
                    result.server_message);
    }
  }
  if (!result.ok && result.retry_after_ms > 0) {
    result.retryable = true;
  }
  if (!result.ok && !result.retryable &&
      (status_code == 400 || status_code == 401 || status_code == 413 ||
       status_code == 415)) {
    // Permanent client-side rejection: keep as non-retryable.
  } else if (!result.ok && !result.retryable && status_code >= 500) {
    result.retryable = true;
  }
  if (!result.ok && result.error == UploadError::kNone) {
    result.error = UploadError::kHttpStatusFailed;
  }
  return result;
}

void queue_remove(size_t index) {
  if (index >= kQueueLen) {
    return;
  }
  s_queue[index].used = false;
  s_queue[index].path[0] = '\0';
  s_queue[index].retries = 0;
  s_queue[index].next_attempt_ms = 0;
}

uint32_t queue_schedule_retry(size_t index,
                              uint8_t retries,
                              uint32_t retry_after_ms,
                              bool add_jitter) {
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
    backoff = (backoff <= (kMaxBackoffMs - jitter)) ? (backoff + jitter)
                                                     : kMaxBackoffMs;
  }
  s_queue[index].retries = retries;
  s_queue[index].next_attempt_ms = millis() + backoff;
  return backoff;
}

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
  strncpy(item.path, path, sizeof(item.path));
  item.path[sizeof(item.path) - 1] = '\0';
  item.retries = 0;
  item.next_attempt_ms = 0;
  item.manual = manual;
  item.used = true;
  return true;
}

void task_entry(void*) {
  for (;;) {
    if (!s_initialized) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    probe_upload_server_reachability();

    if (WiFi.status() != WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    const uint32_t now = millis();
    if (s_last_pending_scan_ms == 0 ||
        (now - s_last_pending_scan_ms) >= kPendingScanIntervalMs) {
      queue_pending();
      s_last_pending_scan_ms = now;
    }

    size_t index = 0;
    QueueItem item{};
    bool has_item = false;

    portENTER_CRITICAL(&s_queue_mux);
    const QueueItem* queued = queue_snapshot_ready(now, &index);
    if (queued != nullptr) {
      item = *queued;
      has_item = true;
    }
    portEXIT_CRITICAL(&s_queue_mux);

    if (!has_item) {
      vTaskDelay(pdMS_TO_TICKS(kTaskSleepMs));
      continue;
    }

    storage::FileInfo info{};
    if (!storage::find_file_info(item.path, &info, nullptr)) {
      portENTER_CRITICAL(&s_queue_mux);
      queue_remove(index);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    if (info.flags & storage::kFlagActive) {
      portENTER_CRITICAL(&s_queue_mux);
      s_queue[index].next_attempt_ms = millis() + 3000;
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    if (info.flags & storage::kFlagUploaded) {
      portENTER_CRITICAL(&s_queue_mux);
      queue_remove(index);
      portEXIT_CRITICAL(&s_queue_mux);
      continue;
    }

    portENTER_CRITICAL(&s_stats_mux);
    s_uploading = true;
    s_current_file_size = info.size_bytes;
    s_current_file_sent = 0;
    portEXIT_CRITICAL(&s_stats_mux);

    Serial.printf("[UPLOAD] Attempt path=%s try=%u\n",
                  info.path,
                  static_cast<unsigned>(item.retries + 1));
    const UploadResult result = send_file_multipart(info);

    portENTER_CRITICAL(&s_stats_mux);
    s_uploading = false;
    s_current_file_size = 0;
    s_current_file_sent = 0;
    if (result.ok) {
      s_uploaded_files_total++;
      s_uploaded_bytes_total += info.size_bytes;
      s_last_error = false;
      s_last_error_interrupted = false;
      s_last_error_connect = false;
      s_last_error_message[0] = '\0';
    } else {
      s_last_error = true;
      s_last_error_interrupted = result.interrupted;
      s_last_error_connect = result.connect_problem;
      set_last_error(result);
      if (result.server_code[0] != '\0') {
        const size_t n = strlen(s_last_error_message);
        if (n + 4 < sizeof(s_last_error_message)) {
          snprintf(s_last_error_message + n,
                   sizeof(s_last_error_message) - n,
                   " [%s]",
                   result.server_code);
        }
      }
    }
    portEXIT_CRITICAL(&s_stats_mux);

    const bool ok = result.ok;
    bool log_retry = false;
    bool log_permanent = false;
    uint32_t scheduled_delay_ms = 0;
    portENTER_CRITICAL(&s_queue_mux);
    if (ok) {
      queue_remove(index);
    } else if (result.retryable || result.connect_problem || result.interrupted) {
      scheduled_delay_ms = queue_schedule_retry(index,
                                                static_cast<uint8_t>(item.retries + 1),
                                                result.retry_after_ms,
                                                true);
      log_retry = true;
    } else {
      queue_remove(index);
      log_permanent = true;
    }
    portEXIT_CRITICAL(&s_queue_mux);

    if (log_retry) {
      Serial.printf(
          "[UPLOAD] Retry scheduled path=%s in=%lums err=%u status=%d code=%s msg=%s interrupted=%d connect=%d wifi=%d freeHeap=%lu\n",
          info.path,
          static_cast<unsigned long>(scheduled_delay_ms),
          static_cast<unsigned>(result.error),
          result.http_status,
          result.server_code,
          result.server_message,
          result.interrupted ? 1 : 0,
          result.connect_problem ? 1 : 0,
          static_cast<int>(WiFi.status()),
          static_cast<unsigned long>(ESP.getFreeHeap()));
    } else if (log_permanent) {
      Serial.printf(
          "[UPLOAD] Permanent failure path=%s err=%u status=%d code=%s msg=%s\n",
          info.path,
          static_cast<unsigned>(result.error),
          result.http_status,
          result.server_code,
          result.server_message);
    }

    if (ok) {
      storage::mark_uploaded(info.path);
      Serial.printf("[UPLOAD] Uploaded %s\n", info.path);
    }

    if (result.connect_problem) {
      vTaskDelay(pdMS_TO_TICKS(kPostConnectProblemSleepMs));
    } else {
      vTaskDelay(pdMS_TO_TICKS(kPostAttemptSleepMs));
    }
  }
}

} // namespace

void init() {
  if (s_initialized) {
    return;
  }
  memset(s_queue, 0, sizeof(s_queue));
  s_initialized = true;
  xTaskCreatePinnedToCore(task_entry,
                          "upload_task",
                          8192,
                          nullptr,
                          kUploadTaskPriority,
                          &s_task,
                          kUploadTaskCore);
}

void request_upload(const char* path) {
  if (!s_initialized || path == nullptr) {
    return;
  }
  portENTER_CRITICAL(&s_queue_mux);
  const bool queued = queue_add_or_bump(path, true);
  portEXIT_CRITICAL(&s_queue_mux);
  if (!queued) {
    Serial.printf("[UPLOAD] Queue full, dropping %s\n", path);
  }
}

void request_upload_auto(const char* path) {
  if (!s_initialized || path == nullptr) {
    return;
  }
  if (!config::get().global.auto_upload_enabled) {
    return;
  }
  portENTER_CRITICAL(&s_queue_mux);
  const bool queued = queue_add_or_bump(path, false);
  portEXIT_CRITICAL(&s_queue_mux);
  if (!queued) {
    Serial.printf("[UPLOAD] Queue full, dropping %s\n", path);
  }
}

void queue_pending() {
  if (!s_initialized) {
    return;
  }
  if (!config::get().global.auto_upload_enabled) {
    return;
  }
  const size_t count = storage::file_count();
  for (size_t i = 0; i < count; ++i) {
    storage::FileInfo info{};
    if (!storage::get_file_info(i, &info)) {
      continue;
    }
    if (info.flags & storage::kFlagActive) {
      continue;
    }
    if (info.flags & storage::kFlagUploaded) {
      continue;
    }
    request_upload_auto(info.path);
  }
}

Stats get_stats() {
  Stats stats{};
  stats.initialized = s_initialized;

  portENTER_CRITICAL(&s_stats_mux);
  const uint32_t now = millis();
  if (s_speed_window_start_ms != 0 && (now - s_speed_window_start_ms) > 2000 &&
      s_speed_window_bytes == 0) {
    s_upload_speed_bps = 0;
  }
  stats.uploading = s_uploading;
  stats.upload_speed_bytes_per_sec = s_upload_speed_bps;
  stats.current_file_size_bytes = s_current_file_size;
  stats.current_file_sent_bytes = s_current_file_sent;
  stats.total_uploaded_files = s_uploaded_files_total;
  stats.total_uploaded_bytes = s_uploaded_bytes_total;
  stats.last_error = s_last_error;
  stats.last_error_interrupted = s_last_error_interrupted;
  stats.last_error_connect = s_last_error_connect;
  strncpy(stats.last_error_message, s_last_error_message,
          sizeof(stats.last_error_message));
  stats.last_error_message[sizeof(stats.last_error_message) - 1] = '\0';
  stats.server_reachable_known = s_server_reachable_known;
  stats.server_reachable = s_server_reachable;
  stats.server_rtt_ms = s_server_rtt_ms;
  strncpy(stats.server_reach_message, s_server_reach_message,
          sizeof(stats.server_reach_message));
  stats.server_reach_message[sizeof(stats.server_reach_message) - 1] = '\0';
  portEXIT_CRITICAL(&s_stats_mux);

  const size_t count = storage::file_count();
  uint32_t uploaded_files = 0;
  uint32_t outstanding_files = 0;
  uint64_t outstanding_bytes = 0;
  for (size_t i = 0; i < count; ++i) {
    storage::FileInfo info{};
    if (!storage::get_file_info(i, &info)) {
      continue;
    }
    if (info.flags & storage::kFlagActive) {
      continue;
    }
    if (info.flags & storage::kFlagUploaded) {
      uploaded_files++;
    } else {
      outstanding_files++;
      outstanding_bytes += info.size_bytes;
    }
  }

  portENTER_CRITICAL(&s_stats_mux);
  if (s_uploading && outstanding_bytes >= s_current_file_sent) {
    outstanding_bytes -= s_current_file_sent;
  }
  portEXIT_CRITICAL(&s_stats_mux);

  stats.uploaded_files = uploaded_files;
  stats.outstanding_files = outstanding_files;
  stats.outstanding_bytes = outstanding_bytes;
  return stats;
}

} // namespace upload
