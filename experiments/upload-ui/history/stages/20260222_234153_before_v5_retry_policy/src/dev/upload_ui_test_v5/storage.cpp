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

#if __has_include("dev/sd_http_upload_ui_secrets.h")
#include "dev/sd_http_upload_ui_secrets.h"
#elif __has_include("dev/sd_http_post_speed_secrets.h")
#include "dev/sd_http_post_speed_secrets.h"
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
constexpr size_t kQueueLen = 32;
constexpr uint32_t kAutoScanIntervalMs = 4000;
constexpr uint32_t kBaseBackoffMs = 2000;
constexpr uint32_t kMaxBackoffMs = 60000;
constexpr size_t kTestFileCount = 8;
constexpr const char* kTestFilePaths[kTestFileCount] = {
    "/sd_http_post_8mb_01.bin",
    "/sd_http_post_8mb_02.bin",
    "/sd_http_post_8mb_03.bin",
    "/sd_http_post_8mb_04.bin",
    "/sd_http_post_8mb_05.bin",
    "/sd_http_post_8mb_06.bin",
    "/sd_http_post_8mb_07.bin",
    "/sd_http_post_8mb_08.bin",
};
constexpr char kUploadPath[] = "/sd_http_post_8mb_01.bin";
constexpr char kUploadedStatePath[] = "/upload_state_v5.txt";
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

struct QueueItem {
  bool used;
  bool manual;
  uint8_t retries;
  uint32_t next_attempt_ms;
  char path[96];
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
std::atomic<uint32_t> s_last_auto_scan_ms{0};
RingBuffer s_ring = {};
ParsedUrl s_target = {};
QueueItem s_queue[kQueueLen] = {};
portMUX_TYPE s_queue_mux = portMUX_INITIALIZER_UNLOCKED;

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

bool ensure_single_test_file(const char* path) {
  if (path == nullptr || path[0] == '\0') {
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
    SD.remove(path);
  }
  File f = SD.open(path, FILE_WRITE);
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

bool uploaded_state_contains(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (!SD.exists(kUploadedStatePath)) {
    return false;
  }
  File f = SD.open(kUploadedStatePath, FILE_READ);
  if (!f) {
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

bool mark_uploaded_path(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (uploaded_state_contains(path)) {
    return true;
  }
  File f = SD.open(kUploadedStatePath, FILE_APPEND);
  if (!f) {
    return false;
  }
  const size_t n1 = f.print(path);
  const size_t n2 = f.print("\n");
  f.close();
  return (n1 > 0 && n2 > 0);
}

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

uint32_t queue_schedule_retry(size_t index, uint8_t retries) {
  if (index >= kQueueLen) {
    return 0;
  }
  uint32_t backoff = kBaseBackoffMs;
  for (uint8_t i = 1; i < retries; ++i) {
    if (backoff >= (kMaxBackoffMs / 2)) {
      backoff = kMaxBackoffMs;
      break;
    }
    backoff *= 2;
  }
  if (backoff > kMaxBackoffMs) {
    backoff = kMaxBackoffMs;
  }
  const uint32_t jitter = random(0, 1000);
  backoff = (backoff <= (kMaxBackoffMs - jitter)) ? (backoff + jitter) : kMaxBackoffMs;
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
  strncpy(item.path, path, sizeof(item.path) - 1);
  item.path[sizeof(item.path) - 1] = '\0';
  item.used = true;
  item.manual = manual;
  item.retries = 0;
  item.next_attempt_ms = 0;
  return true;
}

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

void queue_pending() {
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (uploaded_state_contains(kTestFilePaths[i])) {
      continue;
    }
    portENTER_CRITICAL(&s_queue_mux);
    queue_add_or_bump(kTestFilePaths[i], false);
    portEXIT_CRITICAL(&s_queue_mux);
  }
}

void queue_pending_periodic() {
  const uint32_t now_ms = millis();
  const uint32_t last = s_last_auto_scan_ms.load(std::memory_order_relaxed);
  if (last != 0 && (now_ms - last) < kAutoScanIntervalMs) {
    return;
  }
  s_last_auto_scan_ms.store(now_ms, std::memory_order_relaxed);
  queue_pending();
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
