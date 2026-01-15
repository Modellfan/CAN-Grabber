#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <stddef.h>
#include <stdint.h>

namespace logging {

struct Frame {
  uint32_t timestamp_sec;
  uint32_t timestamp_usec;
  uint8_t bus_id;
  uint32_t id;
  bool extended;
  uint8_t dlc;
  uint8_t data[8];
  bool is_tx;
};

void init();
void start();
void stop();
bool enqueue(const Frame& frame);

} // namespace logging

#endif // LOG_WRITER_H
