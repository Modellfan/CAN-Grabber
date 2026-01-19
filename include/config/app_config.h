#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stddef.h>
#include <stdint.h>

namespace config {

constexpr uint8_t kMaxBuses = 6;
constexpr size_t kBusNameMaxLen = 16;
constexpr size_t kWifiSsidMaxLen = 32;
constexpr size_t kWifiPassMaxLen = 64;
constexpr size_t kUrlMaxLen = 128;
constexpr size_t kTokenMaxLen = 64;
constexpr size_t kDbcNameMaxLen = 32;
constexpr size_t kApiTokenMaxLen = 64;

struct BusConfig {
  bool enabled;
  uint32_t bitrate;
  bool read_only;
  bool termination;
  bool logging;
  char name[kBusNameMaxLen];
};

struct WifiConfig {
  char ssid[kWifiSsidMaxLen];
  char password[kWifiPassMaxLen];
};

struct GlobalConfig {
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

struct Config {
  uint32_t magic;
  uint16_t version;
  uint16_t reserved;
  BusConfig buses[kMaxBuses];
  GlobalConfig global;
};

void init();
void save();

const Config& get();
Config& get_mutable();

void set_bus_name(uint8_t bus_id, const char* name);
void set_wifi(uint8_t index, const char* ssid, const char* password);
void set_wifi_count(uint8_t count);
void reset_defaults();

} // namespace config

#endif // APP_CONFIG_H
