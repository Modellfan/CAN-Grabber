#include "net/net_manager.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include <freertos/FreeRTOS.h>

#include "config/app_config.h"

namespace net {

namespace {

constexpr const char* kHostname = "canlogger";
constexpr const char* kApSsid = "canlogger-setup";
constexpr const char* kApPassword = "";
constexpr uint32_t kConnectTimeoutMs = 12000;
constexpr uint32_t kRetryIntervalMs = 5000;
constexpr uint32_t kScanIntervalMs = 30000;
constexpr uint32_t kScanCooldownMs = 10000;
constexpr uint8_t kMaxScanResults = 12;

#if defined(STA_AP_TEST)
#define NET_TEST_LOG(msg) Serial.println(msg)
#define NET_TEST_LOGF(...) Serial.printf(__VA_ARGS__)
#else
#define NET_TEST_LOG(msg) \
  do {                    \
  } while (0)
#define NET_TEST_LOGF(...) \
  do {                     \
  } while (0)
#endif

bool s_initialized = false;
bool s_connecting = false;
bool s_mdns_started = false;
bool s_event_registered = false;
bool s_ap_active = false;
uint8_t s_ssid_index = 0;
uint32_t s_attempt_start_ms = 0;
uint32_t s_next_retry_ms = 0;
uint32_t s_last_scan_ms = 0;
bool s_scan_running = false;
portMUX_TYPE s_scan_mutex = portMUX_INITIALIZER_UNLOCKED;
WifiScanEntry s_scan_results[kMaxScanResults];
size_t s_scan_count = 0;
bool s_sta_mode_cached = false;
uint8_t s_sta_failures[3] = {0, 0, 0};
uint32_t s_last_ap_client_ms = 0;

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

uint8_t rssi_to_percent(int rssi_dbm) {
  if (rssi_dbm <= -100) {
    return 0;
  }
  if (rssi_dbm >= -50) {
    return 100;
  }
  const int percent = (rssi_dbm + 100) * 2;
  return static_cast<uint8_t>(percent);
}

void store_scan_results(int count) {
  portENTER_CRITICAL(&s_scan_mutex);
  s_scan_count = 0;
  for (int i = 0; i < count && s_scan_count < kMaxScanResults; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) {
      continue;
    }
    WifiScanEntry& entry = s_scan_results[s_scan_count++];
    strncpy(entry.ssid, ssid.c_str(), sizeof(entry.ssid));
    entry.ssid[sizeof(entry.ssid) - 1] = '\0';
    entry.rssi_dbm = static_cast<int8_t>(WiFi.RSSI(i));
    entry.rssi_percent = rssi_to_percent(entry.rssi_dbm);
    entry.channel = static_cast<uint8_t>(WiFi.channel(i));
    entry.secure = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
  }
  portEXIT_CRITICAL(&s_scan_mutex);
}

bool station_mode_enabled() {
  return config::get().global.wifi_sta_enabled;
}

uint8_t ap_client_count() {
  return WiFi.softAPgetStationNum();
}

void reset_sta_attempts() {
  s_connecting = false;
  s_next_retry_ms = 0;
  s_ssid_index = 0;
  s_attempt_start_ms = 0;
  for (uint8_t i = 0; i < 3; ++i) {
    s_sta_failures[i] = 0;
  }
}

bool all_sta_exhausted(uint8_t count) {
  if (count == 0) {
    return true;
  }
  for (uint8_t i = 0; i < count; ++i) {
    if (is_valid_ssid(i) && s_sta_failures[i] < 2) {
      return false;
    }
  }
  return true;
}

void disable_station_mode() {
  if (!config::get().global.wifi_sta_enabled) {
    return;
  }
  Serial.println("[net] STA mode disabled (exhausted)");
  config::Config& cfg = config::get_mutable();
  cfg.global.wifi_sta_enabled = false;
  config::save();
  reset_sta_attempts();
  WiFi.disconnect();
}

void start_scan(uint32_t now) {
  const int state = WiFi.scanComplete();
  if (state == WIFI_SCAN_RUNNING) {
    return;
  }
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  s_scan_running = true;
  s_last_scan_ms = now;
  Serial.println("[net] Started async scan");
}

void poll_scan() {
  const int result = WiFi.scanComplete();
  if (result == WIFI_SCAN_RUNNING) {
    return;
  }
  if (result < 0) {
    s_scan_running = false;
    return;
  }
  Serial.print("[net] Scan done: ");
  Serial.println(result);
  store_scan_results(result);
  WiFi.scanDelete();
  s_scan_running = false;
}

void start_mdns() {
  if (s_mdns_started) {
    return;
  }
  if (MDNS.begin(kHostname)) {
    MDNS.addService("http", "tcp", 80);
    s_mdns_started = true;
#if defined(STA_AP_TEST)
    NET_TEST_LOG("[net][test] mDNS started");
#endif
  }
}

void stop_mdns() {
  if (!s_mdns_started) {
    return;
  }
  MDNS.end();
  s_mdns_started = false;
#if defined(STA_AP_TEST)
  NET_TEST_LOG("[net][test] mDNS stopped");
#endif
}

void start_ap() {
  if (s_ap_active) {
    return;
  }
  WiFi.mode(WIFI_AP_STA);
  Serial.println("[net] AP start: canlogger-setup");
  const bool ok = WiFi.softAP(kApSsid, kApPassword);
  if (ok) {
    Serial.print("[net] AP IP: ");
    Serial.println(WiFi.softAPIP());
  } else {
    Serial.println("[net] AP start failed");
  }
  s_ap_active = true;
#if defined(STA_AP_TEST)
  NET_TEST_LOG("[net][test] AP start done");
#endif
}

void stop_ap() {
  if (!s_ap_active) {
    return;
  }
  Serial.println("[net] AP stop");
  WiFi.softAPdisconnect(true);
  s_ap_active = false;
#if defined(STA_AP_TEST)
  NET_TEST_LOG("[net][test] AP stopped");
#endif
}

bool begin_next_network() {
  const uint8_t count = configured_wifi_count();
  const config::Config& cfg = config::get();

  Serial.print("[net] STA candidate count: ");
  Serial.println(count);
  while (s_ssid_index < count) {
    if (is_valid_ssid(s_ssid_index)) {
      if (s_sta_failures[s_ssid_index] >= 2) {
        Serial.print("[net] STA skip exhausted SSID index: ");
        Serial.println(s_ssid_index);
        ++s_ssid_index;
        continue;
      }
      const config::WifiConfig& wifi = cfg.global.wifi[s_ssid_index];
      Serial.print("[net] STA connect: ");
      Serial.println(wifi.ssid);
      Serial.print("[net] STA index: ");
      Serial.println(s_ssid_index);
      WiFi.begin(wifi.ssid, wifi.password);
      s_connecting = true;
      s_attempt_start_ms = millis();
      return true;
    }
    ++s_ssid_index;
  }
  Serial.println("[net] STA no valid SSID candidates");
  return false;
}

void handle_sta_failure(const char* reason) {
  if (!station_mode_enabled()) {
    return;
  }
  const uint8_t count = configured_wifi_count();
  if (count == 0) {
    disable_station_mode();
    return;
  }
  if (s_ssid_index >= count) {
    s_ssid_index = 0;
  }
  if (s_ssid_index < 3 && is_valid_ssid(s_ssid_index)) {
    ++s_sta_failures[s_ssid_index];
    Serial.print("[net] STA failure (");
    Serial.print(reason);
    Serial.print(") idx=");
    Serial.print(s_ssid_index);
    Serial.print(" count=");
    Serial.println(s_sta_failures[s_ssid_index]);
    if (s_sta_failures[s_ssid_index] >= 2) {
      ++s_ssid_index;
    }
  }
  s_connecting = false;
  s_attempt_start_ms = 0;
  if (all_sta_exhausted(count)) {
    disable_station_mode();
    return;
  }
  s_next_retry_ms = millis() + kRetryIntervalMs;
}

} // namespace

void init() {
  WiFi.setAutoReconnect(false);
  WiFi.persistent(false);
  WiFi.disconnect(true, true);
  WiFi.setHostname(kHostname);
  WiFi.setSleep(false);
  Serial.print("[net] Init, mode: ");
  Serial.println(WiFi.getMode());
#if defined(STA_AP_TEST)
  NET_TEST_LOG("[net][test] STA/AP test mode enabled");
#endif

  if (!s_event_registered) {
    WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
      Serial.print("[net] Event: ");
      Serial.println(static_cast<int>(event));
      if (event == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        Serial.println("[net] STA connected");
        start_mdns();
      } else if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
        Serial.println("[net] STA disconnected");
        stop_mdns();
        handle_sta_failure("disconnected");
      } else if (event == ARDUINO_EVENT_WIFI_STA_START) {
        Serial.println("[net] STA start");
      } else if (event == ARDUINO_EVENT_WIFI_AP_START) {
        Serial.println("[net] AP start event");
        s_ap_active = true;
      } else if (event == ARDUINO_EVENT_WIFI_AP_STOP) {
        Serial.println("[net] AP stop event");
        s_ap_active = false;
    } else if (event == ARDUINO_EVENT_WIFI_AP_STACONNECTED) {
      const uint8_t* mac = info.wifi_ap_staconnected.mac;
      Serial.printf("[net] AP STA connected: %02X:%02X:%02X:%02X:%02X:%02X aid=%u\n",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
                      info.wifi_ap_staconnected.aid);
      s_last_ap_client_ms = millis();
    } else if (event == ARDUINO_EVENT_WIFI_AP_STADISCONNECTED) {
      const uint8_t* mac = info.wifi_ap_stadisconnected.mac;
      Serial.printf(
          "[net] AP STA disconnected: %02X:%02X:%02X:%02X:%02X:%02X aid=%u\n",
          mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
          info.wifi_ap_stadisconnected.aid);
      s_last_ap_client_ms = millis();
    }
    });
    s_event_registered = true;
  }

  s_initialized = true;
  s_sta_mode_cached = config::get().global.wifi_sta_enabled;
}

void connect() {
  if (!s_initialized) {
    return;
  }

  s_ssid_index = 0;
  s_connecting = false;
  s_next_retry_ms = 0;
  s_attempt_start_ms = 0;

  WiFi.mode(WIFI_AP_STA);
  start_ap();

  if (!station_mode_enabled()) {
    Serial.println("[net] STA mode disabled; AP only");
    WiFi.disconnect();
    reset_sta_attempts();
    s_last_scan_ms = 0;
    start_scan(millis());
    return;
  }

  const uint8_t count = configured_wifi_count();
  if (count == 0) {
    Serial.println("[net] STA mode enabled but no SSIDs; switching to AP");
    disable_station_mode();
    return;
  }

  if (ap_client_count() > 0) {
    Serial.println("[net] STA connect skipped (AP client active)");
    return;
  }

  begin_next_network();
}

void disconnect() {
  s_connecting = false;
  s_next_retry_ms = 0;
  stop_mdns();
  stop_ap();
  WiFi.disconnect();
}

void loop() {
  if (!s_initialized) {
    return;
  }

  const uint32_t now = millis();
  const bool sta_enabled = station_mode_enabled();
  if (sta_enabled != s_sta_mode_cached) {
    s_sta_mode_cached = sta_enabled;
    Serial.print("[net] STA mode changed: ");
    Serial.println(sta_enabled ? "on" : "off");
    reset_sta_attempts();
    if (!sta_enabled) {
      WiFi.disconnect();
      stop_mdns();
    }
  }

  if ((WiFi.getMode() & WIFI_AP) == 0) {
    s_ap_active = false;
  }
  if (!s_ap_active) {
    WiFi.mode(WIFI_AP_STA);
    start_ap();
  }

  if (!sta_enabled) {
    if (ap_client_count() == 0 && (now - s_last_ap_client_ms) >= kScanCooldownMs) {
      if (!s_scan_running && (now - s_last_scan_ms) >= kScanIntervalMs) {
        start_scan(now);
      }
      poll_scan();
    } else if (s_scan_running) {
      WiFi.scanDelete();
      s_scan_running = false;
    }
  } else if (s_scan_running) {
    WiFi.scanDelete();
    s_scan_running = false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    if (!s_mdns_started) {
      start_mdns();
    }
    return;
  }

  if (!sta_enabled) {
    return;
  }

  if (ap_client_count() > 0) {
    if (s_connecting) {
      Serial.println("[net] STA connect aborted (AP client active)");
      WiFi.disconnect();
      s_connecting = false;
      s_attempt_start_ms = 0;
      s_next_retry_ms = now + kRetryIntervalMs;
    }
    return;
  }

  if (s_connecting) {
    if (now - s_attempt_start_ms >= kConnectTimeoutMs) {
      Serial.println("[net] STA connect timeout");
      handle_sta_failure("timeout");
    }
    return;
  }

  if (s_next_retry_ms != 0 && now < s_next_retry_ms) {
    return;
  }
  s_next_retry_ms = 0;

  if (!begin_next_network()) {
    if (all_sta_exhausted(configured_wifi_count())) {
      disable_station_mode();
    } else {
      s_next_retry_ms = now + kRetryIntervalMs;
    }
  }
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
  return rssi_to_percent(rssi_dbm());
}

uint8_t ap_clients() {
  return ap_client_count();
}

size_t wifi_scan_count() {
  portENTER_CRITICAL(&s_scan_mutex);
  const size_t count = s_scan_count;
  portEXIT_CRITICAL(&s_scan_mutex);
  return count;
}

bool wifi_scan_entry(size_t index, WifiScanEntry* out) {
  if (!out) {
    return false;
  }
  portENTER_CRITICAL(&s_scan_mutex);
  if (index >= s_scan_count) {
    portEXIT_CRITICAL(&s_scan_mutex);
    return false;
  }
  *out = s_scan_results[index];
  portEXIT_CRITICAL(&s_scan_mutex);
  return true;
}

} // namespace net
