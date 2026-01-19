#include "rest/rest_api.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <WiFi.h>
#include <WebServer.h>
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

bool token_configured() {
  return config::get().global.api_token[0] != '\0';
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
  s_server.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

void send_json(const JsonDocument& doc, int code = 200) {
  String body;
  serializeJson(doc, body);
  s_server.send(code, "application/json", body);
}

void add_config_json(JsonObject root, const config::Config& cfg) {
  JsonObject global = root.createNestedObject("global");
  global["max_file_size_bytes"] = cfg.global.max_file_size_bytes;
  global["low_space_threshold_bytes"] = cfg.global.low_space_threshold_bytes;
  global["wifi_count"] = cfg.global.wifi_count;
  global["upload_url"] = cfg.global.upload_url;
  global["influx_url"] = cfg.global.influx_url;
  global["influx_token"] = cfg.global.influx_token;
  global["api_token"] = cfg.global.api_token;
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
    obj["termination"] = bus.termination;
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
      if (bus.containsKey("termination")) {
        cfg.buses[id].termination = bus["termination"].as<bool>();
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
  if (!ensure_auth()) {
    return;
  }

  DynamicJsonDocument doc(8192);
  JsonObject root = doc.to<JsonObject>();
  add_config_json(root, config::get());
  send_json(doc);
}

void handle_config_put() {
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

void handle_can_stats() {
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

void handle_control_start() {
  if (!ensure_auth()) {
    return;
  }
  logging::start();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_stop() {
  if (!ensure_auth()) {
    return;
  }
  logging::stop();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_close_file() {
  if (!ensure_auth()) {
    return;
  }
  logging::rotate_files();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_not_found() {
  const String uri = s_server.uri();
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
  }

  s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
}

} // namespace

void init() {
  s_server.on("/api/status", HTTP_GET, handle_status);
  s_server.on("/api/config", HTTP_GET, handle_config_get);
  s_server.on("/api/config", HTTP_PUT, handle_config_put);
  s_server.on("/api/config", HTTP_POST, handle_config_put);
  s_server.on("/api/can/stats", HTTP_GET, handle_can_stats);
  s_server.on("/api/storage/stats", HTTP_GET, handle_storage_stats);
  s_server.on("/api/buffers", HTTP_GET, handle_buffers);
  s_server.on("/api/files", HTTP_GET, handle_files_list);
  s_server.on("/api/control/start_logging", HTTP_POST, handle_control_start);
  s_server.on("/api/control/stop_logging", HTTP_POST, handle_control_stop);
  s_server.on("/api/control/close_active_file", HTTP_POST, handle_control_close_file);
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
