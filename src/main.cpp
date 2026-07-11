#if !defined(SD_SPEED_TEST) && !defined(RX_LOAD_TEST) && !defined(SD_HTTP_DOWNLOAD_TEST)

#include <Arduino.h>
#include <esp_heap_caps.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "hardware/hardware_config.h"
#include "logging/log_writer.h"
#include "net/net_manager.h"
#include "rest/rest_api.h"
#include "rtc/rtc_clock.h"
#include "system/system_stats.h"
#include "storage/storage_manager.h"
#include "upload/upload_manager.h"
#include "web/web_server.h"

#ifndef APP_NAME
#define APP_NAME "CAN-Grabber"
#endif
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif
#ifndef PIO_ENV_NAME
#define PIO_ENV_NAME "unknown"
#endif
#ifndef ENABLE_COMPRESSOR
#define ENABLE_COMPRESSOR 0
#endif

#if ENABLE_COMPRESSOR
#include "compress/compressor.h"
#endif

namespace {

char s_serial_command[64] = {};
size_t s_serial_command_len = 0;
uint32_t s_serial_ignore_until_ms = 0;
uint32_t s_last_system_sample_ms = 0;
constexpr const char* kDebugWifiSsid = "\xC3\x9C" "berlingen@Wohnzimmer";
constexpr const char* kDebugWifiPassword = "Ueberlingen2019";

void printWifiStatus() {
  const config::Config& cfg = config::get();
  Serial.printf("[serial] WiFi STA: enabled=%s count=%u\n",
                cfg.global.wifi_sta_enabled ? "true" : "false",
                cfg.global.wifi_count);
  for (uint8_t i = 0; i < 3; ++i) {
    const char* ssid = cfg.global.wifi[i].ssid;
    if (ssid[0] == '\0') {
      Serial.printf("[serial] WiFi[%u]: <empty>\n", i);
      continue;
    }
    Serial.printf("[serial] WiFi[%u]: ssid=%s pass_len=%u\n",
                  i,
                  ssid,
                  static_cast<unsigned>(strlen(cfg.global.wifi[i].password)));
  }
}

void applyDebugWifiConfig() {
  config::set_wifi(0, kDebugWifiSsid, kDebugWifiPassword);
  config::set_wifi_count(1);
  config::Config& cfg = config::get_mutable();
  cfg.global.wifi_sta_enabled = true;
  config::save();

  Serial.printf("[serial] Debug WiFi stored in slot 0: %s\n", kDebugWifiSsid);
  Serial.println("[serial] STA enabled, restarting to apply network config");
  Serial.flush();
  delay(50);
  ESP.restart();
}

void executeSerialCommand(const char* command) {
  if (command == nullptr || command[0] == '\0') {
    return;
  }

  if (strcmp(command, "help") == 0) {
    Serial.println("[serial] Commands: help, reset, wifi-status, wifi-debug, sys-stats");
    return;
  }

  if (strcmp(command, "wifi-status") == 0) {
    printWifiStatus();
    return;
  }

  if (strcmp(command, "wifi-debug") == 0) {
    applyDebugWifiConfig();
    return;
  }

  if (strcmp(command, "sys-stats") == 0) {
    system_stats::print_summary("serial");
    return;
  }

  if (strcmp(command, "reset") == 0 || strcmp(command, "reboot") == 0) {
    Serial.println("[serial] Reset requested");
    Serial.flush();
    delay(50);
    ESP.restart();
    return;
  }

  Serial.printf("[serial] Unknown command: %s\n", command);
}

void handleSerialConsole() {
  if (millis() < s_serial_ignore_until_ms) {
    while (Serial.available() > 0) {
      (void)Serial.read();
    }
    s_serial_command_len = 0;
    return;
  }

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      s_serial_command[s_serial_command_len] = '\0';
      if (s_serial_command_len > 0) {
        executeSerialCommand(s_serial_command);
      }
      s_serial_command_len = 0;
      continue;
    }

    if (s_serial_command_len + 1 < sizeof(s_serial_command)) {
      s_serial_command[s_serial_command_len++] = c;
    } else {
      s_serial_command_len = 0;
      Serial.println("[serial] Command too long");
    }
  }
}

} // namespace

static void printBuildInfo() {
  Serial.println();
  Serial.print("App: ");
  Serial.println(APP_NAME);
  Serial.print("Version: ");
  Serial.println(APP_VERSION);
  Serial.print("Build: ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  Serial.print("PIO Env: ");
  Serial.println(PIO_ENV_NAME);
  Serial.print("Heap free/internal/largest: ");
  Serial.print(ESP.getFreeHeap());
  Serial.print(" / ");
  Serial.print(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.print(" / ");
  Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  Serial.print("PSRAM size/free: ");
  Serial.print(ESP.getPsramSize());
  Serial.print(" / ");
  Serial.println(ESP.getFreePsram());
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  printBuildInfo();
  Serial.println("[serial] Type 'reset' to reboot the device");
  Serial.println("[serial] Type 'wifi-debug' to store the default debug STA network");
  Serial.println("[serial] Type 'sys-stats' to print system diagnostics");
  s_serial_ignore_until_ms = millis() + 3000;

  system_stats::init();
  system_stats::sample(system_stats::Component::kBoot, "serial_ready");
  config::init();
  system_stats::sample(system_stats::Component::kBoot, "config");
  rtc_clock::init();
  system_stats::sample(system_stats::Component::kRtc, "init");
  storage::init();
  system_stats::sample(system_stats::Component::kStorage, "init");
  can::init();
  system_stats::sample(system_stats::Component::kCan, "init");
  logging::init();
  system_stats::sample(system_stats::Component::kLogging, "init");
  logging::start();
  system_stats::sample(system_stats::Component::kLogging, "start");
  upload::init();
  system_stats::sample(system_stats::Component::kUpload, "init");
  upload::queue_pending();
  system_stats::sample(system_stats::Component::kUpload, "queue");
#if ENABLE_COMPRESSOR
  compressor::init();
#endif
  net::init();
  system_stats::sample(system_stats::Component::kNet, "init");
  net::connect();
  system_stats::sample(system_stats::Component::kNet, "connect");
  rest::init();
  system_stats::sample(system_stats::Component::kRest, "init");
  rest::start();
  system_stats::sample(system_stats::Component::kRest, "start");
  web::init();
  system_stats::sample(system_stats::Component::kWeb, "init");
  web::start();
  system_stats::sample(system_stats::Component::kWeb, "start");
  system_stats::print_summary("setup_complete");
}

void loop() {
  handleSerialConsole();
  if ((millis() - s_last_system_sample_ms) >= 5000UL) {
    s_last_system_sample_ms = millis();
    system_stats::sample(system_stats::Component::kLoop, "tick");
  }
  net::loop();
  upload::loop();
  rest::loop();
  web::loop();
  delay(10);
}

#endif // !SD_SPEED_TEST && !RX_LOAD_TEST && !SD_HTTP_DOWNLOAD_TEST
