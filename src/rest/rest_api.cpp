#include "rest/rest_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <WebServer.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "logging/log_writer.h"
#include "net/net_manager.h"
#include "storage/storage_manager.h"

namespace rest {

namespace {

WebServer s_server(80);
bool s_started = false;
bool s_spiffs_ready = false;

bool token_configured() {
  return config::get().global.api_token[0] != '\0';
}

void add_cors_headers() {
  s_server.sendHeader("Access-Control-Allow-Origin", "*");
  s_server.sendHeader("Access-Control-Allow-Methods", "GET,POST,PUT,OPTIONS");
  s_server.sendHeader("Access-Control-Allow-Headers",
                      "Content-Type,Authorization,X-Api-Token");
}

const char* content_type_for_path(const String& path) {
  if (path.endsWith(".html")) {
    return "text/html";
  }
  if (path.endsWith(".css")) {
    return "text/css";
  }
  if (path.endsWith(".js")) {
    return "application/javascript";
  }
  if (path.endsWith(".png")) {
    return "image/png";
  }
  if (path.endsWith(".gif")) {
    return "image/gif";
  }
  if (path.endsWith(".svg")) {
    return "image/svg+xml";
  }
  if (path.endsWith(".ico")) {
    return "image/x-icon";
  }
  return "text/plain";
}

void handle_static() {
  if (!s_spiffs_ready) {
    s_server.send(500, "text/plain", "SPIFFS mount failed");
    return;
  }

  String path = s_server.uri();
  if (path.endsWith("/")) {
    path += "index.html";
  }

  if (!SPIFFS.exists(path)) {
    s_server.send(404, "text/plain", "Not found");
    return;
  }

  File file = SPIFFS.open(path, "r");
  if (!file) {
    s_server.send(500, "text/plain", "Failed to open file");
    return;
  }

  s_server.streamFile(file, content_type_for_path(path));
  file.close();
}

void handle_options() {
  add_cors_headers();
  s_server.send(204, "text/plain", "");
}

bool check_auth() {
  if (!token_configured()) {
    return true;
  }

  const char* token = config::get().global.api_token;
  const String header_token = s_server.header("X-Api-Token");
  if (header_token.length() > 0 && header_token == token) {
    return true;
  }

  const String auth = s_server.header("Authorization");
  if (auth.startsWith("Bearer ")) {
    const String bearer = auth.substring(7);
    return bearer == token;
  }

  return false;
}

bool ensure_auth() {
  if (check_auth()) {
    return true;
  }
  add_cors_headers();
  s_server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

void send_json(const JsonDocument& doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  add_cors_headers();
  s_server.send(code, "application/json", body);
}

void add_config_json(JsonObject root, const config::Config& cfg) {
  JsonObject global = root.createNestedObject("global");
  global["max_file_size_bytes"] = cfg.global.max_file_size_bytes;
  global["low_space_threshold_bytes"] = cfg.global.low_space_threshold_bytes;
  global["wifi_count"] = cfg.global.wifi_count;
  global["wifi_sta_enabled"] = cfg.global.wifi_sta_enabled;
  global["upload_url"] = cfg.global.upload_url;
  global["influx_url"] = cfg.global.influx_url;
  global["influx_token"] = cfg.global.influx_token;
  global["api_token"] = cfg.global.api_token;
  global["can_time_sync"] = cfg.global.can_time_sync;
  global["manual_time_epoch"] = cfg.global.manual_time_epoch;
  global["dbc_name"] = cfg.global.dbc_name;
  JsonArray wifi = global.createNestedArray("wifi");
  for (uint8_t i = 0; i < 3; ++i) {
    JsonObject entry = wifi.createNestedObject();
    entry["ssid"] = cfg.global.wifi[i].ssid;
    entry["password"] = cfg.global.wifi[i].password;
  }

  JsonArray buses = root.createNestedArray("buses");
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    const config::BusConfig& bus = cfg.buses[i];
    JsonObject obj = buses.createNestedObject();
    obj["id"] = i;
    obj["enabled"] = bus.enabled;
    obj["bitrate"] = bus.bitrate;
    obj["read_only"] = bus.read_only;
    obj["logging"] = bus.logging;
    obj["name"] = bus.name;
  }
}

void apply_config_from_json(const JsonObject& root) {
  config::Config& cfg = config::get_mutable();

  if (root.containsKey("global")) {
    JsonObject global = root["global"].as<JsonObject>();
    if (global.containsKey("max_file_size_bytes")) {
      cfg.global.max_file_size_bytes = global["max_file_size_bytes"].as<uint32_t>();
    }
    if (global.containsKey("low_space_threshold_bytes")) {
      cfg.global.low_space_threshold_bytes =
          global["low_space_threshold_bytes"].as<uint32_t>();
    }
    if (global.containsKey("wifi_count")) {
      config::set_wifi_count(global["wifi_count"].as<uint8_t>());
    }
    if (global.containsKey("wifi_sta_enabled")) {
      cfg.global.wifi_sta_enabled = global["wifi_sta_enabled"].as<bool>();
    }
    if (global.containsKey("upload_url")) {
      const char* value = global["upload_url"] | "";
      strncpy(cfg.global.upload_url, value, sizeof(cfg.global.upload_url));
      cfg.global.upload_url[sizeof(cfg.global.upload_url) - 1] = '\0';
    }
    if (global.containsKey("influx_url")) {
      const char* value = global["influx_url"] | "";
      strncpy(cfg.global.influx_url, value, sizeof(cfg.global.influx_url));
      cfg.global.influx_url[sizeof(cfg.global.influx_url) - 1] = '\0';
    }
    if (global.containsKey("influx_token")) {
      const char* value = global["influx_token"] | "";
      strncpy(cfg.global.influx_token, value, sizeof(cfg.global.influx_token));
      cfg.global.influx_token[sizeof(cfg.global.influx_token) - 1] = '\0';
    }
    if (global.containsKey("api_token")) {
      const char* value = global["api_token"] | "";
      strncpy(cfg.global.api_token, value, sizeof(cfg.global.api_token));
      cfg.global.api_token[sizeof(cfg.global.api_token) - 1] = '\0';
    }
    if (global.containsKey("can_time_sync")) {
      cfg.global.can_time_sync = global["can_time_sync"].as<bool>();
    }
    if (global.containsKey("manual_time_epoch")) {
      cfg.global.manual_time_epoch = global["manual_time_epoch"].as<int64_t>();
    }
    if (global.containsKey("dbc_name")) {
      const char* value = global["dbc_name"] | "";
      strncpy(cfg.global.dbc_name, value, sizeof(cfg.global.dbc_name));
      cfg.global.dbc_name[sizeof(cfg.global.dbc_name) - 1] = '\0';
    }
    if (global.containsKey("wifi")) {
      JsonArray wifi = global["wifi"].as<JsonArray>();
      uint8_t count = 0;
      for (JsonObject entry : wifi) {
        if (count >= 3) {
          break;
        }
        const char* ssid = entry["ssid"] | "";
        const char* password = entry["password"] | "";
        config::set_wifi(count, ssid, password);
        ++count;
      }
      config::set_wifi_count(count);
    }
  }

  if (root.containsKey("buses")) {
    JsonArray buses = root["buses"].as<JsonArray>();
    for (JsonObject bus : buses) {
      if (!bus.containsKey("id")) {
        continue;
      }
      const uint8_t id = bus["id"].as<uint8_t>();
      if (id >= config::kMaxBuses) {
        continue;
      }
      if (bus.containsKey("enabled")) {
        cfg.buses[id].enabled = bus["enabled"].as<bool>();
      }
      if (bus.containsKey("bitrate")) {
        cfg.buses[id].bitrate = bus["bitrate"].as<uint32_t>();
      }
      if (bus.containsKey("read_only")) {
        cfg.buses[id].read_only = bus["read_only"].as<bool>();
      }
      if (bus.containsKey("logging")) {
        cfg.buses[id].logging = bus["logging"].as<bool>();
      }
      if (bus.containsKey("name")) {
        const char* name = bus["name"] | "";
        config::set_bus_name(id, name);
      }
    }
  }

  config::save();
}

void handle_status() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(4096);
  JsonObject root = doc.to<JsonObject>();
  root["uptime_sec"] = millis() / 1000;
  root["wifi_connected"] = net::is_connected();
  root["ip"] = WiFi.localIP().toString();
  root["ssid"] = WiFi.SSID();
  root["rssi_dbm"] = net::rssi_dbm();
  root["rssi_percent"] = net::rssi_percent();
  root["sta_mode_enabled"] = config::get().global.wifi_sta_enabled;
  root["ap_clients"] = net::ap_clients();
  time_t now = time(nullptr);
  root["time_epoch"] = static_cast<int64_t>(now);
  root["time_valid"] = now > 100000;

  logging::Stats log_stats = logging::get_stats();
  JsonObject log = root.createNestedObject("logging");
  log["total_bytes"] = log_stats.total_bytes;
  log["bytes_per_sec"] = log_stats.bytes_per_sec;
  log["active_buses"] = log_stats.active_buses;
  log["started"] = log_stats.started;
  log["open_failures"] = log_stats.open_failures;
  log["write_failures"] = log_stats.write_failures;
  log["last_write_ms"] = log_stats.last_write_ms;

  storage::Stats st = storage::get_stats();
  JsonObject storage_obj = root.createNestedObject("storage");
  storage_obj["ready"] = storage::is_ready();
  storage_obj["total_bytes"] = st.total_bytes;
  storage_obj["free_bytes"] = st.free_bytes;

  JsonArray can_stats = root.createNestedArray("can");
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    JsonObject entry = can_stats.createNestedObject();
    entry["bus"] = i;
    entry["drops"] = can::drop_count(i);
    entry["high_water"] = can::high_water(i);
  }

  send_json(doc);
}

void handle_config_get() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(8192);
  JsonObject root = doc.to<JsonObject>();
  add_config_json(root, config::get());
  send_json(doc);
}

void handle_config_put() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  const String body = s_server.arg("plain");
  if (body.length() == 0) {
    s_server.send(400, "application/json", "{\"error\":\"empty_body\"}");
    return;
  }

  DynamicJsonDocument doc(8192);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    s_server.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  apply_config_from_json(root);
  net::connect();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_time_set() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  const String body = s_server.arg("plain");
  if (body.length() == 0) {
    s_server.send(400, "application/json", "{\"error\":\"empty_body\"}");
    return;
  }

  DynamicJsonDocument doc(512);
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    s_server.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }

  if (!doc.containsKey("epoch")) {
    s_server.send(400, "application/json", "{\"error\":\"missing_epoch\"}");
    return;
  }

  const int64_t epoch = doc["epoch"].as<int64_t>();
  if (epoch <= 0) {
    s_server.send(400, "application/json", "{\"error\":\"invalid_epoch\"}");
    return;
  }

  timeval tv{};
  tv.tv_sec = static_cast<time_t>(epoch);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);

  config::Config& cfg = config::get_mutable();
  cfg.global.manual_time_epoch = epoch;
  config::save();

  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_wifi_scan() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  const size_t count = net::wifi_scan_count();
  for (size_t i = 0; i < count; ++i) {
    net::WifiScanEntry info{};
    if (!net::wifi_scan_entry(i, &info)) {
      continue;
    }
    JsonObject entry = arr.createNestedObject();
    entry["ssid"] = info.ssid;
    entry["rssi_dbm"] = info.rssi_dbm;
    entry["rssi_percent"] = info.rssi_percent;
    entry["channel"] = info.channel;
    entry["secure"] = info.secure;
  }
  send_json(doc);
}

void handle_can_stats() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    JsonObject entry = arr.createNestedObject();
    entry["bus"] = i;
    entry["drops"] = can::drop_count(i);
    entry["high_water"] = can::high_water(i);
  }
  send_json(doc);
}

void handle_storage_stats() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(1024);
  JsonObject obj = doc.to<JsonObject>();
  storage::Stats st = storage::get_stats();
  obj["ready"] = storage::is_ready();
  obj["total_bytes"] = st.total_bytes;
  obj["free_bytes"] = st.free_bytes;
  send_json(doc);
}

void handle_buffers() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(2048);
  JsonObject root = doc.to<JsonObject>();
  logging::Stats stats = logging::get_stats();
  root["started"] = stats.started;
  root["total_bytes"] = stats.total_bytes;
  root["bytes_per_sec"] = stats.bytes_per_sec;
  root["write_failures"] = stats.write_failures;
  root["open_failures"] = stats.open_failures;
  root["active_buses"] = stats.active_buses;
  root["last_write_ms"] = stats.last_write_ms;
  send_json(doc);
}

void handle_files_list() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(8192);
  JsonArray arr = doc.to<JsonArray>();
  const size_t count = storage::file_count();
  for (size_t i = 0; i < count; ++i) {
    storage::FileInfo info{};
    if (!storage::get_file_info(i, &info)) {
      continue;
    }
    JsonObject entry = arr.createNestedObject();
    entry["id"] = i;
    entry["path"] = info.path;
    entry["start_ms"] = info.start_ms;
    entry["end_ms"] = info.end_ms;
    entry["size_bytes"] = info.size_bytes;
    entry["checksum"] = info.checksum;
    entry["bus_id"] = info.bus_id;
    entry["flags"] = info.flags;
  }
  send_json(doc);
}

bool parse_file_route(const String& uri, size_t* out_id, String* out_action) {
  const String prefix = "/api/files/";
  if (!uri.startsWith(prefix)) {
    return false;
  }

  String tail = uri.substring(prefix.length());
  int slash = tail.indexOf('/');
  String id_str = (slash >= 0) ? tail.substring(0, slash) : tail;
  if (id_str.length() == 0) {
    return false;
  }

  char* endptr = nullptr;
  const unsigned long id = strtoul(id_str.c_str(), &endptr, 10);
  if (endptr == id_str.c_str() || *endptr != '\0') {
    return false;
  }

  if (out_id) {
    *out_id = static_cast<size_t>(id);
  }
  if (out_action) {
    *out_action = (slash >= 0) ? tail.substring(slash + 1) : "";
  }
  return true;
}

void handle_file_download(size_t id) {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  storage::FileInfo info{};
  if (!storage::get_file_info(id, &info)) {
    s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
    return;
  }

  if (!SD.exists(info.path)) {
    s_server.send(404, "application/json", "{\"error\":\"missing\"}");
    return;
  }

  File file = SD.open(info.path, FILE_READ);
  if (!file) {
    s_server.send(500, "application/json", "{\"error\":\"open_failed\"}");
    return;
  }

  const char* base = strrchr(info.path, '/');
  const char* name = base ? base + 1 : info.path;
  String disposition = String("attachment; filename=\"") + name + "\"";
  s_server.sendHeader("Content-Disposition", disposition);
  const size_t sent = s_server.streamFile(file, "application/octet-stream");
  file.close();
  if (sent > 0) {
    storage::mark_downloaded(info.path);
  }
}

void handle_file_mark_downloaded(size_t id) {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  storage::FileInfo info{};
  if (!storage::get_file_info(id, &info)) {
    s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
    return;
  }
  storage::mark_downloaded(info.path);
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_file_delete(size_t id) {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  if (!storage::delete_file(id)) {
    s_server.send(400, "application/json", "{\"error\":\"delete_failed\"}");
    return;
  }
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_start() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }
  logging::start();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_stop() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }
  logging::stop();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_close_file() {
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }
  logging::rotate_files();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_not_found() {
  add_cors_headers();
  if (s_server.method() == HTTP_OPTIONS) {
    s_server.send(204, "text/plain", "");
    return;
  }
  const String uri = s_server.uri();
  if (!uri.startsWith("/api/")) {
    handle_static();
    return;
  }
  size_t id = 0;
  String action;
  if (parse_file_route(uri, &id, &action)) {
    if (action == "download" && s_server.method() == HTTP_GET) {
      handle_file_download(id);
      return;
    }
    if (action == "mark_downloaded" && s_server.method() == HTTP_POST) {
      handle_file_mark_downloaded(id);
      return;
    }
    if (action == "delete" && s_server.method() == HTTP_POST) {
      handle_file_delete(id);
      return;
    }
  }

  s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
}

} // namespace

void init() {
  s_spiffs_ready = SPIFFS.begin(true);
  s_server.on("/api/status", HTTP_GET, handle_status);
  s_server.on("/api/status", HTTP_OPTIONS, handle_options);
  s_server.on("/api/config", HTTP_GET, handle_config_get);
  s_server.on("/api/config", HTTP_PUT, handle_config_put);
  s_server.on("/api/config", HTTP_POST, handle_config_put);
  s_server.on("/api/config", HTTP_OPTIONS, handle_options);
  s_server.on("/api/time", HTTP_POST, handle_time_set);
  s_server.on("/api/time", HTTP_OPTIONS, handle_options);
  s_server.on("/api/wifi/scan", HTTP_GET, handle_wifi_scan);
  s_server.on("/api/wifi/scan", HTTP_OPTIONS, handle_options);
  s_server.on("/api/can/stats", HTTP_GET, handle_can_stats);
  s_server.on("/api/can/stats", HTTP_OPTIONS, handle_options);
  s_server.on("/api/storage/stats", HTTP_GET, handle_storage_stats);
  s_server.on("/api/storage/stats", HTTP_OPTIONS, handle_options);
  s_server.on("/api/buffers", HTTP_GET, handle_buffers);
  s_server.on("/api/buffers", HTTP_OPTIONS, handle_options);
  s_server.on("/api/files", HTTP_GET, handle_files_list);
  s_server.on("/api/files", HTTP_OPTIONS, handle_options);
  s_server.on("/api/control/start_logging", HTTP_POST, handle_control_start);
  s_server.on("/api/control/stop_logging", HTTP_POST, handle_control_stop);
  s_server.on("/api/control/close_active_file", HTTP_POST, handle_control_close_file);
  s_server.on("/api/control/start_logging", HTTP_OPTIONS, handle_options);
  s_server.on("/api/control/stop_logging", HTTP_OPTIONS, handle_options);
  s_server.on("/api/control/close_active_file", HTTP_OPTIONS, handle_options);
  s_server.onNotFound(handle_not_found);
}

void start() {
  if (s_started) {
    return;
  }
  s_server.begin();
  s_started = true;
}

void stop() {
  if (!s_started) {
    return;
  }
  s_server.stop();
  s_started = false;
}

void loop() {
  if (!s_started) {
    return;
  }
  s_server.handleClient();
}

} // namespace rest
