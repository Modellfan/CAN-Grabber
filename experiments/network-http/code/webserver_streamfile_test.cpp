#ifdef WEBSERVER_STREAMFILE_TEST
/*
 * Author: Avant Maker
 * Date: March 17, 2025
 * Version: 1.0
 * License: MIT 
 * 
 * Description: 
 * This example demonstrates how to use ESP32 WebServer Library's 
 * streamFile() to serve an HTML file stored in SPIFFS to a client
 * when they access the root URL ("/"). It allows the ESP32 to
 * efficiently send large files without loading the entire file
 * into memory.
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
#include <FS.h>
#include <SD.h>
#include <SPI.h>

// Project hardware config
#include "hardware/hardware_config.h"

// Network credentials
const char* ssid = "Blacknet@Ueberlingen";          // Replace with your Wi-Fi SSID
const char* password = "Ueberlingen2019";  // Replace with your Wi-Fi password

// Web server port number
WebServer server(80);

// File paths
const char* indexPath = "/index.html";
const char* testBinPath = "/test_500kb.bin";
const size_t testBinSize = 500 * 1024;
const size_t testBinChunk = 1024;

SPIClass s_sd_spi(HSPI);
bool s_sd_ready = false;

// Forward declarations
void handleRoot();
void handleNotFound();
void serveFile(const char* path);
void listFiles();

// MIME types for common file extensions
const char* getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  else if (filename.endsWith(".css")) return "text/css";
  else if (filename.endsWith(".js")) return "application/javascript";
  else if (filename.endsWith(".ico")) return "image/x-icon";
  else if (filename.endsWith(".json")) return "application/json";
  else if (filename.endsWith(".png")) return "image/png";
  else if (filename.endsWith(".jpg")) return "image/jpeg";
  else if (filename.endsWith(".gif")) return "image/gif";
  else if (filename.endsWith(".svg")) return "image/svg+xml";
  return "text/plain";
}

void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  
  // Initialize SD card
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  s_sd_ready = SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", 8);
  if (!s_sd_ready) {
    Serial.println("SD mount failed");
    return;
  }
  Serial.println("SD mounted successfully");
  
  // Create test binary if missing
  if (!SD.exists(testBinPath)) {
    File file = SD.open(testBinPath, "w");
    if (file) {
      uint8_t buffer[testBinChunk];
      for (size_t i = 0; i < testBinChunk; ++i) {
        buffer[i] = static_cast<uint8_t>(i & 0xFF);
      }
      size_t remaining = testBinSize;
      while (remaining > 0) {
        const size_t to_write = remaining > testBinChunk ? testBinChunk : remaining;
        const size_t written = file.write(buffer, to_write);
        if (written != to_write) {
          Serial.println("Failed to write test bin");
          break;
        }
        remaining -= written;
      }
      file.close();
      Serial.println("Test bin created");
    } else {
      Serial.println("Failed to create test bin");
    }
  }

  // List files in SD for debugging
  listFiles();
  
  // Connect to Wi-Fi
  WiFi.begin(ssid, password);
  Serial.println("Connecting to WiFi");
  
  // Wait for connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  
  // Define server routes
  server.on("/", HTTP_GET, handleRoot);
  
  // Define handler for any file request
  server.onNotFound(handleNotFound);
  
  // Start server
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  // Handle client requests
  server.handleClient();
  delay(10);
}

// Handler for root page
void handleRoot() {
  serveFile(indexPath);
}

// Handle file requests using streamFile method
void serveFile(const char* path) {
  Serial.print("Requested file: ");
  Serial.println(path);
  
  if (!s_sd_ready) {
    Serial.println("SD not ready");
    server.send(500, "text/plain", "SD not ready");
    return;
  }

  if (SD.exists(path)) {
    File file = SD.open(path, "r");
    if (file) {
      const uint32_t start_ms = millis();
      const size_t file_size = file.size();
      // Get MIME type based on file extension
      String contentType = getContentType(String(path));
      
      // Stream the file to the client
      const size_t sent = server.streamFile(file, contentType);
      const uint32_t elapsed_ms = millis() - start_ms;
      const float elapsed_s = static_cast<float>(elapsed_ms) / 1000.0f;
      const float kb_sent = static_cast<float>(sent) / 1024.0f;
      const float kb_per_s = elapsed_s > 0.0f ? (kb_sent / elapsed_s) : 0.0f;
      
      // Close the file when done
      file.close();
      Serial.println("File served successfully");
      Serial.print("Serve time (ms): ");
      Serial.println(elapsed_ms);
      Serial.print("Serve rate (KB/s): ");
      Serial.println(kb_per_s, 2);
      Serial.print("Bytes sent / size: ");
      Serial.print(sent);
      Serial.print(" / ");
      Serial.println(file_size);
    } else {
      Serial.println("Failed to open file for reading");
      server.send(500, "text/plain", "Internal Server Error");
    }
  } else {
    Serial.println("File not found");
    server.send(404, "text/plain", "File Not Found");
  }
}

// Handle requests for files not found
void handleNotFound() {
  // Check if the requested URI is a file
  String path = server.uri();
  
  // If the path doesn't start with a slash, add one
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  
  // Try to serve the file
  if (SD.exists(path)) {
    serveFile(path.c_str());
  } else {
    // If file doesn't exist, return 404
    String message = "File Not Found\n\n";
    message += "URI: ";
    message += server.uri();
    message += "\nMethod: ";
    message += (server.method() == HTTP_GET) ? "GET" : "POST";
    message += "\nArguments: ";
    message += server.args();
    message += "\n";
    
    for (uint8_t i = 0; i < server.args(); i++) {
      message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
    }
    
    server.send(404, "text/plain", message);
  }
}

// List all files in SD (for debugging)
void listFiles() {
  Serial.println("Files on SD:");
  
  File root = SD.open("/");
  if (!root) {
    Serial.println("Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("Not a directory");
    return;
  }
  
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.print("DIR: ");
    } else {
      Serial.print("FILE: ");
      Serial.print(file.name());
      Serial.print(" (");
      Serial.print(file.size());
      Serial.println(" bytes)");
    }
    file = root.openNextFile();
  }
  Serial.println("---------------------");
}

#endif // WEBSERVER_STREAMFILE_TEST
