#if defined(SD_HTTP_DOWNLOAD_TEST)

// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright 2016-2026 Hristo Gochkov, Mathieu Carbou, Emil Muratov, Will Miles

//
// WebSocket example
//

#include <Arduino.h>
#include <AsyncTCP.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <SD.h>
#include <SPI.h>
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

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");
static const char *kTestPath = "/sd_http_test.bin";
static const size_t kTestBytes = 10UL * 1024UL * 1024UL;
static const size_t kBlockSize = 32768;
static const size_t kBlocks = 2;
static uint8_t s_buffers[kBlocks][kBlockSize];
static size_t s_buf_len[kBlocks] = {0, 0};
static bool s_buf_eof[kBlocks] = {false, false};

static SPIClass s_sd_spi(HSPI);
static bool s_sd_ready = false;
static QueueHandle_t s_free_queue = nullptr;
static QueueHandle_t s_ready_queue = nullptr;
static volatile bool s_download_active = false;
static File s_download_file;
static volatile bool s_reader_eof = false;
static int s_active_buf = -1;
static size_t s_active_len = 0;
static size_t s_active_offset = 0;
static uint32_t s_download_start_ms = 0;
static size_t s_download_sent = 0;
static size_t s_download_file_size = 0;
static size_t s_reader_total = 0;
static uint32_t s_last_progress_ms = 0;
static uint32_t s_reader_time_ms = 0;

namespace
{

  void wifi_task(void *)
  {
#if ASYNCWEBSERVER_WIFI_SUPPORTED
    const uint32_t retry_ms = 5000;
    for (;;)
    {
      if (WiFi.status() == WL_CONNECTED)
      {
        vTaskDelay(pdMS_TO_TICKS(1000));
        continue;
      }

      if (strlen(SD_HTTP_TEST_SSID) == 0)
      {
        Serial.println("WiFi SSID not configured");
        vTaskDelay(pdMS_TO_TICKS(2000));
        continue;
      }

      Serial.print("WiFi connect to ");
      Serial.println(SD_HTTP_TEST_SSID);

      WiFi.begin(SD_HTTP_TEST_SSID, SD_HTTP_TEST_PASSWORD);

      const uint32_t start = millis();
      while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000)
      {
        vTaskDelay(pdMS_TO_TICKS(250));
      }
      if (WiFi.status() == WL_CONNECTED)
      {
        Serial.print("WiFi connected, IP: ");
        Serial.println(WiFi.localIP());
      }
      else
      {
        Serial.println("WiFi connect failed");
      }

      vTaskDelay(pdMS_TO_TICKS(retry_ms));
    }
#else
    vTaskDelete(nullptr);
#endif
  }

  void web_task(void *)
  {
    bool server_started = false;
    uint32_t connected_since_ms = 0;
    for (;;)
    {
      const bool connected = WiFi.status() == WL_CONNECTED;
      if (connected)
      {
        if (connected_since_ms == 0)
        {
          connected_since_ms = millis();
        }
        const IPAddress ip = WiFi.localIP();
        const bool ip_valid = (ip[0] != 0);
        const bool stable = (millis() - connected_since_ms) >= 3000;
        if (ip_valid && stable && !server_started)
        {
          server.begin();
          server_started = true;
          Serial.println("Async web server started on port 80");
        }
      }
      else
      {
        connected_since_ms = 0;
        if (server_started)
        {
          server.end();
          server_started = false;
          Serial.println("Async web server stopped");
        }
      }
      vTaskDelay(pdMS_TO_TICKS(250));
    }
  }

} // namespace

static bool init_sd()
{
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8))
  {
    return false;
  }
  return true;
}

static bool ensure_test_file()
{
  if (SD.exists(kTestPath))
  {
    File file = SD.open(kTestPath, FILE_READ);
    if (file)
    {
      const size_t size = file.size();
      file.close();
      if (size == kTestBytes)
      {
        Serial.println("SD test file exists");
        return true;
      }
    }
    SD.remove(kTestPath);
  }

  File file = SD.open(kTestPath, FILE_WRITE);
  if (!file)
  {
    Serial.println("SD test file create failed");
    return false;
  }

  for (size_t i = 0; i < kBlockSize; ++i)
  {
    s_buffers[0][i] = static_cast<uint8_t>(i & 0xFF);
  }

  size_t written = 0;
  while (written < kTestBytes)
  {
    const size_t to_write =
        (kTestBytes - written) < kBlockSize ? (kTestBytes - written) : kBlockSize;
    const size_t out = file.write(s_buffers[0], to_write);
    if (out != to_write)
    {
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

static void reset_buffer_queues()
{
  xQueueReset(s_free_queue);
  xQueueReset(s_ready_queue);
  for (uint8_t i = 0; i < kBlocks; ++i)
  {
    const uint8_t idx = i;
    xQueueSend(s_free_queue, &idx, 0);
    s_buf_len[i] = 0;
    s_buf_eof[i] = false;
  }
  s_reader_eof = false;
  s_active_buf = -1;
  s_active_len = 0;
  s_active_offset = 0;
  s_reader_total = 0;
  s_last_progress_ms = millis();
  s_reader_time_ms = 0;
}

static void sd_reader_task(void *)
{
  for (;;)
  {
    if (!s_download_active || !s_download_file)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (s_reader_eof)
    {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint8_t index = 0;
    if (xQueueReceive(s_free_queue, &index, pdMS_TO_TICKS(100)) != pdTRUE)
    {
      continue;
    }

    const uint32_t read_start = millis();
    const size_t bytes = s_download_file.read(s_buffers[index], kBlockSize);
    const uint32_t read_ms = millis() - read_start;
    s_reader_time_ms += read_ms;
    s_buf_len[index] = bytes;
    s_buf_eof[index] = (bytes == 0);
    if (bytes == 0)
    {
      s_reader_eof = true;
    }
    else
    {
      s_reader_total += bytes;
    }
    xQueueSend(s_ready_queue, &index, portMAX_DELAY);
  }
}

static void stop_download(const char *reason, bool completed)
{
  if (!s_download_active)
  {
    return;
  }
  s_download_active = false;
  if (s_download_file)
  {
    s_download_file.close();
  }
  const uint32_t elapsed_ms = millis() - s_download_start_ms;
  const float mb = static_cast<float>(s_download_sent) / (1024.0f * 1024.0f);
  const float sec = static_cast<float>(elapsed_ms) / 1000.0f;
  const float mbps = (sec > 0.0f) ? (mb / sec) : 0.0f;
  const float read_sec = static_cast<float>(s_reader_time_ms) / 1000.0f;
  const float read_mb = static_cast<float>(s_reader_total) / (1024.0f * 1024.0f);
  const float read_mbps = (read_sec > 0.0f) ? (read_mb / read_sec) : 0.0f;
  if (completed)
  {
    Serial.print("HTTP download finished: ");
  }
  else
  {
    Serial.print("HTTP download aborted (");
    Serial.print(reason ? reason : "unknown");
    Serial.print("): ");
  }
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

static void finish_download()
{
  stop_download("complete", true);
}

static void abort_download(const char *reason)
{
  stop_download(reason, false);
}

static void on_client_disconnect(void *arg, AsyncClient *client)
{
  (void)arg;
  (void)client;
  abort_download("client_disconnect");
}

static size_t fill_chunk(uint8_t *buffer, size_t max_len, size_t index)
{
  (void)index;
  if (!s_download_active)
  {
    return 0;
  }

  if (s_download_sent >= s_download_file_size && s_download_file_size > 0)
  {
    finish_download();
    return 0;
  }

  if (s_active_buf < 0)
  {
    uint8_t buf_idx = 0;
    if (xQueueReceive(s_ready_queue, &buf_idx, 0) != pdTRUE)
    {
      if (s_reader_eof)
      {
        finish_download();
        return 0;
      }
      return RESPONSE_TRY_AGAIN;
    }
    s_active_buf = static_cast<int>(buf_idx);
    s_active_len = s_buf_len[buf_idx];
    s_active_offset = 0;
    if (s_active_len == 0)
    {
      xQueueSend(s_free_queue, &buf_idx, portMAX_DELAY);
      finish_download();
      return 0;
    }
  }

  size_t remaining = s_active_len - s_active_offset;
  if (remaining == 0)
  {
    const int released = s_active_buf;
    s_active_buf = -1;
    s_active_len = 0;
    s_active_offset = 0;
    xQueueSend(s_free_queue, &released, portMAX_DELAY);
    return RESPONSE_TRY_AGAIN;
  }

  const size_t to_copy = (remaining > max_len) ? max_len : remaining;
  memcpy(buffer, s_buffers[s_active_buf] + s_active_offset, to_copy);
  s_active_offset += to_copy;
  s_download_sent += to_copy;

  const uint32_t now = millis();
  if (now - s_last_progress_ms >= 1000)
  {
    Serial.print("Progress sent/read: ");
    Serial.print(s_download_sent);
    Serial.print(" / ");
    Serial.print(s_reader_total);
    Serial.print(" / ");
    Serial.println(s_download_file_size);
    s_last_progress_ms = now;
  }

  if (s_active_offset >= s_active_len)
  {
    const int released = s_active_buf;
    s_active_buf = -1;
    s_active_len = 0;
    s_active_offset = 0;
    xQueueSend(s_free_queue, &released, portMAX_DELAY);
  }

  return to_copy;
}

void setup()
{
  Serial.begin(115200);

#if ASYNCWEBSERVER_WIFI_SUPPORTED
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
#endif

  s_sd_ready = init_sd();
  if (!s_sd_ready)
  {
    Serial.println("SD init failed");
  }
  else
  {
    ensure_test_file();
  }

  s_free_queue = xQueueCreate(kBlocks, sizeof(uint8_t));
  s_ready_queue = xQueueCreate(kBlocks, sizeof(uint8_t));
  if (!s_free_queue || !s_ready_queue)
  {
    Serial.println("Queue init failed");
  }

  server.on("/download", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    if (!s_sd_ready) {
      request->send(500, "text/plain", "SD not ready");
      return;
    }
    if (!SD.exists(kTestPath)) {
      request->send(404, "text/plain", "Test file missing");
      return;
    }
    if (s_download_active) {
      request->send(409, "text/plain", "Download in progress");
      return;
    }

    s_download_file = SD.open(kTestPath, FILE_READ);
    if (!s_download_file) {
      request->send(500, "text/plain", "Open failed");
      return;
    }

    s_download_active = true;
    reset_buffer_queues();
    s_reader_eof = false;
    s_download_start_ms = millis();
    s_download_sent = 0;
    s_download_file_size = s_download_file.size();
    Serial.print("SD file size: ");
    Serial.println(s_download_file_size);

    AsyncClient* client = request->client();
    if (client) {
      client->onDisconnect(on_client_disconnect, nullptr);
    }

    AsyncWebServerResponse* response = request->beginResponse(
        "application/octet-stream",
        s_download_file_size,
        [](uint8_t* buffer, size_t max_len, size_t index) -> size_t {
          return fill_chunk(buffer, max_len, index);
        });
    response->addHeader("Content-Disposition",
                        "attachment; filename=\"sd_http_test.bin\"");
    response->addHeader("X-File-Size", String(s_download_file_size));
    request->send(response);
    Serial.println("HTTP download started"); });

  xTaskCreatePinnedToCore(wifi_task, "wifi_task", 4096, nullptr, 2, nullptr, 0);
  xTaskCreatePinnedToCore(web_task, "web_task", 4096, nullptr, 2, nullptr, 1);
  xTaskCreatePinnedToCore(sd_reader_task, "sd_reader", 4096, nullptr, 3, nullptr, 1);
}

void loop()
{
  delay(1000);
}

#endif // SD_HTTP_DOWNLOAD_TEST
