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
#include "compress/compressor.h"
#include "config/app_config.h"
#include "logging/log_writer.h"
#include "net/net_manager.h"
#include "storage/storage_manager.h"
#include "upload/upload_manager.h"

namespace rest {

namespace {

WebServer s_server(80);
bool s_started = false;
bool s_spiffs_ready = false;
volatile uint32_t s_last_activity_ms = 0;
constexpr uint32_t kSlowRequestThresholdMs = 120;
constexpr uint32_t kSlowHandleClientThresholdMs = 40;
const char* http_method_name(HTTPMethod method);

void mark_activity() {
  s_last_activity_ms = millis();
}

void log_slow_request_if_needed(const char* tag, uint32_t start_ms) {
  const uint32_t elapsed = millis() - start_ms;
  if (elapsed < kSlowRequestThresholdMs) {
    return;
  }
  Serial.printf("[REST][SLOW] %s took %lums method=%s uri=%s freeHeap=%lu\n",
                tag != nullptr ? tag : "handler",
                static_cast<unsigned long>(elapsed),
                http_method_name(s_server.method()),
                s_server.uri().c_str(),
                static_cast<unsigned long>(ESP.getFreeHeap()));
}

bool token_configured() {
  return config::get().global.api_token[0] != '\0';
}

const char* http_method_name(HTTPMethod method) {
  switch (method) {
    case HTTP_GET:
      return "GET";
    case HTTP_POST:
      return "POST";
    case HTTP_PUT:
      return "PUT";
    case HTTP_PATCH:
      return "PATCH";
    case HTTP_DELETE:
      return "DELETE";
    case HTTP_OPTIONS:
      return "OPTIONS";
    case HTTP_HEAD:
      return "HEAD";
    default:
      return "UNKNOWN";
  }
}

void log_not_found_request() {
  const IPAddress remote = s_server.client().remoteIP();
  Serial.printf(
      "[REST] NOT_FOUND method=%s uri=%s remote=%u.%u.%u.%u args=%d\n",
      http_method_name(s_server.method()), s_server.uri().c_str(), remote[0],
      remote[1], remote[2], remote[3], s_server.args());
  for (int i = 0; i < s_server.args(); ++i) {
    Serial.printf("[REST]   arg[%d] %s=%s\n", i, s_server.argName(i).c_str(),
                  s_server.arg(i).c_str());
  }
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

bool is_cacheable_static_asset(const String& path) {
  return path.endsWith(".css") || path.endsWith(".js") || path.endsWith(".png") ||
         path.endsWith(".gif") || path.endsWith(".svg") || path.endsWith(".ico");
}

void handle_static() {
  mark_activity();
  const uint32_t handler_start = millis();
  if (!s_spiffs_ready) {
    s_server.send(500, "text/plain", "SPIFFS mount failed");
    log_slow_request_if_needed("static", handler_start);
    return;
  }

  String path = s_server.uri();
  const int query = path.indexOf('?');
  if (query >= 0) {
    path = path.substring(0, query);
  }
  if (!path.startsWith("/")) {
    path = "/" + path;
  }
  while (path.indexOf("//") >= 0) {
    path.replace("//", "/");
  }
  if (path.endsWith("/")) {
    path += "index.html";
  }
  if (path == "/index") {
    path = "/index.html";
  }

  bool use_gzip = false;
  String file_path = path;
  String gzip_path = path + ".gz";
  if (SPIFFS.exists(gzip_path)) {
    use_gzip = true;
    file_path = gzip_path;
  }

  if (!SPIFFS.exists(file_path)) {
    s_server.send(404, "text/plain", "Not found");
    log_slow_request_if_needed("static", handler_start);
    return;
  }

  const uint32_t open_start = millis();
  File file = SPIFFS.open(file_path, "r");
  if (!file) {
    s_server.send(500, "text/plain", "Failed to open file");
    log_slow_request_if_needed("static", handler_start);
    return;
  }
  const uint32_t open_elapsed = millis() - open_start;
  if (open_elapsed >= kSlowRequestThresholdMs) {
    Serial.printf("[REST][SLOW] static open took %lums path=%s\n",
                  static_cast<unsigned long>(open_elapsed), file_path.c_str());
  }

  WiFiClient client = s_server.client();
  client.setNoDelay(true);
  if (is_cacheable_static_asset(path)) {
    s_server.sendHeader("Cache-Control", "public, max-age=86400, immutable");
  } else {
    s_server.sendHeader("Cache-Control", "no-cache");
  }
  if (use_gzip) {
    s_server.sendHeader("Content-Encoding", "gzip");
  }

  const uint32_t stream_start = millis();
  s_server.streamFile(file, content_type_for_path(path));
  const uint32_t stream_elapsed = millis() - stream_start;
  if (stream_elapsed >= kSlowRequestThresholdMs) {
    Serial.printf("[REST][SLOW] static stream took %lums path=%s\n",
                  static_cast<unsigned long>(stream_elapsed), path.c_str());
  }
  file.close();
  log_slow_request_if_needed("static", handler_start);
}

void handle_options() {
  mark_activity();
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
  JsonObject global = root["global"].to<JsonObject>();
  global["max_file_size_bytes"] = cfg.global.max_file_size_bytes;
  global["low_space_threshold_bytes"] = cfg.global.low_space_threshold_bytes;
  global["wifi_count"] = cfg.global.wifi_count;
  global["wifi_sta_enabled"] = cfg.global.wifi_sta_enabled;
  global["auto_upload_enabled"] = cfg.global.auto_upload_enabled;
  global["compressor_enabled"] = cfg.global.compressor_enabled;
  global["upload_url"] = cfg.global.upload_url;
  global["influx_url"] = cfg.global.influx_url;
  global["influx_token"] = cfg.global.influx_token;
  global["api_token"] = cfg.global.api_token;
  global["can_time_sync"] = cfg.global.can_time_sync;
  global["manual_time_epoch"] = cfg.global.manual_time_epoch;
  global["dbc_name"] = cfg.global.dbc_name;
  JsonArray wifi = global["wifi"].to<JsonArray>();
  for (uint8_t i = 0; i < 3; ++i) {
    JsonObject entry = wifi.add<JsonObject>();
    entry["ssid"] = cfg.global.wifi[i].ssid;
    entry["password"] = cfg.global.wifi[i].password;
  }

  JsonArray buses = root["buses"].to<JsonArray>();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    const config::BusConfig& bus = cfg.buses[i];
    JsonObject obj = buses.add<JsonObject>();
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

  if (!root["global"].isNull()) {
    JsonObject global = root["global"].as<JsonObject>();
    if (!global["max_file_size_bytes"].isNull()) {
      cfg.global.max_file_size_bytes = global["max_file_size_bytes"].as<uint32_t>();
    }
    if (!global["low_space_threshold_bytes"].isNull()) {
      cfg.global.low_space_threshold_bytes =
          global["low_space_threshold_bytes"].as<uint32_t>();
    }
    if (!global["wifi_count"].isNull()) {
      config::set_wifi_count(global["wifi_count"].as<uint8_t>());
    }
    if (!global["wifi_sta_enabled"].isNull()) {
      cfg.global.wifi_sta_enabled = global["wifi_sta_enabled"].as<bool>();
    }
    if (!global["auto_upload_enabled"].isNull()) {
      cfg.global.auto_upload_enabled = global["auto_upload_enabled"].as<bool>();
    }
    if (!global["compressor_enabled"].isNull()) {
      cfg.global.compressor_enabled = global["compressor_enabled"].as<bool>();
    }
    if (!global["upload_url"].isNull()) {
      const char* value = global["upload_url"] | "";
      strncpy(cfg.global.upload_url, value, sizeof(cfg.global.upload_url));
      cfg.global.upload_url[sizeof(cfg.global.upload_url) - 1] = '\0';
    }
    if (!global["influx_url"].isNull()) {
      const char* value = global["influx_url"] | "";
      strncpy(cfg.global.influx_url, value, sizeof(cfg.global.influx_url));
      cfg.global.influx_url[sizeof(cfg.global.influx_url) - 1] = '\0';
    }
    if (!global["influx_token"].isNull()) {
      const char* value = global["influx_token"] | "";
      strncpy(cfg.global.influx_token, value, sizeof(cfg.global.influx_token));
      cfg.global.influx_token[sizeof(cfg.global.influx_token) - 1] = '\0';
    }
    if (!global["api_token"].isNull()) {
      const char* value = global["api_token"] | "";
      strncpy(cfg.global.api_token, value, sizeof(cfg.global.api_token));
      cfg.global.api_token[sizeof(cfg.global.api_token) - 1] = '\0';
    }
    if (!global["can_time_sync"].isNull()) {
      cfg.global.can_time_sync = global["can_time_sync"].as<bool>();
    }
    if (!global["manual_time_epoch"].isNull()) {
      cfg.global.manual_time_epoch = global["manual_time_epoch"].as<int64_t>();
    }
    if (!global["dbc_name"].isNull()) {
      const char* value = global["dbc_name"] | "";
      strncpy(cfg.global.dbc_name, value, sizeof(cfg.global.dbc_name));
      cfg.global.dbc_name[sizeof(cfg.global.dbc_name) - 1] = '\0';
    }
    if (!global["wifi"].isNull()) {
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

  if (!root["buses"].isNull()) {
    JsonArray buses = root["buses"].as<JsonArray>();
    for (JsonObject bus : buses) {
      if (bus["id"].isNull()) {
        continue;
      }
      const uint8_t id = bus["id"].as<uint8_t>();
      if (id >= config::kMaxBuses) {
        continue;
      }
      if (!bus["enabled"].isNull()) {
        cfg.buses[id].enabled = bus["enabled"].as<bool>();
      }
      if (!bus["bitrate"].isNull()) {
        cfg.buses[id].bitrate = bus["bitrate"].as<uint32_t>();
      }
      if (!bus["read_only"].isNull()) {
        cfg.buses[id].read_only = bus["read_only"].as<bool>();
      }
      if (!bus["logging"].isNull()) {
        cfg.buses[id].logging = bus["logging"].as<bool>();
      }
      if (!bus["name"].isNull()) {
        const char* name = bus["name"] | "";
        config::set_bus_name(id, name);
      }
    }
  }

  config::save();
}

void handle_status() {
  mark_activity();
  const uint32_t handler_start = millis();
  add_cors_headers();
  if (!ensure_auth()) {
    log_slow_request_if_needed("status", handler_start);
    return;
  }

  JsonDocument doc;
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
  JsonObject log = root["logging"].to<JsonObject>();
  log["total_bytes"] = log_stats.total_bytes;
  log["bytes_per_sec"] = log_stats.bytes_per_sec;
  log["active_buses"] = log_stats.active_buses;
  log["started"] = log_stats.started;
  log["open_failures"] = log_stats.open_failures;
  log["write_failures"] = log_stats.write_failures;
  log["last_write_ms"] = log_stats.last_write_ms;

  storage::Stats st = storage::get_stats();
  JsonObject storage_obj = root["storage"].to<JsonObject>();
  storage_obj["ready"] = storage::is_ready();
  storage_obj["total_bytes"] = st.total_bytes;
  storage_obj["free_bytes"] = st.free_bytes;
  storage_obj["log_files"] = storage::file_count();

  const uint32_t uploader_stats_start = millis();
  upload::Stats up = upload::get_stats();
  const uint32_t uploader_stats_elapsed = millis() - uploader_stats_start;
  if (uploader_stats_elapsed >= kSlowRequestThresholdMs) {
    Serial.printf("[REST][SLOW] status upload::get_stats took %lums\n",
                  static_cast<unsigned long>(uploader_stats_elapsed));
  }
  JsonObject uploader = root["uploader"].to<JsonObject>();
  uploader["enabled"] = config::get().global.auto_upload_enabled;
  uploader["initialized"] = up.initialized;
  uploader["uploading"] = up.uploading;
  uploader["upload_speed_bps"] = up.upload_speed_bytes_per_sec;
  uploader["current_file_size_bytes"] = up.current_file_size_bytes;
  uploader["current_file_sent_bytes"] = up.current_file_sent_bytes;
  uploader["total_uploaded_files"] = up.total_uploaded_files;
  uploader["total_uploaded_bytes"] = up.total_uploaded_bytes;
  uploader["uploaded_files"] = up.uploaded_files;
  uploader["outstanding_files"] = up.outstanding_files;
  uploader["outstanding_bytes"] = up.outstanding_bytes;
  uploader["last_error"] = up.last_error;
  uploader["last_error_interrupted"] = up.last_error_interrupted;
  uploader["last_error_connect"] = up.last_error_connect;
  uploader["last_error_message"] = up.last_error_message;
  uploader["server_reachable_known"] = up.server_reachable_known;
  uploader["server_reachable"] = up.server_reachable;
  uploader["server_rtt_ms"] = up.server_rtt_ms;
  uploader["server_reach_message"] = up.server_reach_message;

  compressor::Stats cmp = compressor::get_stats();
  JsonObject compressor_obj = root["compressor"].to<JsonObject>();
  compressor_obj["enabled"] = config::get().global.compressor_enabled;
  compressor_obj["active"] = cmp.active;
  compressor_obj["current_input_done_bytes"] = cmp.current_input_done_bytes;
  compressor_obj["current_input_total_bytes"] = cmp.current_input_total_bytes;
  compressor_obj["outstanding_files"] = cmp.outstanding_files;
  compressor_obj["outstanding_bytes"] = cmp.outstanding_bytes;
  compressor_obj["compressed_files_total"] = cmp.compressed_files_total;
  compressor_obj["compressed_input_bytes_total"] = cmp.compressed_input_bytes_total;

  JsonArray can_stats = root["can"].to<JsonArray>();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    const config::BusConfig& bus_cfg = config::get().buses[i];
    const uint32_t depth = can::queue_depth(i);
    const uint32_t capacity = can::queue_capacity();
    const uint32_t load_pct = can::bus_load_pct(i);
    const uint32_t high = can::high_water(i);
    const uint32_t high_pct = (capacity > 0) ? static_cast<uint32_t>((high * 100ULL) / capacity) : 0;
    const uint8_t eflg = can::error_flag_register(i);
    JsonObject entry = can_stats.add<JsonObject>();
    entry["bus"] = i;
    entry["id"] = i;
    entry["name"] = bus_cfg.name;
    entry["enabled"] = bus_cfg.enabled;
    entry["logging"] = bus_cfg.logging;
    entry["read_only"] = bus_cfg.read_only;
    entry["bitrate"] = bus_cfg.bitrate;
    entry["drops"] = can::drop_count(i);
    entry["high_water"] = high;
    entry["high_water_pct"] = high_pct;
    entry["queue_depth"] = depth;
    entry["queue_capacity"] = capacity;
    entry["queue_load_pct"] = load_pct;
    entry["total_received"] = can::total_received(i);
    entry["total_sent"] = can::total_sent(i);
    entry["rx_task_running"] = can::rx_task_running(i);
    entry["rec"] = can::receive_error_counter(i);
    entry["tec"] = can::transmit_error_counter(i);
    entry["eflg"] = eflg;
    entry["bus_off"] = (eflg & 0x20u) != 0;
  }

  send_json(doc);
  log_slow_request_if_needed("status", handler_start);
}

void handle_config_get() {
  mark_activity();
  const uint32_t handler_start = millis();
  add_cors_headers();
  if (!ensure_auth()) {
    log_slow_request_if_needed("config_get", handler_start);
    return;
  }

  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  add_config_json(root, config::get());
  send_json(doc);
  log_slow_request_if_needed("config_get", handler_start);
}

void handle_config_put() {
  mark_activity();
  const uint32_t handler_start = millis();
  add_cors_headers();
  if (!ensure_auth()) {
    log_slow_request_if_needed("config_put", handler_start);
    return;
  }

  const String body = s_server.arg("plain");
  if (body.length() == 0) {
    s_server.send(400, "application/json", "{\"error\":\"empty_body\"}");
    log_slow_request_if_needed("config_put", handler_start);
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    s_server.send(400, "application/json", "{\"error\":\"bad_json\"}");
    log_slow_request_if_needed("config_put", handler_start);
    return;
  }

  JsonObject root = doc.as<JsonObject>();
  apply_config_from_json(root);
  net::connect();
  s_server.send(200, "application/json", "{\"ok\":true}");
  log_slow_request_if_needed("config_put", handler_start);
}

void handle_time_set() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  const String body = s_server.arg("plain");
  if (body.length() == 0) {
    s_server.send(400, "application/json", "{\"error\":\"empty_body\"}");
    return;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);
  if (err) {
    s_server.send(400, "application/json", "{\"error\":\"bad_json\"}");
    return;
  }

  if (doc["epoch"].isNull()) {
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
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  const size_t count = net::wifi_scan_count();
  for (size_t i = 0; i < count; ++i) {
    net::WifiScanEntry info{};
    if (!net::wifi_scan_entry(i, &info)) {
      continue;
    }
    JsonObject entry = arr.add<JsonObject>();
    entry["ssid"] = info.ssid;
    entry["rssi_dbm"] = info.rssi_dbm;
    entry["rssi_percent"] = info.rssi_percent;
    entry["channel"] = info.channel;
    entry["secure"] = info.secure;
  }
  send_json(doc);
}

void handle_can_stats() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    const config::BusConfig& bus_cfg = config::get().buses[i];
    const uint32_t depth = can::queue_depth(i);
    const uint32_t capacity = can::queue_capacity();
    const uint32_t load_pct = can::bus_load_pct(i);
    const uint32_t high = can::high_water(i);
    const uint32_t high_pct = (capacity > 0) ? static_cast<uint32_t>((high * 100ULL) / capacity) : 0;
    const uint8_t eflg = can::error_flag_register(i);
    JsonObject entry = arr.add<JsonObject>();
    entry["bus"] = i;
    entry["id"] = i;
    entry["name"] = bus_cfg.name;
    entry["enabled"] = bus_cfg.enabled;
    entry["logging"] = bus_cfg.logging;
    entry["read_only"] = bus_cfg.read_only;
    entry["bitrate"] = bus_cfg.bitrate;
    entry["drops"] = can::drop_count(i);
    entry["high_water"] = high;
    entry["high_water_pct"] = high_pct;
    entry["queue_depth"] = depth;
    entry["queue_capacity"] = capacity;
    entry["queue_load_pct"] = load_pct;
    entry["total_received"] = can::total_received(i);
    entry["total_sent"] = can::total_sent(i);
    entry["rx_task_running"] = can::rx_task_running(i);
    entry["rec"] = can::receive_error_counter(i);
    entry["tec"] = can::transmit_error_counter(i);
    entry["eflg"] = eflg;
    entry["bus_off"] = (eflg & 0x20u) != 0;
  }
  send_json(doc);
}

void handle_storage_stats() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  JsonDocument doc;
  JsonObject obj = doc.to<JsonObject>();
  storage::Stats st = storage::get_stats();
  obj["ready"] = storage::is_ready();
  obj["total_bytes"] = st.total_bytes;
  obj["free_bytes"] = st.free_bytes;
  send_json(doc);
}

void handle_buffers() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  JsonDocument doc;
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
  mark_activity();
  const uint32_t handler_start = millis();
  add_cors_headers();
  if (!ensure_auth()) {
    log_slow_request_if_needed("files_list", handler_start);
    return;
  }

  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  const size_t count = storage::file_count();
  for (size_t i = 0; i < count; ++i) {
    storage::FileInfo info{};
    if (!storage::get_file_info(i, &info)) {
      continue;
    }
    JsonObject entry = arr.add<JsonObject>();
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
  log_slow_request_if_needed("files_list", handler_start);
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
  mark_activity();
  const uint32_t handler_start = millis();
  add_cors_headers();
  if (!ensure_auth()) {
    log_slow_request_if_needed("file_download", handler_start);
    return;
  }

  storage::FileInfo info{};
  if (!storage::get_file_info(id, &info)) {
    s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
    log_slow_request_if_needed("file_download", handler_start);
    return;
  }
  if (info.flags & 0x04u) {
    s_server.send(409, "application/json", "{\"error\":\"active_file\"}");
    log_slow_request_if_needed("file_download", handler_start);
    return;
  }

  if (!SD.exists(info.path)) {
    s_server.send(404, "application/json", "{\"error\":\"missing\"}");
    log_slow_request_if_needed("file_download", handler_start);
    return;
  }

  File file = SD.open(info.path, FILE_READ);
  if (!file) {
    s_server.send(500, "application/json", "{\"error\":\"open_failed\"}");
    log_slow_request_if_needed("file_download", handler_start);
    return;
  }

  const char* base = strrchr(info.path, '/');
  const char* name = base ? base + 1 : info.path;
  String disposition = String("attachment; filename=\"") + name + "\"";
  s_server.sendHeader("Content-Disposition", disposition);
  const uint32_t stream_start = millis();
  const size_t sent = s_server.streamFile(file, "application/octet-stream");
  const uint32_t stream_elapsed = millis() - stream_start;
  if (stream_elapsed >= kSlowRequestThresholdMs) {
    Serial.printf("[REST][SLOW] file_download stream took %lums path=%s bytes=%lu\n",
                  static_cast<unsigned long>(stream_elapsed),
                  info.path,
                  static_cast<unsigned long>(sent));
  }
  file.close();
  if (sent > 0) {
    storage::mark_downloaded(info.path);
  }
  log_slow_request_if_needed("file_download", handler_start);
}

void handle_file_mark_downloaded(size_t id) {
  mark_activity();
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
  mark_activity();
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

void handle_file_upload(size_t id) {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }

  storage::FileInfo info{};
  if (!storage::get_file_info(id, &info)) {
    s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
    return;
  }
  if (info.flags & storage::kFlagActive) {
    s_server.send(409, "application/json", "{\"error\":\"active_file\"}");
    return;
  }
  upload::request_upload(info.path);
  s_server.send(202, "application/json", "{\"ok\":true,\"queued\":true}");
}

void handle_control_start() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }
  logging::start();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_stop() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }
  logging::stop();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_control_close_file() {
  mark_activity();
  add_cors_headers();
  if (!ensure_auth()) {
    return;
  }
  const String body = s_server.arg("plain");
  if (body.length() > 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);
    if (!err && !doc["bus_id"].isNull()) {
      const uint8_t bus_id = doc["bus_id"].as<uint8_t>();
      logging::rotate_file(bus_id);
      s_server.send(200, "application/json", "{\"ok\":true}");
      return;
    }
  }
  logging::rotate_files();
  s_server.send(200, "application/json", "{\"ok\":true}");
}

void handle_not_found() {
  mark_activity();
  const uint32_t handler_start = millis();
  const String uri = s_server.uri();
  if (uri.startsWith("/api/")) {
    log_not_found_request();
  }
  add_cors_headers();
  if (s_server.method() == HTTP_OPTIONS) {
    s_server.send(204, "text/plain", "");
    return;
  }
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
    if (action == "upload" && s_server.method() == HTTP_POST) {
      handle_file_upload(id);
      return;
    }
  }

  s_server.send(404, "application/json", "{\"error\":\"not_found\"}");
  log_slow_request_if_needed("not_found", handler_start);
}

} // namespace

void init() {
  s_spiffs_ready = SPIFFS.begin(true);
  if (!s_spiffs_ready) {
    Serial.println("[REST] SPIFFS mount failed - static web UI unavailable");
  }

  s_server.on("/", HTTP_GET, handle_static);
  s_server.on("/index.html", HTTP_GET, handle_static);
  s_server.on("/style.css", HTTP_GET, handle_static);
  s_server.on("/app.js", HTTP_GET, handle_static);
  s_server.on("/logo.png", HTTP_GET, handle_static);
  s_server.on("/ajax-loader.gif", HTTP_GET, handle_static);
  s_server.on("/configurate.svg", HTTP_GET, handle_static);
  s_server.on("/globe.svg", HTTP_GET, handle_static);
  s_server.on("/news.svg", HTTP_GET, handle_static);
  s_server.on("/radar.svg", HTTP_GET, handle_static);
  s_server.on("/save.svg", HTTP_GET, handle_static);
  s_server.on("/stack.svg", HTTP_GET, handle_static);
  s_server.on("/refresh.png", HTTP_GET, handle_static);
  s_server.on("/icon-check-circle.png", HTTP_GET, handle_static);
  s_server.on("/icon-trash.png", HTTP_GET, handle_static);
  s_server.on("/icon-x-square.png", HTTP_GET, handle_static);

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
  const uint32_t start_us = micros();
  s_server.handleClient();
  const uint32_t elapsed_us = micros() - start_us;
  if (elapsed_us >= (kSlowHandleClientThresholdMs * 1000UL)) {
    Serial.printf("[REST][SLOW] handleClient took %luus method=%s uri=%s\n",
                  static_cast<unsigned long>(elapsed_us),
                  http_method_name(s_server.method()),
                  s_server.uri().c_str());
  }
}

uint32_t last_activity_ms() {
  return s_last_activity_ms;
}

bool recently_active(uint32_t window_ms) {
  const uint32_t last = s_last_activity_ms;
  if (last == 0) {
    return false;
  }
  return (millis() - last) <= window_ms;
}

} // namespace rest
