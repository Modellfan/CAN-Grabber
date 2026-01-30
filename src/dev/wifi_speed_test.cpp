// #if defined(WIFI_SPEED_TEST)

// // // Download is up to 10.000kb/s
// //Upload only 100kb/s for now
// //with external atenna 400bk/s

// #include <Arduino.h>
// #include <HTTPClient.h>
// #include <WiFi.h>
// #include <FS.h>
// #include <SD.h>
// #include <SPI.h>
// #include <WebServer.h>
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #include <string.h>

// #include "hardware/hardware_config.h"

// #define WIFI_SPEED_TEST_SSID "Blacknet@Ueberlingen"
// #define WIFI_SPEED_TEST_PASSWORD "Ueberlingen2019"

// SPIClass s_sd_spi(HSPI);
// bool s_sd_ready = false;
// WebServer s_server(80);
// TaskHandle_t s_server_task = nullptr;

// const char *get_content_type(const String &filename)
// {
//   if (filename.endsWith(".html"))
//     return "text/html";
//   if (filename.endsWith(".css"))
//     return "text/css";
//   if (filename.endsWith(".js"))
//     return "application/javascript";
//   if (filename.endsWith(".json"))
//     return "application/json";
//   if (filename.endsWith(".png"))
//     return "image/png";
//   if (filename.endsWith(".jpg"))
//     return "image/jpeg";
//   if (filename.endsWith(".bin"))
//     return "application/octet-stream";
//   return "text/plain";
// }

// bool init_sd()
// {
//   s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
//   return SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8);
// }

// void list_files()
// {
//   Serial.println("Files on SD:");
//   File root = SD.open("/");
//   if (!root || !root.isDirectory())
//   {
//     Serial.println("Failed to open SD root");
//     return;
//   }

//   File file = root.openNextFile();
//   while (file)
//   {
//     if (file.isDirectory())
//     {
//       Serial.print("DIR: ");
//     }
//     else
//     {
//       Serial.print("FILE: ");
//       Serial.print(file.name());
//       Serial.print(" (");
//       Serial.print(file.size());
//       Serial.println(" bytes)");
//     }
//     file = root.openNextFile();
//   }
//   Serial.println("---------------------");
// }

// void serve_file(const char *path)
// {
//   if (!s_sd_ready)
//   {
//     s_server.send(500, "text/plain", "SD not ready");
//     return;
//   }
//   if (!SD.exists(path))
//   {
//     s_server.send(404, "text/plain", "File not found");
//     return;
//   }

//   File file = SD.open(path, FILE_READ);
//   if (!file)
//   {
//     s_server.send(500, "text/plain", "Open failed");
//     return;
//   }

//   const uint32_t start_ms = millis();
//   const size_t file_size = file.size();
//   const String content_type = get_content_type(String(path));
//   const size_t sent = s_server.streamFile(file, content_type);
//   const uint32_t elapsed_ms = millis() - start_ms;
//   const float elapsed_s = static_cast<float>(elapsed_ms) / 1000.0f;
//   const float kb_sent = static_cast<float>(sent) / 1024.0f;
//   const float kb_per_s = elapsed_s > 0.0f ? (kb_sent / elapsed_s) : 0.0f;
//   file.close();

//   Serial.print("Served file ");
//   Serial.print(path);
//   Serial.print(" bytes sent/size ");
//   Serial.print(sent);
//   Serial.print(" / ");
//   Serial.print(file_size);
//   Serial.print(" in ");
//   Serial.print(elapsed_ms);
//   Serial.print(" ms (");
//   Serial.print(kb_per_s, 2);
//   Serial.println(" KB/s)");
// }

// // Fast, low-allocation file sender (bigger writes than streamFile())
// void serve_file_fast(const char* path) {
//   if (!s_sd_ready) {
//     s_server.send(500, "text/plain", "SD not ready");
//     return;
//   }
//   if (!SD.exists(path)) {
//     s_server.send(404, "text/plain", "File not found");
//     return;
//   }

//   File file = SD.open(path, FILE_READ);
//   if (!file) {
//     s_server.send(500, "text/plain", "Open failed");
//     return;
//   }

//   const uint32_t start_ms = millis();
//   const size_t file_size = file.size();

//   // Avoid building a String for MIME type if you can.
//   // If your get_content_type requires String, consider replacing it with a const char* version.
//   const char* content_type = "application/octet-stream";
//   {
//     // Minimal extension check without heap allocation
//     const char* ext = strrchr(path, '.');
//     if (ext) {
//       if (!strcmp(ext, ".html")) content_type = "text/html";
//       else if (!strcmp(ext, ".css")) content_type = "text/css";
//       else if (!strcmp(ext, ".js")) content_type = "application/javascript";
//       else if (!strcmp(ext, ".ico")) content_type = "image/x-icon";
//       else if (!strcmp(ext, ".json")) content_type = "application/json";
//       else if (!strcmp(ext, ".png")) content_type = "image/png";
//       else if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) content_type = "image/jpeg";
//       else if (!strcmp(ext, ".gif")) content_type = "image/gif";
//       else if (!strcmp(ext, ".svg")) content_type = "image/svg+xml";
//       else content_type = "application/octet-stream";
//     }
//   }

//   // Send headers only; we'll write the body manually in big chunks.
//   s_server.setContentLength(file_size);
//   s_server.send(200, content_type, "");

//   WiFiClient client = s_server.client();

//   // Bigger buffer usually = better throughput (try 4096/8192/16384).
//   // 8192 is a good starting point for ESP32 RAM.
//   static uint8_t buf[16384];

//   size_t sent = 0;
//   while (file.available()) {
//     const size_t to_read = file.read(buf, sizeof(buf));
//     if (to_read == 0) break;

//     // Ensure full write (client.write may write partial)
//     size_t off = 0;
//     while (off < to_read) {
//       const size_t w = client.write(buf + off, to_read - off);
//       if (w == 0) { file.close(); return; } // client stalled/disconnected
//       off += w;
//       sent += w;
//       delay(0); // yield to WiFi/LWIP
//     }
//   }

//   file.close();

//   const uint32_t elapsed_ms = millis() - start_ms;
//   const float elapsed_s = (float)elapsed_ms / 1000.0f;
//   const float kb_per_s = (elapsed_s > 0.0f) ? ((float)sent / 1024.0f) / elapsed_s : 0.0f;

//   Serial.print("Served file ");
//   Serial.print(path);
//   Serial.print(" bytes sent/size ");
//   Serial.print(sent);
//   Serial.print(" / ");
//   Serial.print(file_size);
//   Serial.print(" in ");
//   Serial.print(elapsed_ms);
//   Serial.print(" ms (");
//   Serial.print(kb_per_s, 2);
//   Serial.println(" KB/s)");
// }

// void handle_root()
// {
//   const char* body =
//       "<!doctype html><html><head><title>WiFi Speed Test</title></head>"
//       "<body><h2>WiFi Speed Test</h2>"
//       "<p><a href=\"/download\">Download test file</a></p>"
//       "</body></html>";
//   s_server.send(200, "text/html", body);
// }

// void handle_download()
// {
//   if (!s_sd_ready)
//   {
//     s_server.send(500, "text/plain", "SD not ready");
//     return;
//   }

//   File root = SD.open("/");
//   if (!root || !root.isDirectory())
//   {
//     s_server.send(500, "text/plain", "Failed to open SD root");
//     return;
//   }

//   String body = "<!doctype html><html><head><title>SD Downloads</title></head><body>";
//   body += "<h2>SD Downloads</h2><ul>";

//   File file = root.openNextFile();
//   while (file)
//   {
//     if (!file.isDirectory())
//     {
//       const char *name = file.name();
//       body += "<li><a href=\"";
//       body += name;
//       body += "\">";
//       body += name;
//       body += "</a> (";
//       body += file.size();
//       body += " bytes)</li>";
//     }
//     file = root.openNextFile();
//   }

//   body += "</ul></body></html>";
//   s_server.send(200, "text/html", body);
// }


// void handle_not_found()
// {
//   String path = s_server.uri();
//   if (!path.startsWith("/"))
//   {
//     path = "/" + path;
//   }

//   if (SD.exists(path))
//   {
//     serve_file_fast(path.c_str());
//     return;
//   }

//   String message = "File Not Found\n\n";
//   message += "URI: ";
//   message += s_server.uri();
//   message += "\nMethod: ";
//   message += (s_server.method() == HTTP_GET) ? "GET" : "POST";
//   message += "\nArguments: ";
//   message += s_server.args();
//   message += "\n";
//   for (uint8_t i = 0; i < s_server.args(); i++)
//   {
//     message += " " + s_server.argName(i) + ": " + s_server.arg(i) + "\n";
//   }
//   s_server.send(404, "text/plain", message);
// }

// void server_task(void *)
// {
//   s_server.on("/", HTTP_GET, handle_root);
//   s_server.on("/download", HTTP_GET, handle_download);
//   s_server.onNotFound(handle_not_found);
//   s_server.begin();
//   Serial.println("WiFi speed test server started on port 80");

//   for (;;)
//   {
//     s_server.handleClient();
//     vTaskDelay(0);
//   }
// }

// void connect_wifi()
// {
//   Serial.println("+++ Connecting to WiFi");
//   WiFi.mode(WIFI_STA);
//   WiFi.begin(WIFI_SPEED_TEST_SSID, WIFI_SPEED_TEST_PASSWORD);
//   WiFi.setSleep(false);

//   while (WiFi.status() != WL_CONNECTED)
//   {
//     delay(500);
//     Serial.print('.');
//   }
//   Serial.println();
//   Serial.println("+++ WiFi Connected");
//   Serial.print("... IP Address: ");
//   Serial.println(WiFi.localIP());
// }

// void setup()
// {
//   Serial.begin(115200);
//   Serial.println("+++ Serial Initialized");
//   connect_wifi();
//   s_sd_ready = init_sd();
//   if (!s_sd_ready)
//   {
//     Serial.println("SD mount failed");
//   }

//   xTaskCreatePinnedToCore(server_task, "wifi_speed_http", 16384, nullptr, 3, &s_server_task, 1);
// }

// void loop()
// {
//   delay(1000);
// }



// #endif // WIFI_SPEED_TEST

//Same speed of 300-400kb/s as above

#if defined(WIFI_SPEED_TEST)

// Minimal high-speed HTTP server built on raw TCP (WiFiServer).
// Endpoint:  http://<esp_ip>:8080/speed?mb=10
// Sends MB of generated bytes from RAM as fast as possible.

#include <Arduino.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define WIFI_SPEED_TEST_SSID     "Blacknet@Ueberlingen"
#define WIFI_SPEED_TEST_PASSWORD "Ueberlingen2019"

namespace {

WiFiServer g_srv(80);
TaskHandle_t g_task = nullptr;

void connect_wifi() {
  Serial.println("+++ Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                 // important for throughput
  WiFi.useStaticBuffers(true);
  WiFi.begin(WIFI_SPEED_TEST_SSID, WIFI_SPEED_TEST_PASSWORD);

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.println();
  Serial.println("+++ WiFi Connected");
  Serial.print("... IP Address: ");
  Serial.println(WiFi.localIP());
}

static bool read_request_line(WiFiClient& c, String& out_line, uint32_t timeout_ms = 2000) {
  out_line = "";
  uint32_t start = millis();
  while (millis() - start < timeout_ms) {
    while (c.available()) {
      char ch = (char)c.read();
      if (ch == '\n') return true;
      if (ch != '\r') out_line += ch;
      if (out_line.length() > 256) return true; // safety cap
    }
    delay(0);
  }
  return false;
}

static void send_http_speed(WiFiClient& c, size_t bytes_to_send) {
  // Static payload buffer (no heap churn)
  static uint8_t buf[32768];
  static bool inited = false;
  if (!inited) {
    for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = (uint8_t)i;
    inited = true;
  }

  // HTTP headers
  c.print("HTTP/1.1 200 OK\r\n");
  c.print("Content-Type: application/octet-stream\r\n");
  c.print("Cache-Control: no-store\r\n");
  c.print("Connection: close\r\n");
  c.print("Content-Length: ");
  c.print((uint32_t)bytes_to_send);
  c.print("\r\n\r\n");

  // Body: write until done
  size_t sent = 0;
  while (sent < bytes_to_send && c.connected()) {
    size_t chunk = bytes_to_send - sent;
    if (chunk > sizeof(buf)) chunk = sizeof(buf);

    size_t off = 0;
    while (off < chunk && c.connected()) {
      size_t w = c.write(buf + off, chunk - off);
      if (w == 0) { delay(0); continue; } // backpressure; yield
      off += w;
    }
    sent += chunk;
    delay(0); // yield to WiFi/LWIP
  }
}

static void tcp_http_speed_task(void*) {
  g_srv.begin();
  Serial.println("+++ TCP HTTP speed server started");
  Serial.println("... URL: http://<esp_ip>:8080/speed?mb=10");

  for (;;) {
    WiFiClient c = g_srv.available();
    if (!c) { vTaskDelay(1); continue; }

    c.setNoDelay(true);

    // Read first line: "GET /speed?mb=10 HTTP/1.1"
    String line;
    if (!read_request_line(c, line)) { c.stop(); continue; }

    // Drain the rest of headers quickly (don’t waste time parsing)
    uint32_t t0 = millis();
    while (c.connected() && millis() - t0 < 20) {
      while (c.available()) c.read();
      delay(0);
    }

    // Minimal routing
    if (line.startsWith("GET ")) {
      int sp2 = line.indexOf(' ', 4);
      String target = (sp2 > 0) ? line.substring(4, sp2) : "/";

      if (target.startsWith("/speed")) {
        // parse ?mb=
        size_t mb = 10;
        int q = target.indexOf("?mb=");
        if (q >= 0) {
          mb = (size_t)target.substring(q + 4).toInt();
          if (mb < 1) mb = 1;
          if (mb > 200) mb = 200; // safety cap
        }
        const size_t bytes_to_send = mb * 1024UL * 1024UL;
        send_http_speed(c, bytes_to_send);
      } else {
        c.print("HTTP/1.1 404 Not Found\r\nConnection: close\r\n\r\n");
      }
    } else {
      c.print("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
    }

    c.stop();
    vTaskDelay(1);
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("+++ Serial Initialized");

  connect_wifi();

  // Run server on core 1, leave WiFi/LWIP happier on core 0
  xTaskCreatePinnedToCore(
    tcp_http_speed_task,
    "tcp_http_speed",
    32000,        // stack
    nullptr,
    3,           // priority
    &g_task,
    1            // core
  );
}

void loop() {
  // nothing; server runs in its own task
  delay(1000);
}

#endif // WIFI_SPEED_TEST
