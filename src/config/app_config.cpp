#include "config/app_config.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace config {

namespace {

constexpr uint32_t kConfigMagic = 0x43414742; // "CAGB"
constexpr uint16_t kConfigVersion = 1;
constexpr const char* kPrefsNamespace = "can-grabber";
constexpr const char* kPrefsKey = "config";

Config s_config{};

void format_default_bus_name(uint8_t bus_id, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  snprintf(out, out_len, "can%u", static_cast<unsigned>(bus_id));
  out[out_len - 1] = '\0';
}

bool is_name_char_allowed(char c) {
  if (c >= 'a' && c <= 'z') return true;
  if (c >= 'A' && c <= 'Z') return true;
  if (c >= '0' && c <= '9') return true;
  if (c == '-' || c == '_') return true;
  return false;
}

void sanitize_name(const char* name, char* out, size_t out_len, uint8_t bus_id) {
  if (out_len == 0) {
    return;
  }

  if (name == nullptr || name[0] == '\0') {
    format_default_bus_name(bus_id, out, out_len);
    return;
  }

  size_t out_idx = 0;
  for (size_t i = 0; name[i] != '\0' && out_idx + 1 < out_len; ++i) {
    char c = name[i];
    if (is_name_char_allowed(c)) {
      out[out_idx++] = c;
    } else if (c == ' ') {
      out[out_idx++] = '_';
    }
  }
  out[out_idx] = '\0';

  if (out_idx == 0) {
    format_default_bus_name(bus_id, out, out_len);
  }
}

void apply_defaults(Config& cfg) {
  memset(&cfg, 0, sizeof(cfg));
  cfg.magic = kConfigMagic;
  cfg.version = kConfigVersion;

  cfg.global.max_file_size_bytes = 64UL * 1024UL * 1024UL;
  cfg.global.wifi_count = 0;

  for (uint8_t i = 0; i < kMaxBuses; ++i) {
    cfg.buses[i].enabled = (i == 0);
    cfg.buses[i].bitrate = 500UL * 1000UL;
    cfg.buses[i].read_only = false;
    cfg.buses[i].termination = false;
    cfg.buses[i].logging = true;
    format_default_bus_name(i, cfg.buses[i].name, sizeof(cfg.buses[i].name));
  }
}

bool load_from_nvs(Config& out_cfg) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return false;
  }

  Config temp{};
  size_t len = prefs.getBytes(kPrefsKey, &temp, sizeof(temp));
  prefs.end();

  if (len != sizeof(temp)) {
    return false;
  }
  if (temp.magic != kConfigMagic || temp.version != kConfigVersion) {
    return false;
  }

  out_cfg = temp;
  for (uint8_t i = 0; i < kMaxBuses; ++i) {
    out_cfg.buses[i].name[kBusNameMaxLen - 1] = '\0';
  }
  return true;
}

void save_to_nvs(const Config& cfg) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, false)) {
    return;
  }
  prefs.putBytes(kPrefsKey, &cfg, sizeof(cfg));
  prefs.end();
}

} // namespace

void init() {
  Config loaded{};
  if (load_from_nvs(loaded)) {
    s_config = loaded;
  } else {
    apply_defaults(s_config);
    save_to_nvs(s_config);
  }
}

void save() {
  save_to_nvs(s_config);
}

const Config& get() {
  return s_config;
}

Config& get_mutable() {
  return s_config;
}

void set_bus_name(uint8_t bus_id, const char* name) {
  if (bus_id >= kMaxBuses) {
    return;
  }
  sanitize_name(name, s_config.buses[bus_id].name, sizeof(s_config.buses[bus_id].name), bus_id);
}

void reset_defaults() {
  apply_defaults(s_config);
  save_to_nvs(s_config);
}

} // namespace config
