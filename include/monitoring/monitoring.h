#ifndef MONITORING_H
#define MONITORING_H

#include <stdint.h>

namespace monitoring {

struct Status {
  uint32_t uptime_sec;
  uint32_t mode;
  uint32_t error_flags;
};

void init();
Status get_status();

} // namespace monitoring

#endif // MONITORING_H
