#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <stddef.h>
#include <stdint.h>

namespace net {

void init();
void connect();
void disconnect();
void loop();

bool is_connected();
int8_t rssi_dbm();
uint8_t rssi_percent();
uint8_t ap_clients();

struct WifiScanEntry {
  char ssid[33];
  int8_t rssi_dbm;
  uint8_t rssi_percent;
  uint8_t channel;
  bool secure;
};

size_t wifi_scan_count();
bool wifi_scan_entry(size_t index, WifiScanEntry* out);

} // namespace net

#endif // NET_MANAGER_H
