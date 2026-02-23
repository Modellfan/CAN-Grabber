#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>

#include <atomic>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "can/can_manager.h"
#include "config/app_config.h"
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

#ifndef SD_HTTP_UPLOAD_TEST_API_TOKEN
#define SD_HTTP_UPLOAD_TEST_API_TOKEN ""
#endif

namespace {

constexpr size_t CHUNK_SIZE = 8192;
constexpr size_t CHUNK_COUNT = 6;
constexpr size_t kQueueLen = 32;
constexpr size_t kTestFileCount = 8;
constexpr uint32_t kAutoScanIntervalMs = 4000;
constexpr uint32_t kStatusRateWindowMs = 250;
constexpr uint32_t kConnectTimeoutMs = 10000;
constexpr uint32_t kResponseTimeoutMs = 15000;
constexpr uint32_t kWriteStallTimeoutMs = 2000;
constexpr uint32_t kUploadTimesliceUs = 2000;
constexpr uint32_t kSerialProgressIntervalMs = 1000;
constexpr uint32_t kCreateBytes = 8UL * 1024UL * 1024UL;
constexpr uint32_t kGreenBacklogBytes = 4096;
constexpr uint32_t kRedBacklogBytes = 12288;
constexpr uint32_t kResumeBacklogBytes = 3072;
constexpr uint32_t kProbeMinIntervalMs = 3000;
constexpr uint32_t kProbeConnectTimeoutMs = 2500;

constexpr BaseType_t CAN_CORE = 0;
constexpr BaseType_t STORAGE_CORE = 1;
constexpr BaseType_t UPLOAD_NET_CORE = 1;
constexpr BaseType_t SERVER_CORE = 1;
constexpr BaseType_t MONITOR_CORE = 1;

constexpr UBaseType_t STORAGE_TASK_PRIO = 6;
constexpr UBaseType_t UPLOAD_NET_TASK_PRIO = 5;
constexpr UBaseType_t SERVER_TASK_PRIO = 4;
constexpr UBaseType_t MONITOR_TASK_PRIO = 2;

constexpr char kUploadedStatePath[] = "/upload_state_v3.txt";
constexpr char kTestFilePaths[kTestFileCount][32] = {
    "/sd_http_post_8mb_01.bin",
    "/sd_http_post_8mb_02.bin",
    "/sd_http_post_8mb_03.bin",
    "/sd_http_post_8mb_04.bin",
    "/sd_http_post_8mb_05.bin",
    "/sd_http_post_8mb_06.bin",
    "/sd_http_post_8mb_07.bin",
    "/sd_http_post_8mb_08.bin",
};

enum class UploadState : uint8_t {
  IDLE = 0,
  CONNECTING = 1,
  SENDING = 2,
  FINALIZING = 3,
  DONE = 4,
  ERROR = 5,
};

enum class BacklogZone : uint8_t {
  GREEN = 0,
  YELLOW = 1,
  RED = 2,
};

struct UploadStats {
  uint64_t bytes_sent;
  uint64_t total_bytes;
  uint32_t rate_bps;
  UploadState state;
  int32_t error_code;
  uint32_t seq;
};

struct ParsedUrl {
  bool valid;
  uint16_t port;
  char host[96];
  char path[192];
};

struct QueueItem {
  bool used;
  char path[96];
};

struct ChunkPacket {
  uint8_t index;
  uint16_t len;
  uint8_t eof;
};

struct StreamControl {
  bool active;
  bool request_open;
  bool request_stop;
  uint64_t total_file_bytes;
  char path[96];
};

struct ProbeState {
  bool has_ip;
  bool ok;
  uint32_t ip_raw;
  uint32_t last_probe_ms;
  uint32_t last_rtt_ms;
  int32_t last_error;
};

struct LogFileState {
  File file;
  bool active;
  char path[80];
  uint32_t start_ms;
  uint64_t bytes_written;
};

struct MarkUploadedReq {
  char path[96];
};

SPIClass s_sd_spi(HSPI);
WebServer s_server(80);
TaskHandle_t s_storage_task_handle = nullptr;
TaskHandle_t s_upload_net_task_handle = nullptr;
TaskHandle_t s_server_task_handle = nullptr;
TaskHandle_t s_monitor_task_handle = nullptr;

portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_queue_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_stream_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_probe_mux = portMUX_INITIALIZER_UNLOCKED;

UploadStats s_stats = {0, 0, 0, UploadState::IDLE, 0, 0};
ParsedUrl s_target = {};
QueueItem s_upload_queue[kQueueLen] = {};
char s_active_upload_path[96] = {0};
std::atomic<uint32_t> s_last_auto_scan_ms{0};
std::atomic<bool> s_upload_requested{false};
std::atomic<bool> s_abort_requested{false};
std::atomic<uint32_t> s_run_counter{0};
std::atomic<uint32_t> s_active_run_id{0};
std::atomic<uint32_t> s_terminal_state_until_ms{0};
std::atomic<BacklogZone> s_backlog_zone{BacklogZone::GREEN};
std::atomic<uint32_t> s_last_backlog_bytes{0};
std::atomic<uint64_t> s_log_total_bytes{0};
std::atomic<uint32_t> s_log_bytes_per_sec{0};
std::atomic<uint32_t> s_log_pop_count{0};
std::atomic<uint32_t> s_sim_fps{0};

uint8_t s_chunk_pool[CHUNK_COUNT][CHUNK_SIZE] = {};
QueueHandle_t s_free_chunks = nullptr;
QueueHandle_t s_filled_chunks = nullptr;
QueueHandle_t s_mark_uploaded_q = nullptr;
StreamControl s_stream = {false, false, false, 0, {0}};
ProbeState s_probe = {false, false, 0, 0, 0, 0};
LogFileState s_log_files[config::kMaxBuses] = {};

const char kIndexHtml[] PROGMEM =
    "<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' "
    "content='width=device-width,initial-scale=1'><title>Upload UI v3</title>"
    "<style>body{font-family:monospace;max-width:560px;margin:20px auto;padding:0 12px}"
    "button{padding:8px 12px;margin-right:8px}#p{font-size:20px;font-weight:700}"
    "#r{font-size:18px;margin:8px 0 10px}</style>"
    "</head><body><h1>ESP32 Upload v3</h1><div id='p'>0%</div><div id='r'>0 B/s</div><pre id='s'>idle</pre>"
    "<button onclick='fetch(\"/start\")'>start</button>"
    "<button onclick='fetch(\"/abort\")'>abort</button>"
    "<button onclick='fetch(\"/sim/load?fps=4000\")'>sim 4k fps</button><script>"
    "const p=document.getElementById('p');const r=document.getElementById('r');const s=document.getElementById('s');"
    "function fmtRate(bps){if(bps>=1048576)return (bps/1048576).toFixed(2)+' MB/s';"
    "if(bps>=1024)return (bps/1024).toFixed(1)+' KB/s';return bps+' B/s';}"
    "function draw(o){const t=Math.max(1,Number(o.total||1));const sent=Number(o.sent||0);"
    "const pct=(100*sent/t).toFixed(1);p.textContent=pct+'%';"
    "r.textContent=fmtRate(Number(o.rate||0));"
    "s.textContent='state='+o.state+'\\nerr='+o.error+'\\nzone='+o.zone+'\\nbacklog='+o.backlog"
    "+'\\nlog_bps='+o.log_bps+'\\nprobe='+o.probe_ok+'/'+o.probe_rtt_ms+'ms';}"
    "function poll(){fetch('/status').then(x=>x.json()).then(draw).catch(()=>{});}poll();setInterval(poll,250);"
    "</script></body></html>";

const char* state_name(UploadState state) {
  switch (state) {
    case UploadState::IDLE: return "IDLE";
    case UploadState::CONNECTING: return "CONNECTING";
    case UploadState::SENDING: return "SENDING";
    case UploadState::FINALIZING: return "FINALIZING";
    case UploadState::DONE: return "DONE";
    case UploadState::ERROR: return "ERROR";
    default: return "UNKNOWN";
  }
}

const char* zone_name(BacklogZone zone) {
  switch (zone) {
    case BacklogZone::GREEN: return "GREEN";
    case BacklogZone::YELLOW: return "YELLOW";
    case BacklogZone::RED: return "RED";
    default: return "UNKNOWN";
  }
}

void update_stats(UploadState state, int32_t error_code, uint64_t sent, uint64_t total, uint32_t rate) {
  portENTER_CRITICAL(&s_stats_mux);
  s_stats.state = state;
  s_stats.error_code = error_code;
  s_stats.bytes_sent = sent;
  s_stats.total_bytes = total;
  s_stats.rate_bps = rate;
  s_stats.seq++;
  portEXIT_CRITICAL(&s_stats_mux);
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

void set_probe_state(bool ok, bool has_ip, uint32_t ip_raw, uint32_t rtt_ms, int32_t error_code) {
  portENTER_CRITICAL(&s_probe_mux);
  s_probe.ok = ok;
  s_probe.has_ip = has_ip;
  s_probe.ip_raw = ip_raw;
  s_probe.last_rtt_ms = rtt_ms;
  s_probe.last_error = error_code;
  s_probe.last_probe_ms = millis();
  portEXIT_CRITICAL(&s_probe_mux);
}

void read_probe_snapshot(ProbeState* out) {
  if (out == nullptr) {
    return;
  }
  portENTER_CRITICAL(&s_probe_mux);
  *out = s_probe;
  portEXIT_CRITICAL(&s_probe_mux);
}

bool probe_server_reachability(bool force) {
  const uint32_t now_ms = millis();
  ProbeState snap = {};
  read_probe_snapshot(&snap);
  if (!force && snap.last_probe_ms != 0 && (now_ms - snap.last_probe_ms) < kProbeMinIntervalMs) {
    return snap.ok;
  }
  if (WiFi.status() != WL_CONNECTED) {
    set_probe_state(false, snap.has_ip, snap.ip_raw, 0, -30);
    return false;
  }
  IPAddress ip;
  if (!WiFi.hostByName(s_target.host, ip)) {
    set_probe_state(false, false, 0, 0, -31);
    return false;
  }
  WiFiClient probe_client;
  const uint32_t t0 = millis();
  bool ok = probe_client.connect(ip, s_target.port, kProbeConnectTimeoutMs);
  const uint32_t rtt = millis() - t0;
  if (ok) {
    probe_client.stop();
    set_probe_state(true, true, static_cast<uint32_t>(ip), rtt, 0);
    return true;
  }
  set_probe_state(false, true, static_cast<uint32_t>(ip), rtt, -32);
  return false;
}

bool connect_upload_client(WiFiClient& client, uint32_t timeout_ms) {
  if (probe_server_reachability(false)) {
    ProbeState snap = {};
    read_probe_snapshot(&snap);
    if (snap.has_ip) {
      IPAddress ip(snap.ip_raw);
      if (client.connect(ip, s_target.port, timeout_ms)) {
        return true;
      }
    }
  }
  return client.connect(s_target.host, s_target.port, timeout_ms);
}

void flush_sd_spi_clocks() {
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  delay(2);
  s_sd_spi.beginTransaction(SPISettings(SD_SPI_CLOCK_HZ, MSBFIRST, SPI_MODE0));
  for (int i = 0; i < 16; ++i) {
    s_sd_spi.transfer(0xFF);
  }
  s_sd_spi.endTransaction();
}

bool recover_sd(const char* reason) {
  Serial.print("SD recover: ");
  Serial.println((reason != nullptr) ? reason : "unknown");

  SD.end();
  s_sd_spi.end();
  delay(100);

  for (int attempt = 0; attempt < 6; ++attempt) {
    s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    flush_sd_spi_clocks();
    if (SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8)) {
      Serial.printf("SD recover ok on attempt %d\n", attempt + 1);
      return true;
    }
    SD.end();
    s_sd_spi.end();
    delay(150);
  }
  Serial.println("SD recover failed");
  return false;
}

bool init_sd() {
  for (int attempt = 0; attempt < 5; ++attempt) {
    s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    if (SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8)) {
      return true;
    }
    delay(150);
    s_sd_spi.end();
    delay(50);
  }
  return recover_sd("init_sd");
}

bool ensure_single_test_file(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  if (SD.exists(path)) {
    File existing = SD.open(path, FILE_READ);
    if (existing && existing.size() == static_cast<int64_t>(kCreateBytes)) {
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
    if (!recover_sd("ensure_single_test_file open write")) {
      return false;
    }
    f = SD.open(path, FILE_WRITE);
    if (!f) {
      return false;
    }
  }
  static uint8_t block[4096];
  for (size_t i = 0; i < sizeof(block); ++i) {
    block[i] = static_cast<uint8_t>(i & 0xFF);
  }
  size_t written = 0;
  while (written < kCreateBytes) {
    size_t n = min(sizeof(block), static_cast<size_t>(kCreateBytes - written));
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

bool ensure_test_files() {
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
  s_upload_queue[index].used = false;
  s_upload_queue[index].path[0] = '\0';
}

bool queue_add_or_bump(const char* path) {
  if (path == nullptr || path[0] == '\0') {
    return false;
  }
  int free_index = -1;
  for (size_t i = 0; i < kQueueLen; ++i) {
    if (s_upload_queue[i].used) {
      if (strcmp(s_upload_queue[i].path, path) == 0) {
        return true;
      }
    } else if (free_index < 0) {
      free_index = static_cast<int>(i);
    }
  }
  if (free_index < 0) {
    return false;
  }
  QueueItem& item = s_upload_queue[static_cast<size_t>(free_index)];
  strncpy(item.path, path, sizeof(item.path) - 1);
  item.path[sizeof(item.path) - 1] = '\0';
  item.used = true;
  return true;
}

bool queue_pop_next(char* out_path, size_t out_len) {
  if (out_path == nullptr || out_len == 0) {
    return false;
  }
  for (size_t i = 0; i < kQueueLen; ++i) {
    if (!s_upload_queue[i].used) {
      continue;
    }
    strncpy(out_path, s_upload_queue[i].path, out_len - 1);
    out_path[out_len - 1] = '\0';
    queue_remove(i);
    return true;
  }
  return false;
}

void queue_pending_scan() {
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (uploaded_state_contains(kTestFilePaths[i])) {
      continue;
    }
    portENTER_CRITICAL(&s_queue_mux);
    queue_add_or_bump(kTestFilePaths[i]);
    portEXIT_CRITICAL(&s_queue_mux);
  }
}

void print_upload_log(const char* tag, uint32_t run_id, int http_status, uint32_t elapsed_ms, const UploadStats& snap) {
  Serial.printf("UPLOAD_LOG tag=%s run=%lu ms=%lu state=%s sent=%llu total=%llu rate_bps=%lu error=%ld http=%d backlog=%lu zone=%s\n",
                tag,
                static_cast<unsigned long>(run_id),
                static_cast<unsigned long>(elapsed_ms),
                state_name(snap.state),
                static_cast<unsigned long long>(snap.bytes_sent),
                static_cast<unsigned long long>(snap.total_bytes),
                static_cast<unsigned long>(snap.rate_bps),
                static_cast<long>(snap.error_code),
                http_status,
                static_cast<unsigned long>(s_last_backlog_bytes.load(std::memory_order_relaxed)),
                zone_name(s_backlog_zone.load(std::memory_order_relaxed)));
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
  String boundary = String("----CANGrabberV3Boundary") + String(now_ms);

  const char* path = (upload_path != nullptr && upload_path[0] != '\0') ? upload_path : kTestFilePaths[0];
  const char* upload_name = strrchr(path, '/');
  upload_name = (upload_name != nullptr && *(upload_name + 1) != '\0') ? (upload_name + 1) : path;
  char idempotency_key[128];
  snprintf(idempotency_key, sizeof(idempotency_key), "v3-%s-%lu", upload_name, static_cast<unsigned long>(payload_size));

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
  *prefix += "Content-Disposition: form-data; name=\"source\"\r\n\r\nesp32-v3-storage-arbiter\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"size_bytes\"\r\n\r\n";
  *prefix += String(static_cast<unsigned long>(payload_size)) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"path\"\r\n\r\n";
  *prefix += String(path) + "\r\n";
  *prefix += "--" + boundary + "\r\n";
  *prefix += "Content-Disposition: form-data; name=\"updatefile\"; filename=\"";
  *prefix += upload_name;
  *prefix += "\"\r\n";
  *prefix += "Content-Type: application/octet-stream\r\n\r\n";

  *suffix = "\r\n--" + boundary + "--\r\n";

  const uint64_t content_len = static_cast<uint64_t>(prefix->length()) + payload_size + static_cast<uint64_t>(suffix->length());
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
  *headers += "\r\nConnection: close\r\n";
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

void stream_set_request_open(const char* path, uint64_t file_size) {
  portENTER_CRITICAL(&s_stream_mux);
  strncpy(s_stream.path, path, sizeof(s_stream.path) - 1);
  s_stream.path[sizeof(s_stream.path) - 1] = '\0';
  s_stream.total_file_bytes = file_size;
  s_stream.request_open = true;
  s_stream.request_stop = false;
  portEXIT_CRITICAL(&s_stream_mux);
}

void stream_set_request_stop() {
  portENTER_CRITICAL(&s_stream_mux);
  s_stream.request_stop = true;
  s_stream.request_open = false;
  portEXIT_CRITICAL(&s_stream_mux);
}

StreamControl stream_snapshot() {
  StreamControl snap{};
  portENTER_CRITICAL(&s_stream_mux);
  snap = s_stream;
  portEXIT_CRITICAL(&s_stream_mux);
  return snap;
}

void stream_set_active(bool active) {
  portENTER_CRITICAL(&s_stream_mux);
  s_stream.active = active;
  if (!active) {
    s_stream.request_open = false;
  }
  portEXIT_CRITICAL(&s_stream_mux);
}

void stream_clear_all() {
  portENTER_CRITICAL(&s_stream_mux);
  s_stream.active = false;
  s_stream.request_open = false;
  s_stream.request_stop = false;
  s_stream.total_file_bytes = 0;
  s_stream.path[0] = '\0';
  portEXIT_CRITICAL(&s_stream_mux);
}

void enqueue_mark_uploaded(const char* path) {
  if (s_mark_uploaded_q == nullptr || path == nullptr) {
    return;
  }
  MarkUploadedReq req = {};
  strncpy(req.path, path, sizeof(req.path) - 1);
  req.path[sizeof(req.path) - 1] = '\0';
  xQueueSend(s_mark_uploaded_q, &req, 0);
}

BacklogZone select_backlog_zone(uint32_t backlog_bytes) {
  BacklogZone current = s_backlog_zone.load(std::memory_order_relaxed);
  BacklogZone next = current;
  if (current == BacklogZone::RED) {
    next = (backlog_bytes <= kResumeBacklogBytes) ? BacklogZone::GREEN : BacklogZone::RED;
  } else {
    if (backlog_bytes >= kRedBacklogBytes) {
      next = BacklogZone::RED;
    } else if (backlog_bytes <= kGreenBacklogBytes) {
      next = BacklogZone::GREEN;
    } else {
      next = BacklogZone::YELLOW;
    }
  }
  s_backlog_zone.store(next, std::memory_order_relaxed);
  s_last_backlog_bytes.store(backlog_bytes, std::memory_order_relaxed);
  return next;
}

} // namespace
