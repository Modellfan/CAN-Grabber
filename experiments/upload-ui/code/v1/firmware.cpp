#if defined(SD_HTTP_UPLOAD_UI_TEST)

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/hardware_config.h"

#if __has_include("../../config/sd_http_upload_ui_secrets.h")
#include "../../config/sd_http_upload_ui_secrets.h"
#elif __has_include("../../../http-upload-performance/config/sd_http_post_speed_secrets.h")
#include "../../../http-upload-performance/config/sd_http_post_speed_secrets.h"
#endif

#ifndef SD_HTTP_UPLOAD_TEST_SSID
#define SD_HTTP_UPLOAD_TEST_SSID ""
#endif

#ifndef SD_HTTP_UPLOAD_TEST_PASSWORD
#define SD_HTTP_UPLOAD_TEST_PASSWORD ""
#endif

#ifndef SD_HTTP_UPLOAD_TEST_URL
#define SD_HTTP_UPLOAD_TEST_URL "http://192.168.1.2:8080/upload"
#endif

namespace {

constexpr size_t CHUNK_SIZE = 8192;
constexpr int UPLOAD_CORE = 1;
constexpr int SERVER_CORE = 0;
constexpr UBaseType_t UPLOAD_TASK_PRIORITY = 5;
constexpr UBaseType_t SERVER_TASK_PRIORITY = 3;
constexpr UBaseType_t PREFETCH_TASK_PRIORITY = 2;
constexpr UBaseType_t MONITOR_TASK_PRIORITY = 1;
constexpr uint32_t UPLOAD_TIMESLICE_US = 2000;
constexpr uint32_t ZERO_WRITE_RETRY_DELAY_MS = 1;
constexpr uint32_t STATUS_RATE_WINDOW_MS = 250;
constexpr uint32_t CONNECT_TIMEOUT_MS = 10000;
constexpr uint32_t RESPONSE_TIMEOUT_MS = 15000;
constexpr uint32_t WRITE_STALL_TIMEOUT_MS = 2000;
constexpr uint32_t SERIAL_PROGRESS_INTERVAL_MS = 1000;
constexpr bool USE_PREFETCH_TASK = false;
constexpr bool ENABLE_UPLOAD_TASK = true;
constexpr uint32_t MAX_UPLOAD_BPS = 0;
constexpr size_t RING_SLOTS = 4;
constexpr char kUploadPath[] = "/sd_http_post_8mb.bin";
constexpr size_t kCreateBytes = 8UL * 1024UL * 1024UL;

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

struct ParsedUrl {
  bool valid;
  uint16_t port;
  char host[96];
  char path[192];
};

struct RingSlot {
  uint8_t* data;
  size_t len;
  bool inUse;
};

struct RingBuffer {
  RingSlot slots[RING_SLOTS];
  size_t head;
  size_t tail;
  size_t count;
};

SPIClass s_sd_spi(HSPI);
WebServer s_server(80);
TaskHandle_t s_upload_task_handle = nullptr;
TaskHandle_t s_server_task_handle = nullptr;
TaskHandle_t s_prefetch_task_handle = nullptr;
TaskHandle_t s_monitor_task_handle = nullptr;
portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
UploadStats s_stats = {0, 0, 0, UploadState::IDLE, 0, 0};
std::atomic<bool> s_upload_requested{false};
std::atomic<bool> s_abort_requested{false};
std::atomic<uint32_t> s_run_counter{0};
std::atomic<uint32_t> s_active_run_id{0};
std::atomic<uint32_t> s_terminal_state_until_ms{0};
RingBuffer s_ring = {};
ParsedUrl s_target = {};

const char kIndexHtml[] PROGMEM =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
    "content='width=device-width,initial-scale=1'><title>Upload UI</title>"
    "<style>body{font-family:monospace;max-width:520px;margin:20px auto;padding:0 12px}"
    "button{padding:8px 12px;margin-right:8px}#p{font-size:20px;font-weight:700}"
    "#r{font-size:18px;margin:8px 0 10px}</style>"
    "</head><body><h1>ESP32 Upload</h1><div id='p'>0%</div><div id='r'>0 B/s</div><pre id='s'>idle</pre>"
    "<button onclick='fetch(\"/start\")'>start</button>"
    "<button onclick='fetch(\"/abort\")'>abort</button><script>"
    "const p=document.getElementById('p');const r=document.getElementById('r');const s=document.getElementById('s');"
    "function fmtRate(bps){if(bps>=1048576)return (bps/1048576).toFixed(2)+' MB/s';"
    "if(bps>=1024)return (bps/1024).toFixed(1)+' KB/s';return bps+' B/s';}"
    "function draw(o){const t=Math.max(1,Number(o.total||1));const sent=Number(o.sent||0);"
    "const rate=Number(o.rate||0);"
    "const pct=(100*sent/t).toFixed(1);p.textContent=pct+'%';"
    "r.textContent=fmtRate(rate);"
    "s.textContent='state='+o.state+'\\nsent='+sent+'\\ntotal='+o.total+'\\nerr='+o.error;}"
    "function poll(){fetch('/status').then(x=>x.json()).then(draw).catch(()=>{});}poll();setInterval(poll,250);</script></body></html>";

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

void print_upload_log(const char* tag, uint32_t run_id, int http_status, uint32_t elapsed_ms, const UploadStats& snap) {
  Serial.printf("UPLOAD_LOG tag=%s run=%lu ms=%lu state=%s sent=%llu total=%llu rate_bps=%lu error=%ld http=%d rssi=%d heap=%lu\n",
                tag,
                static_cast<unsigned long>(run_id),
                static_cast<unsigned long>(elapsed_ms),
                state_name(snap.state),
                static_cast<unsigned long long>(snap.bytesSent),
                static_cast<unsigned long long>(snap.totalBytes),
                static_cast<unsigned long>(snap.lastRateBps),
                static_cast<long>(snap.errorCode),
                http_status,
                WiFi.RSSI(),
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

UploadStats read_stats_snapshot() {
  UploadStats snap{};
  portENTER_CRITICAL(&s_stats_mux);
  snap = s_stats;
  portEXIT_CRITICAL(&s_stats_mux);
  return snap;
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
                             uint64_t payload_size,
                             String* headers,
                             String* prefix,
                             String* suffix) {
  if (headers == nullptr || prefix == nullptr || suffix == nullptr) {
    return false;
  }
  const uint32_t now_ms = millis();
  String boundary = String("----CANGrabberSpeedTestBoundary") + String(now_ms);

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
  *prefix += String(kUploadPath) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"flags\"\r\n\r\n0\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\nesp32-s3-ui-test\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"updatefile\"; filename=\"sd_http_post_8mb.bin\"\r\n";
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
  *headers += "\r\nContent-Length: ";
  *headers += String(static_cast<unsigned long long>(content_len));
  *headers += "\r\n\r\n";
  return true;
}

bool connect_wifi() {
  if (strlen(SD_HTTP_UPLOAD_TEST_SSID) == 0) {
    Serial.println("SSID is empty; set SD_HTTP_UPLOAD_TEST_SSID");
    return false;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.useStaticBuffers(true);
  WiFi.begin(SD_HTTP_UPLOAD_TEST_SSID, SD_HTTP_UPLOAD_TEST_PASSWORD);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

bool init_sd() {
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  return SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8);
}

bool ensure_test_file() {
  if (SD.exists(kUploadPath)) {
    File existing = SD.open(kUploadPath, FILE_READ);
    if (existing && existing.size() == kCreateBytes) {
      existing.close();
      return true;
    }
    if (existing) {
      existing.close();
    }
    SD.remove(kUploadPath);
  }
  File f = SD.open(kUploadPath, FILE_WRITE);
  if (!f) {
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

void ring_reset() {
  s_ring.head = 0;
  s_ring.tail = 0;
  s_ring.count = 0;
  for (size_t i = 0; i < RING_SLOTS; ++i) {
    s_ring.slots[i].len = 0;
    s_ring.slots[i].inUse = false;
  }
}

void ring_init_memory() {
  for (size_t i = 0; i < RING_SLOTS; ++i) {
    if (s_ring.slots[i].data == nullptr) {
      s_ring.slots[i].data = static_cast<uint8_t*>(ps_malloc(CHUNK_SIZE));
      if (s_ring.slots[i].data == nullptr) {
        s_ring.slots[i].data = static_cast<uint8_t*>(malloc(CHUNK_SIZE));
      }
    }
    s_ring.slots[i].len = 0;
    s_ring.slots[i].inUse = false;
  }
  s_ring.head = 0;
  s_ring.tail = 0;
  s_ring.count = 0;
}

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

} // namespace

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
    Serial.println("SD init failed");
    return;
  }
  if (!ensure_test_file()) {
    Serial.println("Failed to create/validate test file");
    return;
  }

  configure_http_server();
  print_architecture();
  print_serial_help();

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

void loop() {
  handle_serial();
  delay(20);
}

#endif // SD_HTTP_UPLOAD_UI_TEST
