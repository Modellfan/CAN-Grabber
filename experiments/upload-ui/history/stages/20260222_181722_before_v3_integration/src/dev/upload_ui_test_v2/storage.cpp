#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
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

#ifndef SD_HTTP_UPLOAD_TEST_API_TOKEN
#define SD_HTTP_UPLOAD_TEST_API_TOKEN ""
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
constexpr size_t kTestFileCount = 8;
constexpr size_t kQueueLen = 32;
constexpr uint32_t kAutoScanIntervalMs = 4000;
constexpr uint32_t kBaseBackoffMs = 2000;
constexpr uint32_t kMaxBackoffMs = 60000;
constexpr uint32_t kProbeMinIntervalMs = 3000;
constexpr uint32_t kProbeConnectTimeoutMs = 2500;
constexpr char kUploadedStatePath[] = "/upload_state_v2.txt";
constexpr char kUploadPath[] = "/sd_http_post_8mb_01.bin";
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

struct ProbeState {
  bool has_ip;
  bool ok;
  uint32_t ip_raw;
  uint32_t last_probe_ms;
  uint32_t last_rtt_ms;
  int32_t last_error;
};

struct UploaderContractState {
  bool initialized;
  uint32_t current_file_size_bytes;
  uint32_t total_uploaded_files;
  uint64_t total_uploaded_bytes;
  bool last_error;
  bool last_error_interrupted;
  bool last_error_connect;
  char last_error_message[96];
};

struct UploaderContractStats {
  bool initialized;
  bool uploading;
  uint32_t upload_speed_bytes_per_sec;
  uint32_t current_file_size_bytes;
  uint32_t current_file_sent_bytes;
  uint32_t total_uploaded_files;
  uint64_t total_uploaded_bytes;
  uint32_t uploaded_files;
  uint32_t outstanding_files;
  uint64_t outstanding_bytes;
  bool last_error;
  bool last_error_interrupted;
  bool last_error_connect;
  char last_error_message[96];
  bool server_reachable_known;
  bool server_reachable;
  int32_t server_rtt_ms;
  char server_reach_message[96];
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
portMUX_TYPE s_probe_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_uploader_contract_mux = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE s_uploader_stats_cache_mux = portMUX_INITIALIZER_UNLOCKED;
char s_active_upload_path[96] = {0};
ProbeState s_probe = {false, false, 0, 0, 0, 0};
UploaderContractState s_uploader_contract = {false, 0, 0, 0, false, false, false, {0}};
UploaderContractStats s_uploader_stats_cache = {};

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
    "const pOk=Number(o.probe_ok||0);const pIp=String(o.probe_ip||'n/a');"
    "const pAge=Number(o.probe_age_ms||0);const pRtt=Number(o.probe_rtt_ms||0);const pErr=Number(o.probe_err||0);"
    "s.textContent='state='+o.state+'\\nsent='+sent+'\\ntotal='+o.total+'\\nerr='+o.error+"
    "'\\nprobe_ok='+pOk+'\\nprobe_ip='+pIp+'\\nprobe_age_ms='+pAge+'\\nprobe_rtt_ms='+pRtt+'\\nprobe_err='+pErr;}"
    "function poll(){fetch('/status').then(x=>x.json()).then(draw).catch(()=>{});}poll();setInterval(poll,250);</script></body></html>";

bool uploaded_state_contains(const char* path);
void read_probe_snapshot(ProbeState* out);

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
  char idempotency_key[128];
  snprintf(idempotency_key, sizeof(idempotency_key), "v2-%s-%lu", upload_name, static_cast<unsigned long>(payload_size));
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

void uploader_set_initialized(bool initialized) {
  portENTER_CRITICAL(&s_uploader_contract_mux);
  s_uploader_contract.initialized = initialized;
  portEXIT_CRITICAL(&s_uploader_contract_mux);
}

void uploader_set_current_file_size(uint32_t bytes) {
  portENTER_CRITICAL(&s_uploader_contract_mux);
  s_uploader_contract.current_file_size_bytes = bytes;
  portEXIT_CRITICAL(&s_uploader_contract_mux);
}

void uploader_set_error(bool interrupted, bool connect_error, const char* message) {
  portENTER_CRITICAL(&s_uploader_contract_mux);
  s_uploader_contract.last_error = true;
  s_uploader_contract.last_error_interrupted = interrupted;
  s_uploader_contract.last_error_connect = connect_error;
  if (message != nullptr) {
    strncpy(s_uploader_contract.last_error_message, message, sizeof(s_uploader_contract.last_error_message) - 1);
    s_uploader_contract.last_error_message[sizeof(s_uploader_contract.last_error_message) - 1] = '\0';
  } else {
    s_uploader_contract.last_error_message[0] = '\0';
  }
  portEXIT_CRITICAL(&s_uploader_contract_mux);
}

void uploader_clear_error() {
  portENTER_CRITICAL(&s_uploader_contract_mux);
  s_uploader_contract.last_error = false;
  s_uploader_contract.last_error_interrupted = false;
  s_uploader_contract.last_error_connect = false;
  s_uploader_contract.last_error_message[0] = '\0';
  portEXIT_CRITICAL(&s_uploader_contract_mux);
}

void uploader_note_success(uint32_t uploaded_bytes) {
  portENTER_CRITICAL(&s_uploader_contract_mux);
  s_uploader_contract.total_uploaded_files++;
  s_uploader_contract.total_uploaded_bytes += uploaded_bytes;
  portEXIT_CRITICAL(&s_uploader_contract_mux);
}

uint32_t uploaded_file_count() {
  uint32_t count = 0;
  for (size_t i = 0; i < kTestFileCount; ++i) {
    if (uploaded_state_contains(kTestFilePaths[i])) {
      ++count;
    }
  }
  return count;
}

void compute_outstanding(uint32_t* out_files, uint64_t* out_bytes) {
  if (out_files == nullptr || out_bytes == nullptr) {
    return;
  }
  uint32_t files = 0;
  uint64_t bytes = 0;
  for (size_t i = 0; i < kTestFileCount; ++i) {
    const char* path = kTestFilePaths[i];
    if (uploaded_state_contains(path)) {
      continue;
    }
    ++files;
    File f = SD.open(path, FILE_READ);
    if (f) {
      bytes += static_cast<uint64_t>(f.size());
      f.close();
    }
  }
  *out_files = files;
  *out_bytes = bytes;
}

void server_reach_message_from_probe(const ProbeState& probe, char* out, size_t out_len) {
  if (out == nullptr || out_len == 0) {
    return;
  }
  if (probe.last_probe_ms == 0) {
    strncpy(out, "not_probed_yet", out_len - 1);
    out[out_len - 1] = '\0';
    return;
  }
  if (probe.ok) {
    if (probe.last_rtt_ms > 0) {
      snprintf(out, out_len, "reachable rtt=%lums", static_cast<unsigned long>(probe.last_rtt_ms));
    } else {
      strncpy(out, "reachable", out_len - 1);
      out[out_len - 1] = '\0';
    }
    return;
  }
  snprintf(out, out_len, "unreachable err=%ld", static_cast<long>(probe.last_error));
}

UploaderContractStats build_uploader_contract_stats() {
  UploaderContractStats out = {};

  UploadStats snap = read_stats_snapshot();
  ProbeState probe = {};
  read_probe_snapshot(&probe);
  UploaderContractState local = {};
  portENTER_CRITICAL(&s_uploader_contract_mux);
  local = s_uploader_contract;
  portEXIT_CRITICAL(&s_uploader_contract_mux);

  out.initialized = local.initialized;
  out.uploading = (snap.state == UploadState::CONNECTING || snap.state == UploadState::SENDING ||
                   snap.state == UploadState::FINALIZING);
  out.upload_speed_bytes_per_sec = snap.lastRateBps;
  out.current_file_size_bytes = local.current_file_size_bytes;
  out.current_file_sent_bytes = static_cast<uint32_t>(min<uint64_t>(snap.bytesSent, local.current_file_size_bytes));
  out.total_uploaded_files = local.total_uploaded_files;
  out.total_uploaded_bytes = local.total_uploaded_bytes;
  out.uploaded_files = uploaded_file_count();
  compute_outstanding(&out.outstanding_files, &out.outstanding_bytes);
  out.last_error = local.last_error;
  out.last_error_interrupted = local.last_error_interrupted;
  out.last_error_connect = local.last_error_connect;
  strncpy(out.last_error_message, local.last_error_message, sizeof(out.last_error_message) - 1);
  out.last_error_message[sizeof(out.last_error_message) - 1] = '\0';
  out.server_reachable_known = (probe.last_probe_ms != 0);
  out.server_reachable = probe.ok;
  out.server_rtt_ms = out.server_reachable_known ? static_cast<int32_t>(probe.last_rtt_ms) : -1;
  server_reach_message_from_probe(probe, out.server_reach_message, sizeof(out.server_reach_message));

  return out;
}

void refresh_uploader_contract_stats() {
  UploaderContractStats latest = build_uploader_contract_stats();
  portENTER_CRITICAL(&s_uploader_stats_cache_mux);
  s_uploader_stats_cache = latest;
  portEXIT_CRITICAL(&s_uploader_stats_cache_mux);
}

UploaderContractStats read_uploader_contract_stats() {
  UploaderContractStats out = {};
  portENTER_CRITICAL(&s_uploader_stats_cache_mux);
  out = s_uploader_stats_cache;
  portEXIT_CRITICAL(&s_uploader_stats_cache_mux);
  return out;
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

bool get_probe_cached_ip(IPAddress* out_ip) {
  if (out_ip == nullptr) {
    return false;
  }
  bool has_ip = false;
  uint32_t ip_raw = 0;
  portENTER_CRITICAL(&s_probe_mux);
  has_ip = s_probe.has_ip;
  ip_raw = s_probe.ip_raw;
  portEXIT_CRITICAL(&s_probe_mux);
  if (!has_ip || ip_raw == 0) {
    return false;
  }
  *out_ip = IPAddress(ip_raw);
  return true;
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
  bool have_cached = get_probe_cached_ip(&ip);
  if (!have_cached) {
    if (!WiFi.hostByName(s_target.host, ip)) {
      set_probe_state(false, false, 0, 0, -31);
      return false;
    }
  }

  WiFiClient probe_client;
  const uint32_t t0 = millis();
  bool ok = probe_client.connect(ip, s_target.port, kProbeConnectTimeoutMs);
  const uint32_t rtt_ms = millis() - t0;
  if (ok) {
    probe_client.stop();
    set_probe_state(true, true, static_cast<uint32_t>(ip), rtt_ms, 0);
    return true;
  }

  // Refresh DNS once if cached path failed.
  IPAddress refreshed;
  if (have_cached && WiFi.hostByName(s_target.host, refreshed)) {
    WiFiClient retry_client;
    const uint32_t t1 = millis();
    ok = retry_client.connect(refreshed, s_target.port, kProbeConnectTimeoutMs);
    const uint32_t retry_rtt = millis() - t1;
    if (ok) {
      retry_client.stop();
      set_probe_state(true, true, static_cast<uint32_t>(refreshed), retry_rtt, 0);
      return true;
    }
    set_probe_state(false, true, static_cast<uint32_t>(refreshed), retry_rtt, -32);
    return false;
  }

  set_probe_state(false, true, static_cast<uint32_t>(ip), rtt_ms, -32);
  return false;
}

bool connect_upload_client(WiFiClient& client, uint32_t timeout_ms) {
  probe_server_reachability(false);

  IPAddress ip;
  if (get_probe_cached_ip(&ip)) {
    if (client.connect(ip, s_target.port, timeout_ms)) {
      return true;
    }
  }

  if (probe_server_reachability(true) && get_probe_cached_ip(&ip)) {
    if (client.connect(ip, s_target.port, timeout_ms)) {
      return true;
    }
  }

  const bool ok = client.connect(s_target.host, s_target.port, timeout_ms);
  if (ok) {
    IPAddress resolved;
    if (WiFi.hostByName(s_target.host, resolved)) {
      set_probe_state(true, true, static_cast<uint32_t>(resolved), 0, 0);
    } else {
      set_probe_state(true, false, 0, 0, 0);
    }
  } else {
    set_probe_state(false, false, 0, 0, -33);
  }
  return ok;
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

bool is_http_retryable(int status) {
  if (status == 429 || status == 503) {
    return true;
  }
  if (status >= 500) {
    return true;
  }
  return false;
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

