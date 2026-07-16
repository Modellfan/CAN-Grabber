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

#if __has_include("../../../config/sd_http_upload_ui_secrets.h")
#include "../../../config/sd_http_upload_ui_secrets.h"
#elif __has_include("../../../../http-upload-performance/config/sd_http_post_speed_secrets.h")
#include "../../../../http-upload-performance/config/sd_http_post_speed_secrets.h"
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
// ====================================================================================================
// Task Shared Data Section
// ====================================================================================================
// Shared module state used across uploader/webserver/serial interface.

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
constexpr uint8_t kSdRecoverMaxAttempts = 3;
constexpr uint32_t kSdRecoverSettleMs = 25;
constexpr uint8_t kSdClockFlushTransfers = 32;

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
bool recover_sd(const char* reason, uint8_t attempts = kSdRecoverMaxAttempts);
bool recover_sd_safe(const char* reason, uint8_t attempts = kSdRecoverMaxAttempts);

SPIClass s_sd_spi(HSPI);
WebServer s_server(80);
TaskHandle_t s_upload_task_handle = nullptr;
TaskHandle_t s_server_task_handle = nullptr;
TaskHandle_t s_prefetch_task_handle = nullptr;
TaskHandle_t s_monitor_task_handle = nullptr;
std::atomic<bool> s_upload_requested{false};
std::atomic<bool> s_abort_requested{false};
std::atomic<uint32_t> s_run_counter{0};
std::atomic<uint32_t> s_active_run_id{0};
std::atomic<uint32_t> s_terminal_state_until_ms{0};
std::atomic<bool> s_sd_recovering{false};
std::atomic<bool> s_sd_available{false};
RingBuffer s_ring = {};

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
    "function fmtMB(bytes){return (Number(bytes||0)/1048576).toFixed(2);}"
    "function fmtRate(bps){if(bps>=1048576)return (bps/1048576).toFixed(2)+' MB/s';"
    "if(bps>=1024)return (bps/1024).toFixed(1)+' KB/s';return bps+' B/s';}"
    "function fmtEta(sec){sec=Number(sec||0);if(sec<=0)return '-';"
    "const h=Math.floor(sec/3600),m=Math.floor((sec%3600)/60),s=Math.floor(sec%60);"
    "if(h>0)return h+'h '+m+'m '+s+'s';if(m>0)return m+'m '+s+'s';return s+'s';}"
    "function draw(o){const t=Math.max(1,Number(o.total||1));const sent=Number(o.sent||0);"
    "const rate=Number(o.rate||0);"
    "const pct=(100*sent/t).toFixed(1);p.textContent=pct+'%';"
    "r.textContent=fmtRate(rate);"
    "const probeOk=Number(o.probe_ok||0)===1;"
    "const sdOk=Number(o.sd_ok||0)===1;"
    "s.textContent='state='+o.state+'\\nsent='+sent+'\\ntotal='+o.total+'\\nerr='+o.error+"
    "'\\nsd='+(sdOk?'ok':'missing')+"
    "'\\nprobe='+(probeOk?'ok':'fail')+' '+Number(o.probe_ms||0)+'ms age='+(o.probe_age_ms||0)+'ms ip='+(o.probe_ip||'?')+"
    "'\\nprobe_err='+(o.probe_err||'')+' cache_h='+(o.cache_hits||0)+' cache_m='+(o.cache_misses||0)+"
    "'\\nqueue done='+(o.uploaded_count||0)+' left='+(o.outstanding_count||0)+' ready='+(o.queue_ready||0)+' delayed='+(o.queue_delayed||0)+"
    "'\\nmb done='+fmtMB(o.done_bytes)+' left='+fmtMB(o.left_bytes)+' eta='+fmtEta(o.eta_s);}"
    "function poll(){fetch('/status').then(x=>x.json()).then(draw).catch(()=>{});}poll();setInterval(poll,250);</script></body></html>";


// Connect station Wi-Fi with a bounded wait and simple dot progress.
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

// Initialize SD interface; recovery attempts are only used during startup.
bool init_sd() {
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8)) {
    s_sd_available.store(true, std::memory_order_relaxed);
    return true;
  }
  const bool recovered = recover_sd("init_sd", kSdRecoverMaxAttempts);
  s_sd_available.store(recovered, std::memory_order_relaxed);
  return recovered;
}

// Clock out idle SPI bytes to help SD card leave a bad bus state.
void flush_sd_spi_clocks() {
  pinMode(SD_CS_PIN, OUTPUT);
  digitalWrite(SD_CS_PIN, HIGH);
  SPISettings settings(400000, MSBFIRST, SPI_MODE0);
  s_sd_spi.beginTransaction(settings);
  for (uint8_t i = 0; i < kSdClockFlushTransfers; ++i) {
    s_sd_spi.transfer(0xFF);
  }
  s_sd_spi.endTransaction();
}

// Blocking SD deinit/remount recovery routine with bounded retries.
bool recover_sd(const char* reason, uint8_t attempts) {
  if (attempts == 0) {
    attempts = 1;
  }
  if (s_sd_recovering.exchange(true, std::memory_order_acq_rel)) {
    return false;
  }
  bool recovered = false;
  const char* why = (reason != nullptr) ? reason : "unknown";
  Serial.printf("SD_RECOVER start reason=%s attempts=%u\n", why, static_cast<unsigned>(attempts));
  for (uint8_t attempt = 1; attempt <= attempts; ++attempt) {
    SD.end();
    delay(kSdRecoverSettleMs);
    s_sd_spi.end();
    delay(kSdRecoverSettleMs);
    s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
    flush_sd_spi_clocks();
    delay(kSdRecoverSettleMs);
    if (SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8)) {
      Serial.printf("SD_RECOVER success attempt=%u\n", static_cast<unsigned>(attempt));
      recovered = true;
      break;
    }
    Serial.printf("SD_RECOVER retry_failed attempt=%u\n", static_cast<unsigned>(attempt));
  }
  if (!recovered) {
    Serial.println("SD_RECOVER failed");
    Serial.println("SD_RECOVER action_required: power reset device and re-seat SD card.");
  }
  s_sd_available.store(recovered, std::memory_order_relaxed);
  s_sd_recovering.store(false, std::memory_order_release);
  return recovered;
}

// Runtime-safe SD recovery entrypoint (intentionally disabled for v5).
bool recover_sd_safe(const char* reason, uint8_t attempts) {
  (void)reason;
  (void)attempts;
  Serial.println("recover_sd: runtime recovery disabled (startup-only).");
  return false;
}

// Return whether storage is currently considered usable.
bool sd_available() {
  return s_sd_available.load(std::memory_order_relaxed);
}


// Reset ring-buffer indexes/flags without reallocating memory.
void ring_reset() {
  s_ring.head = 0;
  s_ring.tail = 0;
  s_ring.count = 0;
  for (size_t i = 0; i < RING_SLOTS; ++i) {
    s_ring.slots[i].len = 0;
    s_ring.slots[i].inUse = false;
  }
}

// Allocate ring-buffer backing blocks and clear slot metadata.
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

// ====================================================================================================
// Self Tests
// ====================================================================================================
