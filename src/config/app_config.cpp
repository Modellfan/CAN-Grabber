#include "config/app_config.h"

#include <Preferences.h>
#include <stdio.h>
#include <string.h>

namespace config {

namespace {

constexpr uint32_t kConfigMagic = 0x43414742; // "CAGB"
constexpr uint16_t kConfigVersion = 4;
constexpr const char* kPrefsNamespace = "can-grabber";
constexpr const char* kPrefsKey = "config";

Config s_config{};

struct GlobalConfigV1 {
  uint32_t max_file_size_bytes;
  uint8_t wifi_count;
  WifiConfig wifi[3];
  char upload_url[kUrlMaxLen];
  char influx_url[kUrlMaxLen];
  char influx_token[kTokenMaxLen];
  char dbc_name[kDbcNameMaxLen];
};

struct GlobalConfigV2 {
  uint32_t max_file_size_bytes;
  uint8_t wifi_count;
  WifiConfig wifi[3];
  char upload_url[kUrlMaxLen];
  char influx_url[kUrlMaxLen];
  char influx_token[kTokenMaxLen];
  char api_token[kApiTokenMaxLen];
  char dbc_name[kDbcNameMaxLen];
};

struct GlobalConfigV3 {
  uint32_t max_file_size_bytes;
  uint32_t low_space_threshold_bytes;
  uint8_t wifi_count;
  WifiConfig wifi[3];
  char upload_url[kUrlMaxLen];
  char influx_url[kUrlMaxLen];
  char influx_token[kTokenMaxLen];
  char api_token[kApiTokenMaxLen];
  char dbc_name[kDbcNameMaxLen];
};

struct BusConfigV3 {
  bool enabled;
  uint32_t bitrate;
  bool read_only;
  bool termination;
  bool logging;
  char name[kBusNameMaxLen];
};

struct ConfigV1 {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  BusConfigV3 buses[kMaxBuses];
  GlobalConfigV1 global;
};

struct ConfigV2 {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  BusConfigV3 buses[kMaxBuses];
  GlobalConfigV2 global;
};

struct ConfigV3 {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  BusConfigV3 buses[kMaxBuses];
  GlobalConfigV3 global;
};

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
  cfg.global.low_space_threshold_bytes = 32UL * 1024UL * 1024UL;
  cfg.global.wifi_count = 0;
  cfg.global.api_token[0] = '\0';

  for (uint8_t i = 0; i < kMaxBuses; ++i) {
    cfg.buses[i].enabled = (i == 0);
    cfg.buses[i].bitrate = 500UL * 1000UL;
    cfg.buses[i].read_only = false;
    cfg.buses[i].logging = true;
    format_default_bus_name(i, cfg.buses[i].name, sizeof(cfg.buses[i].name));
  }

  cfg.global.can_time_sync = false;
  cfg.global.manual_time_epoch = 0;
}

bool load_from_nvs(Config& out_cfg) {
  Preferences prefs;
  if (!prefs.begin(kPrefsNamespace, true)) {
    return false;
  }

  const size_t len = prefs.getBytesLength(kPrefsKey);
  if (len == 0) {
    prefs.end();
    return false;
  }

  bool loaded = false;
  if (len == sizeof(Config)) {
    Config temp{};
    if (prefs.getBytes(kPrefsKey, &temp, sizeof(temp)) == sizeof(temp) &&
        temp.magic == kConfigMagic && temp.version == kConfigVersion) {
      out_cfg = temp;
      loaded = true;
    }
  } else if (len == sizeof(ConfigV3)) {
    ConfigV3 temp{};
    if (prefs.getBytes(kPrefsKey, &temp, sizeof(temp)) == sizeof(temp) &&
        temp.magic == kConfigMagic && temp.version == 3) {
      apply_defaults(out_cfg);
      out_cfg.magic = kConfigMagic;
      out_cfg.version = kConfigVersion;
      for (uint8_t i = 0; i < kMaxBuses; ++i) {
        out_cfg.buses[i].enabled = temp.buses[i].enabled;
        out_cfg.buses[i].bitrate = temp.buses[i].bitrate;
        out_cfg.buses[i].read_only = temp.buses[i].read_only;
        out_cfg.buses[i].logging = temp.buses[i].logging;
        strncpy(out_cfg.buses[i].name, temp.buses[i].name, sizeof(out_cfg.buses[i].name));
        out_cfg.buses[i].name[sizeof(out_cfg.buses[i].name) - 1] = '\0';
      }
      out_cfg.global.max_file_size_bytes = temp.global.max_file_size_bytes;
      out_cfg.global.low_space_threshold_bytes = temp.global.low_space_threshold_bytes;
      out_cfg.global.wifi_count = temp.global.wifi_count;
      for (uint8_t i = 0; i < 3; ++i) {
        out_cfg.global.wifi[i] = temp.global.wifi[i];
      }
      strncpy(out_cfg.global.upload_url, temp.global.upload_url, sizeof(out_cfg.global.upload_url));
      strncpy(out_cfg.global.influx_url, temp.global.influx_url, sizeof(out_cfg.global.influx_url));
      strncpy(out_cfg.global.influx_token,
              temp.global.influx_token,
              sizeof(out_cfg.global.influx_token));
      strncpy(out_cfg.global.api_token, temp.global.api_token, sizeof(out_cfg.global.api_token));
      strncpy(out_cfg.global.dbc_name, temp.global.dbc_name, sizeof(out_cfg.global.dbc_name));
      loaded = true;
    }
  } else if (len == sizeof(ConfigV2)) {
    ConfigV2 temp{};
    if (prefs.getBytes(kPrefsKey, &temp, sizeof(temp)) == sizeof(temp) &&
        temp.magic == kConfigMagic && temp.version == 2) {
      apply_defaults(out_cfg);
      out_cfg.magic = kConfigMagic;
      out_cfg.version = kConfigVersion;
      for (uint8_t i = 0; i < kMaxBuses; ++i) {
        out_cfg.buses[i].enabled = temp.buses[i].enabled;
        out_cfg.buses[i].bitrate = temp.buses[i].bitrate;
        out_cfg.buses[i].read_only = temp.buses[i].read_only;
        out_cfg.buses[i].logging = temp.buses[i].logging;
        strncpy(out_cfg.buses[i].name, temp.buses[i].name, sizeof(out_cfg.buses[i].name));
        out_cfg.buses[i].name[sizeof(out_cfg.buses[i].name) - 1] = '\0';
      }
      out_cfg.global.max_file_size_bytes = temp.global.max_file_size_bytes;
      out_cfg.global.wifi_count = temp.global.wifi_count;
      for (uint8_t i = 0; i < 3; ++i) {
        out_cfg.global.wifi[i] = temp.global.wifi[i];
      }
      strncpy(out_cfg.global.upload_url, temp.global.upload_url, sizeof(out_cfg.global.upload_url));
      strncpy(out_cfg.global.influx_url, temp.global.influx_url, sizeof(out_cfg.global.influx_url));
      strncpy(out_cfg.global.influx_token,
              temp.global.influx_token,
              sizeof(out_cfg.global.influx_token));
      strncpy(out_cfg.global.api_token, temp.global.api_token, sizeof(out_cfg.global.api_token));
      strncpy(out_cfg.global.dbc_name, temp.global.dbc_name, sizeof(out_cfg.global.dbc_name));
      loaded = true;
    }
  } else if (len == sizeof(ConfigV1)) {
    ConfigV1 temp{};
    if (prefs.getBytes(kPrefsKey, &temp, sizeof(temp)) == sizeof(temp) &&
        temp.magic == kConfigMagic && temp.version == 1) {
      apply_defaults(out_cfg);
      out_cfg.magic = kConfigMagic;
      out_cfg.version = kConfigVersion;
      for (uint8_t i = 0; i < kMaxBuses; ++i) {
        out_cfg.buses[i].enabled = temp.buses[i].enabled;
        out_cfg.buses[i].bitrate = temp.buses[i].bitrate;
        out_cfg.buses[i].read_only = temp.buses[i].read_only;
        out_cfg.buses[i].logging = temp.buses[i].logging;
        strncpy(out_cfg.buses[i].name, temp.buses[i].name, sizeof(out_cfg.buses[i].name));
        out_cfg.buses[i].name[sizeof(out_cfg.buses[i].name) - 1] = '\0';
      }
      out_cfg.global.max_file_size_bytes = temp.global.max_file_size_bytes;
      out_cfg.global.wifi_count = temp.global.wifi_count;
      for (uint8_t i = 0; i < 3; ++i) {
        out_cfg.global.wifi[i] = temp.global.wifi[i];
      }
      strncpy(out_cfg.global.upload_url, temp.global.upload_url, sizeof(out_cfg.global.upload_url));
      strncpy(out_cfg.global.influx_url, temp.global.influx_url, sizeof(out_cfg.global.influx_url));
      strncpy(out_cfg.global.influx_token, temp.global.influx_token, sizeof(out_cfg.global.influx_token));
      strncpy(out_cfg.global.dbc_name, temp.global.dbc_name, sizeof(out_cfg.global.dbc_name));
      out_cfg.global.api_token[0] = '\0';
      loaded = true;
    }
  }

  prefs.end();
  if (!loaded) {
    return false;
  }

  for (uint8_t i = 0; i < kMaxBuses; ++i) {
    out_cfg.buses[i].name[kBusNameMaxLen - 1] = '\0';
  }
  out_cfg.global.upload_url[kUrlMaxLen - 1] = '\0';
  out_cfg.global.influx_url[kUrlMaxLen - 1] = '\0';
  out_cfg.global.influx_token[kTokenMaxLen - 1] = '\0';
  out_cfg.global.api_token[kApiTokenMaxLen - 1] = '\0';
  out_cfg.global.dbc_name[kDbcNameMaxLen - 1] = '\0';
  for (uint8_t i = 0; i < 3; ++i) {
    out_cfg.global.wifi[i].ssid[kWifiSsidMaxLen - 1] = '\0';
    out_cfg.global.wifi[i].password[kWifiPassMaxLen - 1] = '\0';
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

void set_wifi(uint8_t index, const char* ssid, const char* password) {
  if (index >= 3) {
    return;
  }

  WifiConfig& wifi = s_config.global.wifi[index];
  if (ssid == nullptr) {
    wifi.ssid[0] = '\0';
  } else {
    strncpy(wifi.ssid, ssid, sizeof(wifi.ssid));
    wifi.ssid[sizeof(wifi.ssid) - 1] = '\0';
  }

  if (password == nullptr) {
    wifi.password[0] = '\0';
  } else {
    strncpy(wifi.password, password, sizeof(wifi.password));
    wifi.password[sizeof(wifi.password) - 1] = '\0';
  }

  if (wifi.ssid[0] != '\0' && s_config.global.wifi_count <= index) {
    s_config.global.wifi_count = static_cast<uint8_t>(index + 1);
  }
}

void set_wifi_count(uint8_t count) {
  s_config.global.wifi_count = (count > 3) ? 3 : count;
}

void reset_defaults() {
  apply_defaults(s_config);
  save_to_nvs(s_config);
}

} // namespace config
