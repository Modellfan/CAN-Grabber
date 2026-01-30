//230kb/s
#if defined(SD_HTTP_DOWNLOAD_TEST2)

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>
#include <WebServer.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "hardware/hardware_config.h"

#if __has_include("dev/sd_http_download_secrets.h")
#include "dev/sd_http_download_secrets.h"
#else
#define SD_HTTP_TEST_SSID ""
#define SD_HTTP_TEST_PASSWORD ""
#endif

namespace {

constexpr char kTestPath[] = "/sd_http_test.bin";
constexpr size_t kTestBytes = 10UL * 1024UL * 1024UL;
constexpr size_t kBlockSize = 32768;
constexpr size_t kBlocks = 2;
constexpr uint32_t kWifiRetryMs = 5000;

SPIClass s_sd_spi(HSPI);
bool s_sd_ready = false;

uint8_t s_buffers[kBlocks][kBlockSize];
size_t s_buf_len[kBlocks] = {0, 0};
bool s_buf_eof[kBlocks] = {false, false};

QueueHandle_t s_free_queue = nullptr;
QueueHandle_t s_ready_queue = nullptr;

volatile bool s_download_active = false;
File s_download_file;
volatile bool s_reader_eof = false;
size_t s_download_sent = 0;
size_t s_download_file_size = 0;
uint32_t s_download_start_ms = 0;
size_t s_reader_total = 0;
uint32_t s_reader_time_ms = 0;
uint32_t s_last_progress_ms = 0;
uint32_t s_last_wait_log_ms = 0;
uint32_t s_last_write_ms = 0;

WebServer s_server(80);

bool init_sd() {
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  return SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8);
}

bool ensure_test_file() {
  if (SD.exists(kTestPath)) {
    File file = SD.open(kTestPath, FILE_READ);
    if (file) {
      const size_t size = file.size();
      file.close();
      if (size == kTestBytes) {
        Serial.println("SD test file exists");
        return true;
      }
    }
    SD.remove(kTestPath);
  }

  File file = SD.open(kTestPath, FILE_WRITE);
  if (!file) {
    Serial.println("SD test file create failed");
    return false;
  }

  for (size_t i = 0; i < kBlockSize; ++i) {
    s_buffers[0][i] = static_cast<uint8_t>(i & 0xFF);
  }

  size_t written = 0;
  while (written < kTestBytes) {
    const size_t to_write =
        (kTestBytes - written) < kBlockSize ? (kTestBytes - written) : kBlockSize;
    const size_t out = file.write(s_buffers[0], to_write);
    if (out != to_write) {
      break;
    }
    written += out;
  }
  file.flush();
  file.close();
  Serial.print("SD test file created: ");
  Serial.print(written);
  Serial.println(" bytes");
  return written == kTestBytes;
}

void reset_buffer_queues() {
  xQueueReset(s_free_queue);
  xQueueReset(s_ready_queue);
  for (uint8_t i = 0; i < kBlocks; ++i) {
    const uint8_t idx = i;
    xQueueSend(s_free_queue, &idx, 0);
    s_buf_len[i] = 0;
    s_buf_eof[i] = false;
  }
  s_reader_eof = false;
  s_download_sent = 0;
  s_reader_total = 0;
  s_reader_time_ms = 0;
  s_download_start_ms = millis();
  s_last_progress_ms = millis();
  s_last_wait_log_ms = millis();
  s_last_write_ms = millis();
}

void sd_reader_task(void*) {
  for (;;) {
    if (!s_download_active || !s_download_file) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (s_reader_eof) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint8_t index = 0;
    if (xQueueReceive(s_free_queue, &index, pdMS_TO_TICKS(100)) != pdTRUE) {
      continue;
    }

    const uint32_t read_start = millis();
    const size_t bytes = s_download_file.read(s_buffers[index], kBlockSize);
    const uint32_t read_ms = millis() - read_start;
    s_reader_time_ms += read_ms;

    s_buf_len[index] = bytes;
    s_buf_eof[index] = (bytes == 0);
    if (bytes == 0) {
      s_reader_eof = true;
      Serial.println("SD reader EOF");
    } else {
      s_reader_total += bytes;
      Serial.print("SD reader chunk: ");
      Serial.println(bytes);
    }
    xQueueSend(s_ready_queue, &index, portMAX_DELAY);
  }
}

void log_download_stats(const char* prefix) {
  const uint32_t elapsed_ms = millis() - s_download_start_ms;
  const float mb = static_cast<float>(s_download_sent) / (1024.0f * 1024.0f);
  const float sec = static_cast<float>(elapsed_ms) / 1000.0f;
  const float mbps = (sec > 0.0f) ? (mb / sec) : 0.0f;
  const float read_sec = static_cast<float>(s_reader_time_ms) / 1000.0f;
  const float read_mb = static_cast<float>(s_reader_total) / (1024.0f * 1024.0f);
  const float read_mbps = (read_sec > 0.0f) ? (read_mb / read_sec) : 0.0f;
  Serial.print(prefix);
  Serial.print(mb, 2);
  Serial.print(" MB in ");
  Serial.print(sec, 2);
  Serial.print(" s (");
  Serial.print(mbps, 2);
  Serial.println(" MB/s)");
  Serial.print("SD read rate: ");
  Serial.print(read_mbps, 2);
  Serial.println(" MB/s");
}

void stop_download(const char* reason) {
  if (!s_download_active) {
    return;
  }
  s_download_active = false;
  if (s_download_file) {
    s_download_file.close();
  }
  Serial.print("HTTP download stopped (");
  Serial.print(reason ? reason : "unknown");
  Serial.println(")");
  log_download_stats("HTTP download: ");
}

void handle_root() {
  const char* body =
      "<!doctype html><html><head><title>SD Download Test</title></head>"
      "<body><h2>SD Download Test</h2>"
      "<p><a href=\"/download\">Download 10MB file</a></p>"
      "</body></html>";
  s_server.send(200, "text/html", body);
}

void handle_download() {
  Serial.println("HTTP download request");
  if (!s_sd_ready) {
    s_server.send(500, "text/plain", "SD not ready");
    return;
  }
  if (!SD.exists(kTestPath)) {
    s_server.send(404, "text/plain", "Test file missing");
    return;
  }
  if (s_download_active) {
    s_server.send(409, "text/plain", "Download in progress");
    return;
  }

  s_download_file = SD.open(kTestPath, FILE_READ);
  if (!s_download_file) {
    s_server.send(500, "text/plain", "Open failed");
    return;
  }

  s_download_file_size = s_download_file.size();
  Serial.print("SD file size: ");
  Serial.println(s_download_file_size);
  if (s_download_file_size == 0) {
    s_download_file.close();
    s_server.send(500, "text/plain", "Empty file");
    return;
  }

  reset_buffer_queues();
  s_download_active = true;
  Serial.print("WiFi RSSI: ");
  Serial.println(WiFi.RSSI());

  s_server.setContentLength(s_download_file_size);
  s_server.send(200, "application/octet-stream", "");
  WiFiClient client = s_server.client();
  client.setNoDelay(true);
  Serial.println("HTTP download streaming...");

  uint8_t current_buf = 0;
  size_t current_offset = 0;
  bool has_buf = false;

  while (client.connected() && s_download_active) {
    if (!has_buf) {
      if (xQueueReceive(s_ready_queue, &current_buf, pdMS_TO_TICKS(500)) != pdTRUE) {
        if (s_reader_eof) {
          Serial.println("HTTP stream: reader EOF reached");
          break;
        }
        const uint32_t now = millis();
        if (now - s_last_wait_log_ms >= 1000) {
          Serial.println("HTTP stream: waiting for buffer...");
          s_last_wait_log_ms = now;
        }
        continue;
      }
      current_offset = 0;
      has_buf = true;
      Serial.print("HTTP stream: got buffer ");
      Serial.println(current_buf);
    }

    const size_t len = s_buf_len[current_buf];
    if (len == 0) {
      xQueueSend(s_free_queue, &current_buf, portMAX_DELAY);
      has_buf = false;
      if (s_reader_eof) {
        break;
      }
      continue;
    }

    const size_t remaining = len - current_offset;
    if (remaining == 0) {
      xQueueSend(s_free_queue, &current_buf, portMAX_DELAY);
      has_buf = false;
      continue;
    }

    const size_t chunk = remaining > 1460 ? 1460 : remaining;
    const size_t wrote = client.write(s_buffers[current_buf] + current_offset, chunk);
    if (wrote == 0) {
      if (!client.connected()) {
        Serial.println("HTTP stream: client disconnected");
        break;
      }
      const uint32_t now = millis();
      if (now - s_last_wait_log_ms >= 1000) {
        Serial.print("HTTP stream: write stalled, avail=");
        Serial.println(client.availableForWrite());
        s_last_wait_log_ms = now;
      }
      if (now - s_last_write_ms >= 8000) {
        Serial.println("HTTP stream: write timeout");
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(1));
      continue;
    }

    current_offset += wrote;
    s_download_sent += wrote;
    s_last_write_ms = millis();

    const uint32_t now = millis();
    if (now - s_last_progress_ms >= 1000) {
      Serial.print("Progress sent/read: ");
      Serial.print(s_download_sent);
      Serial.print(" / ");
      Serial.print(s_reader_total);
      Serial.print(" / ");
      Serial.println(s_download_file_size);
      s_last_progress_ms = now;
    }

    if (current_offset >= len) {
      xQueueSend(s_free_queue, &current_buf, portMAX_DELAY);
      has_buf = false;
      Serial.println("HTTP stream: buffer sent");
    }
  }

  if (has_buf) {
    xQueueSend(s_free_queue, &current_buf, portMAX_DELAY);
  }

  if (s_download_sent >= s_download_file_size) {
    Serial.println("HTTP download finished");
    log_download_stats("HTTP download: ");
  } else {
    stop_download("client_disconnect");
  }

  s_download_active = false;
  if (s_download_file) {
    s_download_file.close();
  }
}

void server_task(void*) {
  s_server.on("/", HTTP_GET, handle_root);
  s_server.on("/download", HTTP_GET, handle_download);
  s_server.begin();
  Serial.println("WebServer started on port 80");

  for (;;) {
    s_server.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

void wifi_task(void*) {
  for (;;) {
    if (WiFi.status() == WL_CONNECTED) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }

    if (strlen(SD_HTTP_TEST_SSID) == 0) {
      Serial.println("WiFi SSID not configured");
      vTaskDelay(pdMS_TO_TICKS(2000));
      continue;
    }

    Serial.print("WiFi connect to ");
    Serial.println(SD_HTTP_TEST_SSID);
    WiFi.begin(SD_HTTP_TEST_SSID, SD_HTTP_TEST_PASSWORD);

    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000) {
      vTaskDelay(pdMS_TO_TICKS(250));
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("WiFi connected, IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("WiFi connect failed");
    }

    vTaskDelay(pdMS_TO_TICKS(kWifiRetryMs));
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);

  s_sd_ready = init_sd();
  if (!s_sd_ready) {
    Serial.println("SD init failed");
  } else {
    ensure_test_file();
  }

  s_free_queue = xQueueCreate(kBlocks, sizeof(uint8_t));
  s_ready_queue = xQueueCreate(kBlocks, sizeof(uint8_t));
  if (!s_free_queue || !s_ready_queue) {
    Serial.println("Queue init failed");
  }

  xTaskCreatePinnedToCore(wifi_task, "wifi_task", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(sd_reader_task, "sd_reader", 4096, nullptr, 3, nullptr, 1);
  xTaskCreatePinnedToCore(server_task, "http_server", 6144, nullptr, 2, nullptr, 1);
}

void loop() {
  delay(1000);
}

#endif // SD_HTTP_DOWNLOAD_TEST2
