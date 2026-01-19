#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <stdint.h>

namespace net {

void init();
void connect();
void disconnect();
void loop();

bool is_connected();
int8_t rssi_dbm();
uint8_t rssi_percent();

} // namespace net

#endif // NET_MANAGER_H
