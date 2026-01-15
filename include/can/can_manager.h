#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <stdint.h>

namespace can {

struct BusConfig {
  bool enabled;
  uint32_t bitrate;
  bool read_only;
  bool termination;
};

void init();
void deinit();

} // namespace can

#endif // CAN_MANAGER_H
