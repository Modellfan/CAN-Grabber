#if defined(SD_HTTP_POST_SPEED_TEST)

#include <Arduino.h>
#include <AsyncTCP.h>
#include <SD.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hardware/hardware_config.h"

#if __has_include("../config/sd_http_post_speed_secrets.h")
#include "../config/sd_http_post_speed_secrets.h"
#elif __has_include("../../network-http/config/sd_http_download_secrets.h")
#include "../../network-http/config/sd_http_download_secrets.h"
#endif

#ifndef SD_HTTP_TEST_SSID
#define SD_HTTP_TEST_SSID ""
#endif

#ifndef SD_HTTP_TEST_PASSWORD
#define SD_HTTP_TEST_PASSWORD ""
#endif

#ifndef SD_HTTP_POST_TEST_URL
#define SD_HTTP_POST_TEST_URL "http://192.168.1.2:8080/upload"
#endif

namespace {

constexpr char kTestPath[] = "/sd_http_post_8mb.bin";
constexpr size_t kTestBytes = 8UL * 1024UL * 1024UL;
constexpr uint32_t kWifiConnectTimeoutMs = 15000;
constexpr uint32_t kConnectTimeoutMs = 8000;
constexpr uint32_t kWriteTimeoutMs = 90000;
constexpr uint32_t kResponseTimeoutMs = 15000;
constexpr uint32_t kHardRunTimeoutMs = 240000;
constexpr size_t kMaxBufferBytes = 16384;

SPIClass s_sd_spi(HSPI);
bool s_sd_ready = false;

struct ParsedUrl {
  char host[64];
  char path[96];
  uint16_t port;
  bool valid;
};

enum class WriteAlgo : uint8_t {
  kGreedy = 0,
  kBudgeted = 1,
};

enum class TransferMode : uint8_t {
  kContentLength = 0,
  kChunked = 1,
};

struct RunConfig {
  const char* name;
  WriteAlgo algo;
  TransferMode transfer_mode;
  bool keep_alive;
  size_t sd_read_bytes;
  size_t write_chunk_bytes;
  uint32_t max_busy_us;
  uint8_t yield_every_chunks;
};

struct RunStats {
  bool ok;
  int http_status;
  uint32_t elapsed_ms;
  uint32_t sd_read_ms;
  uint32_t sd_read_calls;
  uint32_t sd_read_us_avg;
  uint32_t sd_read_us_p95;
  uint32_t sd_read_us_p99;
  uint32_t sd_read_us_max;
  uint32_t max_block_us;
  uint32_t write_calls;
  uint32_t zero_writes;
  uint32_t tcp_no_space_events;
  uint32_t tcp_no_space_max_streak;
  uint32_t tcp_disconnect_events;
  uint32_t tcp_error_events;
  uint32_t callback_connect_count;
  uint32_t callback_ack_count;
  uint32_t callback_poll_count;
  uint32_t callback_data_count;
  uint32_t callback_error_count;
  uint32_t callback_disconnect_count;
  uint32_t callback_core_switches;
  int8_t callback_last_core;
  uint32_t perf_sample_count;
  float avg_core0_busy_pct;
  float avg_core1_busy_pct;
  float avg_loop_task_cpu_pct;
  float avg_async_task_cpu_pct;
  uint32_t runtime_delta_total;
  uint32_t task_state_running;
  uint32_t task_state_ready;
  uint32_t task_state_blocked;
  uint32_t task_state_suspended;
  uint32_t task_state_deleted;
  uint32_t min_free_heap_bytes;
  uint32_t min_largest_heap_block_bytes;
  uint32_t min_free_psram_bytes;
  int32_t min_wifi_rssi_dbm;
  uint32_t wifi_disconnect_events;
  uint32_t loop_stack_hw_min_words;
  uint32_t async_stack_hw_min_words;
  uint32_t idle0_stack_hw_min_words;
  uint32_t idle1_stack_hw_min_words;
  size_t sent_bytes;
  uint8_t fail_reason;
  uint8_t phase_at_fail;
};

const RunConfig kTargetSpeedRuns[] = {
    {"speed-close-len-16k", WriteAlgo::kGreedy, TransferMode::kContentLength, false, 16384, 16384, 0, 4},
    {"speed-close-len-8k", WriteAlgo::kGreedy, TransferMode::kContentLength, false, 8192, 8192, 0, 3},
    {"speed-budget-close-len-8k-4ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 8192, 4096, 4000, 1},
};

const RunConfig kTargetResponsiveRuns[] = {
    {"resp-close-len-8k-2ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 8192, 4096, 2000, 1},
    {"resp-close-len-4k-1ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 4096, 2048, 1000, 1},
    {"resp-close-len-2k-0.5ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 1024, 500, 1},
    {"resp-close-len-2k-0.25ms-512w", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 512, 250, 1},
};

const RunConfig kTargetChunkedRuns[] = {
    {"chunked-close-8k", WriteAlgo::kGreedy, TransferMode::kChunked, false, 8192, 8192, 0, 3},
    {"chunked-budget-close-4k-1ms", WriteAlgo::kBudgeted, TransferMode::kChunked, false, 4096, 2048, 1000, 1},
    {"chunked-keepalive-8k", WriteAlgo::kGreedy, TransferMode::kChunked, true, 8192, 8192, 0, 3},
};

const RunConfig kTargetTuningRuns[] = {
    {"tune-len-2k-768w-0.35ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 768, 350, 1},
    {"tune-len-2k-640w-0.30ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 640, 300, 1},
    {"tune-len-2k-512w-0.20ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 512, 200, 1},
    {"tune-len-2k-512w-0.28ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 512, 280, 1},
    {"tune-len-2k-512w-0.35ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 512, 350, 1},
    {"tune-len-2k-512w-0.50ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 512, 500, 1},
    {"tune-len-2k-512w-0.80ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 512, 800, 1},
    {"tune-len-2k-384w-0.24ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 384, 240, 1},
    {"tune-len-2k-256w-0.20ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 2048, 256, 200, 1},
    {"tune-len-4k-1536w-0.90ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 4096, 1536, 900, 1},
    {"tune-len-4k-1024w-0.75ms", WriteAlgo::kBudgeted, TransferMode::kContentLength, false, 4096, 1024, 750, 1},
    {"tune-chunked-2k-768w-0.35ms", WriteAlgo::kBudgeted, TransferMode::kChunked, false, 2048, 768, 350, 1},
    {"tune-chunked-2k-512w-0.28ms", WriteAlgo::kBudgeted, TransferMode::kChunked, false, 2048, 512, 280, 1},
};

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

bool connect_wifi() {
  if (strlen(SD_HTTP_TEST_SSID) == 0) {
    Serial.println("WiFi SSID is not configured");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.useStaticBuffers(true);
  WiFi.begin(SD_HTTP_TEST_SSID, SD_HTTP_TEST_PASSWORD);

  const uint32_t start_ms = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start_ms) < kWifiConnectTimeoutMs) {
    delay(200);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connect failed");
    return false;
  }

  Serial.print("WiFi connected: ");
  Serial.println(WiFi.localIP());
  return true;
}

bool init_sd() {
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  return SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8);
}

bool ensure_test_file() {
  if (SD.exists(kTestPath)) {
    File existing = SD.open(kTestPath, FILE_READ);
    if (existing) {
      const size_t size = existing.size();
      existing.close();
      if (size == kTestBytes) {
        Serial.println("8MB test file exists");
        return true;
      }
    }
    SD.remove(kTestPath);
  }

  File file = SD.open(kTestPath, FILE_WRITE);
  if (!file) {
    Serial.println("Failed to create test file");
    return false;
  }

  static uint8_t pattern[4096];
  for (size_t i = 0; i < sizeof(pattern); ++i) {
    pattern[i] = static_cast<uint8_t>(i & 0xFF);
  }

  size_t written = 0;
  while (written < kTestBytes) {
    const size_t to_write = min(sizeof(pattern), kTestBytes - written);
    const size_t out = file.write(pattern, to_write);
    if (out != to_write) {
      break;
    }
    written += out;
    if ((written % (1024UL * 1024UL)) == 0) {
      Serial.print("Create file progress: ");
      Serial.print(written / (1024UL * 1024UL));
      Serial.println(" MB");
      delay(0);
    }
  }
  file.flush();
  file.close();

  Serial.print("Created file bytes: ");
  Serial.println(written);
  return written == kTestBytes;
}

void update_block_stat(uint32_t busy_start_us, RunStats* stats) {
  if (stats == nullptr) {
    return;
  }
  const uint32_t now = micros();
  const uint32_t busy_us = now - busy_start_us;
  if (busy_us > stats->max_block_us) {
    stats->max_block_us = busy_us;
  }
}

int compare_u32(const void* lhs, const void* rhs) {
  const uint32_t a = *static_cast<const uint32_t*>(lhs);
  const uint32_t b = *static_cast<const uint32_t*>(rhs);
  if (a < b) {
    return -1;
  }
  if (a > b) {
    return 1;
  }
  return 0;
}

class AsyncUploadSession {
public:
  AsyncUploadSession(const ParsedUrl& parsed, const RunConfig& cfg)
      : parsed_(parsed), cfg_(cfg) {
    stats_.ok = false;
    stats_.http_status = 0;
    stats_.min_free_heap_bytes = UINT32_MAX;
    stats_.min_largest_heap_block_bytes = UINT32_MAX;
    stats_.min_free_psram_bytes = UINT32_MAX;
    stats_.min_wifi_rssi_dbm = INT32_MAX;
    stats_.loop_stack_hw_min_words = UINT32_MAX;
    stats_.async_stack_hw_min_words = UINT32_MAX;
    stats_.idle0_stack_hw_min_words = UINT32_MAX;
    stats_.idle1_stack_hw_min_words = UINT32_MAX;
    stats_.callback_last_core = -1;
  }

  ~AsyncUploadSession() {
    if (sd_read_us_samples_ != nullptr) {
      delete[] sd_read_us_samples_;
      sd_read_us_samples_ = nullptr;
    }
  }

  RunStats run() {
    start_ms_ = millis();
    last_activity_ms_ = start_ms_;
    last_perf_sample_ms_ = start_ms_;
    last_wifi_connected_ = (WiFi.status() == WL_CONNECTED);
    file_ = SD.open(kTestPath, FILE_READ);
    if (!file_) {
      mark_fail_reason(1);
      fail();
      return finalize();
    }

    const size_t expected_reads =
        (file_.size() / max(static_cast<size_t>(1), cfg_.sd_read_bytes)) + 64;
    sd_read_us_samples_cap_ = expected_reads > 20000 ? 20000 : expected_reads;
    if (sd_read_us_samples_cap_ > 0) {
      sd_read_us_samples_ = new uint32_t[sd_read_us_samples_cap_];
    }

    if (!WiFi.hostByName(parsed_.host, ip_)) {
      file_.close();
      mark_fail_reason(2);
      fail();
      return finalize();
    }

    build_request();
    bind_callbacks();
    client_.setNoDelay(true);
    client_.setAckTimeout(kWriteTimeoutMs + 8000);
    // Do not set AsyncTCP RX timeout while uploading request body.
    // RX timeout in AsyncTCP applies globally and can close a valid connection
    // if the server sends no inbound data until upload completion.
    client_.setRxTimeout(0);
    if (!client_.connect(ip_, parsed_.port)) {
      file_.close();
      mark_fail_reason(3);
      fail();
      return finalize();
    }

    while (!done_) {
      const uint32_t now = millis();
      if ((now - last_perf_sample_ms_) >= 1000) {
        sample_perf();
        last_perf_sample_ms_ = now;
      }
      if ((now - start_ms_) > kHardRunTimeoutMs) {
        mark_fail_reason(5);
        fail();
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
    }

    if (file_) {
      file_.close();
    }
    client_.close();
    return finalize();
  }

private:
  enum class BodySource : uint8_t {
    kPrefix = 0,
    kFile = 1,
    kSuffix = 2,
    kDone = 3,
  };

  enum class Phase : uint8_t {
    kSendHeaders = 0,
    kSendBody = 1,
    kSendChunkFinal = 2,
    kWaitResponse = 3,
    kComplete = 4,
    kFailed = 5,
  };


  static void on_connect(void* arg, AsyncClient* client) {
    static_cast<AsyncUploadSession*>(arg)->handle_connect(client);
  }
  static void on_disconnect(void* arg, AsyncClient* client) {
    static_cast<AsyncUploadSession*>(arg)->handle_disconnect(client);
  }
  static void on_ack(void* arg, AsyncClient* client, size_t len, uint32_t time) {
    (void)len;
    (void)time;
    static_cast<AsyncUploadSession*>(arg)->handle_ack(client);
  }
  static void on_poll(void* arg, AsyncClient* client) {
    static_cast<AsyncUploadSession*>(arg)->handle_poll(client);
  }
  static void on_error(void* arg, AsyncClient* client, int8_t error) {
    static_cast<AsyncUploadSession*>(arg)->handle_error(client, error);
  }
  static void on_data(void* arg, AsyncClient* client, void* data, size_t len) {
    static_cast<AsyncUploadSession*>(arg)->handle_data(client, data, len);
  }

  void bind_callbacks() {
    client_.onConnect(on_connect, this);
    client_.onDisconnect(on_disconnect, this);
    client_.onAck(on_ack, this);
    client_.onPoll(on_poll, this);
    client_.onError(on_error, this);
    client_.onData(on_data, this);
  }

  void build_request() {
    const size_t payload_size = file_.size();
    const uint32_t now_ms = millis();
    boundary_ = String("----CANGrabberSpeedTestBoundary") + String(now_ms);

    prefix_.reserve(512);
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"bus_id\"\r\n\r\n1\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"start_ms\"\r\n\r\n";
    prefix_ += String(now_ms) + "\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"end_ms\"\r\n\r\n";
    prefix_ += String(now_ms + 1000UL) + "\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"source\"\r\n\r\nesp32-http-post-speed-test\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"size_bytes\"\r\n\r\n";
    prefix_ += String(static_cast<unsigned long>(payload_size)) + "\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"checksum\"\r\n\r\n0\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"path\"\r\n\r\n";
    prefix_ += String(kTestPath) + "\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"flags\"\r\n\r\n0\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"device_id\"\r\n\r\nesp32-s3-speed-test\r\n";
    prefix_ += "--" + boundary_ + "\r\n";
    prefix_ += "Content-Disposition: form-data; name=\"updatefile\"; filename=\"sd_http_post_8mb.bin\"\r\n";
    prefix_ += "Content-Type: application/octet-stream\r\n\r\n";
    suffix_ = "\r\n--" + boundary_ + "--\r\n";

    const size_t content_length = prefix_.length() + payload_size + suffix_.length();
    headers_.reserve(420);
    headers_ += "POST ";
    headers_ += parsed_.path;
    headers_ += " HTTP/1.1\r\n";
    headers_ += "Host: ";
    headers_ += parsed_.host;
    headers_ += "\r\n";
    headers_ += cfg_.keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    headers_ += "Content-Type: multipart/form-data; boundary=";
    headers_ += boundary_;
    headers_ += "\r\n";
    if (cfg_.transfer_mode == TransferMode::kChunked) {
      headers_ += "Transfer-Encoding: chunked\r\n";
    } else {
      headers_ += "Content-Length: ";
      headers_ += String(static_cast<unsigned long>(content_length));
      headers_ += "\r\n";
    }
    headers_ += "X-Test-Config: ";
    headers_ += cfg_.name;
    headers_ += "\r\n\r\n";

    phase_ = Phase::kSendHeaders;
    src_ = BodySource::kPrefix;
    seg_ptr_ = reinterpret_cast<const uint8_t*>(headers_.c_str());
    seg_len_ = headers_.length();
    seg_off_ = 0;
  }

  void handle_connect(AsyncClient* client) {
    (void)client;
    stats_.callback_connect_count++;
    note_callback_core();
    connected_ = true;
    last_activity_ms_ = millis();
    pump_send();
  }

  void handle_ack(AsyncClient* client) {
    (void)client;
    stats_.callback_ack_count++;
    note_callback_core();
    pump_send();
  }

  void handle_poll(AsyncClient* client) {
    (void)client;
    stats_.callback_poll_count++;
    note_callback_core();
    pump_send();
  }

  void handle_disconnect(AsyncClient* client) {
    (void)client;
    stats_.callback_disconnect_count++;
    stats_.tcp_disconnect_events++;
    note_callback_core();
    if (!done_) {
      if (phase_ == Phase::kComplete) {
        done_ = true;
      } else if (header_parsed_ && response_body_ready()) {
        finish();
      } else {
        mark_fail_reason(6);
        fail();
      }
    }
  }

  void handle_data(AsyncClient* client, void* data, size_t len) {
    (void)client;
    if (data == nullptr || len == 0 || done_) {
      return;
    }
    stats_.callback_data_count++;
    note_callback_core();
    last_activity_ms_ = millis();
    const char* p = static_cast<const char*>(data);
    for (size_t i = 0; i < len; ++i) {
      rx_buf_ += p[i];
    }
    parse_response();
  }

  void handle_error(AsyncClient* client, int8_t error) {
    (void)client;
    (void)error;
    stats_.callback_error_count++;
    stats_.tcp_error_events++;
    note_callback_core();
    mark_fail_reason(7);
    fail();
  }

  void note_callback_core() {
    const int8_t core = static_cast<int8_t>(xPortGetCoreID());
    if (stats_.callback_last_core >= 0 && stats_.callback_last_core != core) {
      stats_.callback_core_switches++;
    }
    stats_.callback_last_core = core;
  }

  void sample_perf() {
    const bool wifi_connected = (WiFi.status() == WL_CONNECTED);
    if (!wifi_connected && last_wifi_connected_) {
      stats_.wifi_disconnect_events++;
    }
    last_wifi_connected_ = wifi_connected;
    if (wifi_connected) {
      const int32_t rssi = WiFi.RSSI();
      if (rssi < stats_.min_wifi_rssi_dbm) {
        stats_.min_wifi_rssi_dbm = rssi;
      }
    }

    const uint32_t free_heap = ESP.getFreeHeap();
    if (free_heap < stats_.min_free_heap_bytes) {
      stats_.min_free_heap_bytes = free_heap;
    }
    const uint32_t largest_block = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    if (largest_block < stats_.min_largest_heap_block_bytes) {
      stats_.min_largest_heap_block_bytes = largest_block;
    }
    const uint32_t free_psram = ESP.getFreePsram();
    if (free_psram < stats_.min_free_psram_bytes) {
      stats_.min_free_psram_bytes = free_psram;
    }

    TaskHandle_t loop_handle = xTaskGetHandle("loopTask");
    if (loop_handle != nullptr) {
      const uint32_t hw = uxTaskGetStackHighWaterMark(loop_handle);
      if (hw < stats_.loop_stack_hw_min_words) {
        stats_.loop_stack_hw_min_words = hw;
      }
    }
    TaskHandle_t async_handle = xTaskGetHandle("async_tcp");
    if (async_handle != nullptr) {
      const uint32_t hw = uxTaskGetStackHighWaterMark(async_handle);
      if (hw < stats_.async_stack_hw_min_words) {
        stats_.async_stack_hw_min_words = hw;
      }
    }
    TaskHandle_t idle0 = xTaskGetIdleTaskHandleForCPU(0);
    if (idle0 != nullptr) {
      const uint32_t hw = uxTaskGetStackHighWaterMark(idle0);
      if (hw < stats_.idle0_stack_hw_min_words) {
        stats_.idle0_stack_hw_min_words = hw;
      }
    }
    TaskHandle_t idle1 = xTaskGetIdleTaskHandleForCPU(1);
    if (idle1 != nullptr) {
      const uint32_t hw = uxTaskGetStackHighWaterMark(idle1);
      if (hw < stats_.idle1_stack_hw_min_words) {
        stats_.idle1_stack_hw_min_words = hw;
      }
    }

    uint32_t state_running = 0;
    uint32_t state_ready = 0;
    uint32_t state_blocked = 0;
    uint32_t state_suspended = 0;
    uint32_t state_deleted = 0;
    auto count_state = [&](TaskHandle_t handle) {
      if (handle == nullptr) {
        return;
      }
      const eTaskState st = eTaskGetState(handle);
      switch (st) {
        case eRunning:
          state_running++;
          break;
        case eReady:
          state_ready++;
          break;
        case eBlocked:
          state_blocked++;
          break;
        case eSuspended:
          state_suspended++;
          break;
        case eDeleted:
          state_deleted++;
          break;
        default:
          break;
      }
    };
    count_state(loop_handle);
    count_state(async_handle);
    count_state(idle0);
    count_state(idle1);
    stats_.task_state_running = state_running;
    stats_.task_state_ready = state_ready;
    stats_.task_state_blocked = state_blocked;
    stats_.task_state_suspended = state_suspended;
    stats_.task_state_deleted = state_deleted;
    stats_.perf_sample_count++;
  }

  void parse_response() {
    if (!header_parsed_) {
      const int split = rx_buf_.indexOf("\r\n\r\n");
      if (split < 0) {
        return;
      }
      const String head = rx_buf_.substring(0, split);
      parse_headers(head);
      rx_buf_.remove(0, split + 4);
      header_parsed_ = true;
    }
    if (!header_parsed_) {
      return;
    }
    response_body_bytes_ += rx_buf_.length();
    rx_buf_.remove(0);
    if (response_body_ready()) {
      finish();
    }
  }

  void parse_headers(const String& head) {
    int line_start = 0;
    bool first = true;
    while (line_start >= 0 && line_start < static_cast<int>(head.length())) {
      const int line_end = head.indexOf("\r\n", line_start);
      String line;
      if (line_end < 0) {
        line = head.substring(line_start);
        line_start = -1;
      } else {
        line = head.substring(line_start, line_end);
        line_start = line_end + 2;
      }
      if (first) {
        first = false;
        int status = 0;
        if (sscanf(line.c_str(), "HTTP/%*d.%*d %d", &status) == 1) {
          stats_.http_status = status;
        }
        continue;
      }
      String lower = line;
      lower.toLowerCase();
      if (lower.startsWith("content-length:")) {
        response_content_length_ = lower.substring(15).toInt();
      } else if (lower.startsWith("transfer-encoding:")) {
        if (lower.indexOf("chunked") > 0) {
          response_chunked_ = true;
        }
      } else if (lower.startsWith("connection:")) {
        if (lower.indexOf("close") > 0) {
          response_close_ = true;
        }
      }
    }
  }

  bool response_body_ready() const {
    if (response_content_length_ >= 0) {
      return response_body_bytes_ >= static_cast<size_t>(response_content_length_);
    }
    if (response_chunked_) {
      return false;
    }
    return response_close_;
  }

  bool prepare_next_data_segment() {
    while (true) {
      if (src_ == BodySource::kPrefix) {
        const size_t rem = prefix_.length() - prefix_off_;
        if (rem > 0) {
          seg_ptr_ = reinterpret_cast<const uint8_t*>(prefix_.c_str() + prefix_off_);
          seg_len_ = rem;
          seg_off_ = 0;
          prefix_off_ += rem;
          return true;
        }
        src_ = BodySource::kFile;
      } else if (src_ == BodySource::kFile) {
        const uint32_t read_start_ms = millis();
        const uint32_t read_start_us = micros();
        const size_t n = file_.read(file_buf_, cfg_.sd_read_bytes);
        stats_.sd_read_ms += millis() - read_start_ms;
        if (n > 0) {
          const uint32_t read_us = micros() - read_start_us;
          stats_.sd_read_calls++;
          sd_read_us_sum_ += read_us;
          if (read_us > stats_.sd_read_us_max) {
            stats_.sd_read_us_max = read_us;
          }
          if (sd_read_us_samples_ != nullptr &&
              sd_read_us_samples_count_ < sd_read_us_samples_cap_) {
            sd_read_us_samples_[sd_read_us_samples_count_++] = read_us;
          }
        }
        if (n > 0) {
          seg_ptr_ = file_buf_;
          seg_len_ = n;
          seg_off_ = 0;
          return true;
        }
        src_ = BodySource::kSuffix;
      } else if (src_ == BodySource::kSuffix) {
        const size_t rem = suffix_.length() - suffix_off_;
        if (rem > 0) {
          seg_ptr_ = reinterpret_cast<const uint8_t*>(suffix_.c_str() + suffix_off_);
          seg_len_ = rem;
          seg_off_ = 0;
          suffix_off_ += rem;
          return true;
        }
        src_ = BodySource::kDone;
        return false;
      } else {
        return false;
      }
    }
  }

  bool prepare_chunk_header(size_t len) {
    const int n = snprintf(chunk_head_, sizeof(chunk_head_), "%X\r\n", static_cast<unsigned>(len));
    if (n <= 0) {
      return false;
    }
    seg_ptr_ = reinterpret_cast<const uint8_t*>(chunk_head_);
    seg_len_ = static_cast<size_t>(n);
    seg_off_ = 0;
    return true;
  }

  void pump_send() {
    if (done_ || !connected_ || !client_.connected()) {
      return;
    }

    const uint32_t busy_start_us = micros();
    uint8_t loops = 0;
    while (loops < 8 && !done_) {
      loops++;
      if (phase_ == Phase::kWaitResponse) {
        break;
      }

      if (seg_off_ >= seg_len_) {
        if (phase_ == Phase::kSendHeaders) {
          phase_ = Phase::kSendBody;
        } else if (phase_ == Phase::kSendChunkFinal) {
          phase_ = Phase::kWaitResponse;
          break;
        } else if (phase_ == Phase::kSendBody) {
          if (cfg_.transfer_mode == TransferMode::kChunked) {
            if (!prepare_next_data_segment()) {
              prepare_chunk_header(0);
              seg_ptr_next_ = reinterpret_cast<const uint8_t*>("\r\n");
              seg_len_next_ = 2;
              seg_off_next_ = 0;
              phase_ = Phase::kSendChunkFinal;
            } else {
              chunk_payload_ptr_ = seg_ptr_;
              chunk_payload_len_ = seg_len_;
              chunk_payload_off_ = 0;
              if (!prepare_chunk_header(chunk_payload_len_)) {
                mark_fail_reason(8);
                fail();
                break;
              }
            }
          } else {
            if (!prepare_next_data_segment()) {
              phase_ = Phase::kWaitResponse;
              break;
            }
          }
        }
      }

      if (!client_.canSend() || client_.space() == 0) {
        stats_.zero_writes++;
        stats_.tcp_no_space_events++;
        tcp_no_space_streak_++;
        if (tcp_no_space_streak_ > stats_.tcp_no_space_max_streak) {
          stats_.tcp_no_space_max_streak = tcp_no_space_streak_;
        }
        break;
      }
      size_t remaining = seg_len_ - seg_off_;
      size_t chunk = min(cfg_.write_chunk_bytes, remaining);
      chunk = min(chunk, client_.space());
      if (chunk == 0) {
        stats_.zero_writes++;
        stats_.tcp_no_space_events++;
        tcp_no_space_streak_++;
        if (tcp_no_space_streak_ > stats_.tcp_no_space_max_streak) {
          stats_.tcp_no_space_max_streak = tcp_no_space_streak_;
        }
        break;
      }
      const size_t wrote = client_.write(reinterpret_cast<const char*>(seg_ptr_ + seg_off_), chunk);
      if (wrote == 0) {
        stats_.zero_writes++;
        stats_.tcp_no_space_events++;
        tcp_no_space_streak_++;
        if (tcp_no_space_streak_ > stats_.tcp_no_space_max_streak) {
          stats_.tcp_no_space_max_streak = tcp_no_space_streak_;
        }
        break;
      }
      tcp_no_space_streak_ = 0;
      seg_off_ += wrote;
      stats_.write_calls++;
      stats_.sent_bytes += wrote;
      last_activity_ms_ = millis();

      if (cfg_.transfer_mode == TransferMode::kChunked && phase_ == Phase::kSendBody) {
        if (chunk_payload_ptr_ != nullptr && chunk_payload_off_ < chunk_payload_len_) {
          if (seg_ptr_ == reinterpret_cast<const uint8_t*>(chunk_head_) && seg_off_ >= seg_len_) {
            seg_ptr_ = chunk_payload_ptr_;
            seg_len_ = chunk_payload_len_;
            seg_off_ = chunk_payload_off_;
          } else if (seg_ptr_ == chunk_payload_ptr_ && seg_off_ >= seg_len_) {
            static const uint8_t crlf[2] = {'\r', '\n'};
            seg_ptr_ = crlf;
            seg_len_ = 2;
            seg_off_ = 0;
            chunk_payload_ptr_ = nullptr;
            chunk_payload_len_ = 0;
            chunk_payload_off_ = 0;
          }
        }
      } else if (phase_ == Phase::kSendChunkFinal && seg_off_ >= seg_len_) {
        if (seg_ptr_next_ != nullptr) {
          seg_ptr_ = seg_ptr_next_;
          seg_len_ = seg_len_next_;
          seg_off_ = seg_off_next_;
          seg_ptr_next_ = nullptr;
          seg_len_next_ = 0;
          seg_off_next_ = 0;
        }
      }
    }

    update_block_stat(busy_start_us, &stats_);
  }

  void finish() {
    phase_ = Phase::kComplete;
    done_ = true;
    stats_.ok = stats_.http_status >= 200 && stats_.http_status < 300;
  }

  void fail() {
    stats_.phase_at_fail = static_cast<uint8_t>(phase_);
    phase_ = Phase::kFailed;
    done_ = true;
    stats_.ok = false;
  }

  void mark_fail_reason(uint8_t reason) {
    if (stats_.fail_reason == 0) {
      stats_.fail_reason = reason;
    }
  }

  RunStats finalize() {
    sample_perf();
    if (stats_.perf_sample_count > 0) {
      const float denom = static_cast<float>(stats_.perf_sample_count);
      stats_.avg_core0_busy_pct /= denom;
      stats_.avg_core1_busy_pct /= denom;
      stats_.avg_loop_task_cpu_pct /= denom;
      stats_.avg_async_task_cpu_pct /= denom;
    }
    if (stats_.sd_read_calls > 0) {
      stats_.sd_read_us_avg =
          static_cast<uint32_t>(sd_read_us_sum_ / stats_.sd_read_calls);
    }
    if (sd_read_us_samples_ != nullptr && sd_read_us_samples_count_ > 0) {
      uint32_t* sorted = new uint32_t[sd_read_us_samples_count_];
      if (sorted != nullptr) {
        memcpy(sorted,
               sd_read_us_samples_,
               sd_read_us_samples_count_ * sizeof(uint32_t));
        qsort(sorted,
              sd_read_us_samples_count_,
              sizeof(uint32_t),
              compare_u32);
        const size_t idx95 = (sd_read_us_samples_count_ * 95) / 100;
        const size_t idx99 = (sd_read_us_samples_count_ * 99) / 100;
        stats_.sd_read_us_p95 = sorted[min(idx95, sd_read_us_samples_count_ - 1)];
        stats_.sd_read_us_p99 = sorted[min(idx99, sd_read_us_samples_count_ - 1)];
        delete[] sorted;
      }
    }
    if (stats_.min_free_heap_bytes == UINT32_MAX) {
      stats_.min_free_heap_bytes = ESP.getFreeHeap();
    }
    if (stats_.min_largest_heap_block_bytes == UINT32_MAX) {
      stats_.min_largest_heap_block_bytes =
          heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    }
    if (stats_.min_free_psram_bytes == UINT32_MAX) {
      stats_.min_free_psram_bytes = ESP.getFreePsram();
    }
    if (stats_.min_wifi_rssi_dbm == INT32_MAX) {
      stats_.min_wifi_rssi_dbm = WiFi.RSSI();
    }
    if (stats_.loop_stack_hw_min_words == UINT32_MAX) {
      stats_.loop_stack_hw_min_words = 0;
    }
    if (stats_.async_stack_hw_min_words == UINT32_MAX) {
      stats_.async_stack_hw_min_words = 0;
    }
    if (stats_.idle0_stack_hw_min_words == UINT32_MAX) {
      stats_.idle0_stack_hw_min_words = 0;
    }
    if (stats_.idle1_stack_hw_min_words == UINT32_MAX) {
      stats_.idle1_stack_hw_min_words = 0;
    }
    stats_.elapsed_ms = millis() - start_ms_;
    return stats_;
  }

  const ParsedUrl parsed_;
  const RunConfig cfg_;
  RunStats stats_{};
  AsyncClient client_;
  IPAddress ip_;
  File file_;

  String boundary_;
  String headers_;
  String prefix_;
  String suffix_;
  String rx_buf_;

  uint8_t file_buf_[kMaxBufferBytes] = {0};
  char chunk_head_[24] = {0};
  Phase phase_ = Phase::kSendHeaders;
  BodySource src_ = BodySource::kPrefix;
  bool connected_ = false;
  bool done_ = false;
  bool header_parsed_ = false;
  bool response_chunked_ = false;
  bool response_close_ = false;
  int32_t response_content_length_ = -1;
  size_t response_body_bytes_ = 0;
  uint32_t start_ms_ = 0;
  uint32_t last_activity_ms_ = 0;
  uint32_t last_perf_sample_ms_ = 0;
  bool last_wifi_connected_ = false;

  const uint8_t* seg_ptr_ = nullptr;
  size_t seg_len_ = 0;
  size_t seg_off_ = 0;

  const uint8_t* seg_ptr_next_ = nullptr;
  size_t seg_len_next_ = 0;
  size_t seg_off_next_ = 0;

  const uint8_t* chunk_payload_ptr_ = nullptr;
  size_t chunk_payload_len_ = 0;
  size_t chunk_payload_off_ = 0;
  uint32_t tcp_no_space_streak_ = 0;

  size_t prefix_off_ = 0;
  size_t suffix_off_ = 0;

  uint64_t sd_read_us_sum_ = 0;
  uint32_t* sd_read_us_samples_ = nullptr;
  size_t sd_read_us_samples_cap_ = 0;
  size_t sd_read_us_samples_count_ = 0;

};

RunStats run_single(const ParsedUrl& parsed, const RunConfig& cfg) {
  AsyncUploadSession* session = new AsyncUploadSession(parsed, cfg);
  if (session == nullptr) {
    RunStats stats{};
    stats.ok = false;
    stats.http_status = 0;
    stats.elapsed_ms = 0;
    return stats;
  }
  RunStats stats = session->run();
  delete session;
  return stats;
}

void print_result(const RunConfig& cfg, const RunStats& stats) {
  const float elapsed_s = static_cast<float>(stats.elapsed_ms) / 1000.0f;
  const float mbps = elapsed_s > 0.0f
                         ? (static_cast<float>(stats.sent_bytes) / (1024.0f * 1024.0f)) / elapsed_s
                         : 0.0f;

  Serial.println("----------------------------------------");
  Serial.print("Run: ");
  Serial.println(cfg.name);
  Serial.print("Algo: ");
  Serial.println(cfg.algo == WriteAlgo::kGreedy ? "greedy" : "budgeted");
  Serial.print("Transfer: ");
  Serial.println(cfg.transfer_mode == TransferMode::kChunked ? "chunked" : "content-length");
  Serial.print("Keep-Alive: ");
  Serial.println(cfg.keep_alive ? "on" : "off");
  Serial.print("Read/write chunk: ");
  Serial.print(cfg.sd_read_bytes);
  Serial.print(" / ");
  Serial.println(cfg.write_chunk_bytes);
  Serial.print("Result: ");
  Serial.println(stats.ok ? "OK" : "FAILED");
  Serial.print("HTTP status: ");
  Serial.println(stats.http_status);
  Serial.print("Fail reason / phase: ");
  Serial.print(static_cast<unsigned>(stats.fail_reason));
  Serial.print(" / ");
  Serial.println(static_cast<unsigned>(stats.phase_at_fail));
  Serial.print("Sent bytes: ");
  Serial.println(static_cast<unsigned long>(stats.sent_bytes));
  Serial.print("Elapsed ms: ");
  Serial.println(stats.elapsed_ms);
  Serial.print("Upload MB/s: ");
  Serial.println(mbps, 2);
  Serial.print("SD read ms total: ");
  Serial.println(stats.sd_read_ms);
  Serial.print("Max blocking us (between yields): ");
  Serial.println(stats.max_block_us);
  Serial.print("write calls / zero writes: ");
  Serial.print(stats.write_calls);
  Serial.print(" / ");
  Serial.println(stats.zero_writes);
  Serial.print("TCP no-space events/max streak: ");
  Serial.print(stats.tcp_no_space_events);
  Serial.print(" / ");
  Serial.println(stats.tcp_no_space_max_streak);
  Serial.print("TCP disconnect/error events: ");
  Serial.print(stats.tcp_disconnect_events);
  Serial.print(" / ");
  Serial.println(stats.tcp_error_events);
  Serial.print("Callbacks C/A/P/D/E/X: ");
  Serial.print(stats.callback_connect_count);
  Serial.print("/");
  Serial.print(stats.callback_ack_count);
  Serial.print("/");
  Serial.print(stats.callback_poll_count);
  Serial.print("/");
  Serial.print(stats.callback_data_count);
  Serial.print("/");
  Serial.print(stats.callback_error_count);
  Serial.print("/");
  Serial.println(stats.callback_disconnect_count);
  Serial.print("Callback core switches / last core: ");
  Serial.print(stats.callback_core_switches);
  Serial.print(" / ");
  Serial.println(stats.callback_last_core);
  Serial.print("SD read calls/us avg/p95/p99/max: ");
  Serial.print(stats.sd_read_calls);
  Serial.print(" / ");
  Serial.print(stats.sd_read_us_avg);
  Serial.print(" / ");
  Serial.print(stats.sd_read_us_p95);
  Serial.print(" / ");
  Serial.print(stats.sd_read_us_p99);
  Serial.print(" / ");
  Serial.println(stats.sd_read_us_max);
  Serial.print("CPU avg core0/core1 busy %: ");
  Serial.print(stats.avg_core0_busy_pct, 1);
  Serial.print(" / ");
  Serial.println(stats.avg_core1_busy_pct, 1);
  Serial.print("CPU avg loop/async task %: ");
  Serial.print(stats.avg_loop_task_cpu_pct, 1);
  Serial.print(" / ");
  Serial.println(stats.avg_async_task_cpu_pct, 1);
  Serial.print("Task states run/ready/blocked/susp/deleted: ");
  Serial.print(stats.task_state_running);
  Serial.print("/");
  Serial.print(stats.task_state_ready);
  Serial.print("/");
  Serial.print(stats.task_state_blocked);
  Serial.print("/");
  Serial.print(stats.task_state_suspended);
  Serial.print("/");
  Serial.println(stats.task_state_deleted);
  Serial.print("Runtime delta total: ");
  Serial.println(stats.runtime_delta_total);
  Serial.print("Heap min free/largest block (bytes): ");
  Serial.print(stats.min_free_heap_bytes);
  Serial.print(" / ");
  Serial.println(stats.min_largest_heap_block_bytes);
  Serial.print("PSRAM min free (bytes): ");
  Serial.println(stats.min_free_psram_bytes);
  Serial.print("WiFi min RSSI / disconnect events: ");
  Serial.print(stats.min_wifi_rssi_dbm);
  Serial.print(" / ");
  Serial.println(stats.wifi_disconnect_events);
  Serial.print("Stack HW min loop/async/idle0/idle1 (words): ");
  Serial.print(stats.loop_stack_hw_min_words);
  Serial.print(" / ");
  Serial.print(stats.async_stack_hw_min_words);
  Serial.print(" / ");
  Serial.print(stats.idle0_stack_hw_min_words);
  Serial.print(" / ");
  Serial.println(stats.idle1_stack_hw_min_words);
}

void run_suite(const ParsedUrl& parsed,
               const char* suite_name,
               const RunConfig* runs,
               size_t run_count) {
  Serial.println();
  Serial.print("===== START SUITE: ");
  Serial.print(suite_name);
  Serial.println(" =====");

  for (size_t i = 0; i < run_count; ++i) {
    const RunConfig& cfg = runs[i];
    RunStats stats = run_single(parsed, cfg);
    print_result(cfg, stats);
    vTaskDelay(pdMS_TO_TICKS(1200));
  }

  Serial.print("===== END SUITE: ");
  Serial.print(suite_name);
  Serial.println(" =====");
}

void print_help() {
  Serial.println();
  Serial.println("Commands:");
  Serial.println("  speed       -> run maximize-throughput target");
  Serial.println("  chunked     -> run chunked-transfer compatibility target");
  Serial.println("  tune        -> run fine-grained tuning target");
  Serial.println("  responsive  -> run low-blocking target");
  Serial.println("  both        -> run all targets");
  Serial.println("  reset       -> reboot MCU (fresh run from boot)");
  Serial.println("  help        -> print commands");
}

void handle_serial(const ParsedUrl& parsed) {
  static char cmd[24];
  static size_t len = 0;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      cmd[len] = '\0';
      if (len > 0) {
        if (strcmp(cmd, "speed") == 0) {
          run_suite(parsed, "maximize-upload-speed", kTargetSpeedRuns,
                    sizeof(kTargetSpeedRuns) / sizeof(kTargetSpeedRuns[0]));
        } else if (strcmp(cmd, "chunked") == 0) {
          run_suite(parsed, "chunked-compatibility", kTargetChunkedRuns,
                    sizeof(kTargetChunkedRuns) / sizeof(kTargetChunkedRuns[0]));
        } else if (strcmp(cmd, "tune") == 0) {
          run_suite(parsed, "tuning-parameter-sweep", kTargetTuningRuns,
                    sizeof(kTargetTuningRuns) / sizeof(kTargetTuningRuns[0]));
        } else if (strcmp(cmd, "responsive") == 0) {
          run_suite(parsed, "minimize-blocking", kTargetResponsiveRuns,
                    sizeof(kTargetResponsiveRuns) / sizeof(kTargetResponsiveRuns[0]));
        } else if (strcmp(cmd, "both") == 0) {
          run_suite(parsed, "maximize-upload-speed", kTargetSpeedRuns,
                    sizeof(kTargetSpeedRuns) / sizeof(kTargetSpeedRuns[0]));
          run_suite(parsed, "chunked-compatibility", kTargetChunkedRuns,
                    sizeof(kTargetChunkedRuns) / sizeof(kTargetChunkedRuns[0]));
          run_suite(parsed, "tuning-parameter-sweep", kTargetTuningRuns,
                    sizeof(kTargetTuningRuns) / sizeof(kTargetTuningRuns[0]));
          run_suite(parsed, "minimize-blocking", kTargetResponsiveRuns,
                    sizeof(kTargetResponsiveRuns) / sizeof(kTargetResponsiveRuns[0]));
        } else if (strcmp(cmd, "reset") == 0) {
          Serial.println("Rebooting MCU...");
          delay(50);
          ESP.restart();
        } else {
          print_help();
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
  Serial.println("SD HTTP POST speed test");

  ParsedUrl parsed{};
  if (!parse_http_url(SD_HTTP_POST_TEST_URL, &parsed) || !parsed.valid) {
    Serial.print("Invalid SD_HTTP_POST_TEST_URL: ");
    Serial.println(SD_HTTP_POST_TEST_URL);
    return;
  }

  Serial.print("Target URL: ");
  Serial.println(SD_HTTP_POST_TEST_URL);

  if (!connect_wifi()) {
    return;
  }

  s_sd_ready = init_sd();
  if (!s_sd_ready) {
    Serial.println("SD init failed");
    return;
  }

  if (!ensure_test_file()) {
    Serial.println("8MB test file setup failed");
    return;
  }

  print_help();

  run_suite(parsed, "maximize-upload-speed", kTargetSpeedRuns,
            sizeof(kTargetSpeedRuns) / sizeof(kTargetSpeedRuns[0]));
  run_suite(parsed, "chunked-compatibility", kTargetChunkedRuns,
            sizeof(kTargetChunkedRuns) / sizeof(kTargetChunkedRuns[0]));
  run_suite(parsed, "tuning-parameter-sweep", kTargetTuningRuns,
            sizeof(kTargetTuningRuns) / sizeof(kTargetTuningRuns[0]));
  run_suite(parsed, "minimize-blocking", kTargetResponsiveRuns,
            sizeof(kTargetResponsiveRuns) / sizeof(kTargetResponsiveRuns[0]));
}

void loop() {
  static ParsedUrl parsed{};
  static bool parsed_ok = false;
  if (!parsed_ok) {
    parsed_ok = parse_http_url(SD_HTTP_POST_TEST_URL, &parsed) && parsed.valid;
  }
  if (parsed_ok) {
    handle_serial(parsed);
  }
  delay(20);
}

#endif // SD_HTTP_POST_SPEED_TEST
