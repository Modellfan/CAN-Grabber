#ifdef SET_CONTENT_LENGTH_TEST
/*
 * Author: Avant Maker
 * Date: March 17, 2025
 * Version: 1.0
 * License: MIT
 * 
 * Description:
 * This example demonstrates how to use ESP32 WebServer Library's
 * setContentLength() method when serving binary data.
 *
 * Code Source:
 * This example code is sourced from the Comprehensive Guide
 * to the ESP32 Arduino Core Library, accessible on AvantMaker.com.
 * For additional code examples and in-depth documentation related to
 * the ESP32 Arduino Core Library, please visit:
 *
 * https://avantmaker.com/home/all-about-esp32-arduino-core-library/
 *
 * AvantMaker.com, your premier destination for all things
 * DIY, AI, IoT, Smart Home, and STEM projects. We are dedicated
 * to empowering makers, learners, and enthusiasts with
 * the resources they need to bring their innovative ideas to life.
 */
#include <WiFi.h>
#include <WebServer.h>

const char* ssid = "your-SSID";          // Replace with your Wi-Fi SSID
const char* password = "your-PASSWORD";  // Replace with your Wi-Fi password

WebServer server(80);

// Sample binary data (in a real scenario, this could be an image or file data)
const uint8_t sampleData[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, /* ... more bytes ... */};
const size_t dataSize = sizeof(sampleData);

void handleRoot();
void handleBinaryData();

void setup() {
  Serial.begin(115200);
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("Connected to WiFi");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Define server routes
  server.on("/binary", handleBinaryData);
  server.on("/", handleRoot);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}

void handleRoot() {
  server.send(200, "text/html", "<html><body><h1>AvantMaker.com ESP32 Web Server</h1>"
                               "<p>Click <a href='/binary'>here</a> to download binary data</p></body></html>");
}

void handleBinaryData() {
  // Set content type for binary data
  server.sendHeader("Content-Disposition", "attachment; filename=sample.bin");
  server.sendHeader("Content-Type", "application/octet-stream");
  
  // Set the content length to inform client about the total size
  server.setContentLength(dataSize);
  
  // Send response code and headers (but not content yet)
  server.send(200, "application/octet-stream", "");
  
  // Send binary data in chunks
  const size_t chunkSize = 256;
  size_t sentBytes = 0;
  
  while (sentBytes < dataSize) {
    size_t currentChunkSize = min(chunkSize, dataSize - sentBytes);
    server.client().write(&sampleData[sentBytes], currentChunkSize);
    sentBytes += currentChunkSize;
  }
}

#endif // SET_CONTENT_LENGTH_TEST
