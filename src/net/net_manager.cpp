#include "net/net_manager.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "config/app_config.h"

namespace net {

namespace {

constexpr const char* kHostname = "canlogger";
constexpr const char* kApSsid = "canlogger-setup";
constexpr const char* kApPassword = "";
constexpr uint32_t kConnectTimeoutMs = 12000;
constexpr uint32_t kRetryIntervalMs = 5000;

bool s_initialized = false;
bool s_connecting = false;
bool s_mdns_started = false;
bool s_event_registered = false;
bool s_ap_active = false;
uint8_t s_ssid_index = 0;
uint32_t s_attempt_start_ms = 0;
uint32_t s_next_retry_ms = 0;

uint8_t configured_wifi_count() {
  const config::Config& cfg = config::get();
  const uint8_t count = cfg.global.wifi_count;
  return (count > 3) ? 3 : count;
}

bool is_valid_ssid(uint8_t index) {
  const config::Config& cfg = config::get();
  if (index >= 3) {
    return false;
  }
  return cfg.global.wifi[index].ssid[0] != '\0';
}

void start_mdns() {
  if (s_mdns_started) {
    return;
  }
  if (MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    s_mdns_started = true;
  }
}

void stop_mdns() {
  if (!s_mdns_started) {
    return;
  }
  MDNS.end();
  s_mdns_started = false;
}

void start_ap() {
  if (s_ap_active) {
    return;
  }
  Serial.println("[net] AP start: canlogger-setup");
  WiFi.softAP(kApSsid, kApPassword);
  s_ap_active = true;
}

void stop_ap() {
  if (!s_ap_active) {
    return;
  }
  Serial.println("[net] AP stop");
  WiFi.softAPdisconnect(true);
  s_ap_active = false;
}

bool begin_next_network() {
  const uint8_t count = configured_wifi_count();
  const config::Config& cfg = config::get();

  while (s_ssid_index < count) {
    if (is_valid_ssid(s_ssid_index)) {
      const config::WifiConfig& wifi = cfg.global.wifi[s_ssid_index];
      Serial.print("[net] STA connect: ");
      Serial.println(wifi.ssid);
      WiFi.begin(wifi.ssid, wifi.password);
      s_connecting = true;
      s_attempt_start_ms = millis();
      return true;
    }
    ++s_ssid_index;
  }
  return false;
}

} // namespace

void init() {
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.setHostname(kHostname);

  if (!s_event_registered) {
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      (void)info;
      if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.println("[net] STA connected");
        start_mdns();
      } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        Serial.println("[net] STA disconnected");
        stop_mdns();
      }
    });
    s_event_registered = true;
  }

  s_initialized = true;
}

void connect() {
  if (!s_initialized) {
    return;
  }

  const uint8_t count = configured_wifi_count();
  if (count == 0) {
    WiFi.mode(WIFI_AP);
    start_ap();
    return;
  }

  s_ssid_index = 0;
  s_connecting = false;
  s_next_retry_ms = 0;
  WiFi.mode(WIFI_AP_STA);
  start_ap();
  WiFi.disconnect(true, true);
  begin_next_network();
}

void disconnect() {
  s_connecting = false;
  s_next_retry_ms = 0;
  stop_mdns();
  stop_ap();
  WiFi.disconnect(true, true);
}

void loop() {
  if (!s_initialized) {
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!s_mdns_started) {
      start_mdns();
    }
    if (s_ap_active) {
      stop_ap();
    }
    return;
  }

  const uint32_t now = millis();
  if (s_connecting) {
    if (now - s_attempt_start_ms >= kConnectTimeoutMs) {
      s_connecting = false;
      ++s_ssid_index;
      if (!begin_next_network()) {
        s_next_retry_ms = now + kRetryIntervalMs;
        start_ap();
      }
    }
    return;
  }

  if (s_next_retry_ms != 0 && now < s_next_retry_ms) {
    return;
  }

  s_ssid_index = 0;
  s_next_retry_ms = 0;
  begin_next_network();
}

bool is_connected() {
  return WiFi.status() == WL_CONNECTED;
}

int8_t rssi_dbm() {
  if (!is_connected()) {
    return -127;
  }
  return static_cast<int8_t>(WiFi.RSSI());
}

uint8_t rssi_percent() {
  const int rssi = rssi_dbm();
  if (rssi <= -100) {
    return 0;
  }
  if (rssi >= -50) {
    return 100;
  }
  const int percent = (rssi + 100) * 2;
  return static_cast<uint8_t>(percent);
}

} // namespace net
