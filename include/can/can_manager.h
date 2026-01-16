#ifndef CAN_MANAGER_H
#define CAN_MANAGER_H

#include <stddef.h>
#include <stdint.h>

#include <ACAN2515.h>

namespace can {

struct BusConfig {
  bool enabled;
  uint32_t bitrate;
  bool read_only;
  bool termination;
};

struct Frame {
  uint64_t timestamp_us;
  uint8_t bus_id;
  CANMessage message;
};

void init();
void deinit();
bool pop_rx_frame(uint8_t bus_id, Frame& frame);
uint32_t drop_count(uint8_t bus_id);
uint32_t high_water(uint8_t bus_id);

#ifndef RX_LOAD_TEST
struct LogBlock {
  const uint8_t* data;
  size_t len;
  uint32_t frames;
  uint8_t index;
};

bool acquire_log_block(uint8_t bus_id, LogBlock* out);
void release_log_block(uint8_t bus_id, uint8_t index, uint32_t flushed_frames);
#endif

#ifdef RX_LOAD_TEST
void set_load_test_fps(uint32_t fps);
uint32_t load_test_fps();
uint32_t load_test_produced(uint8_t bus_id);
#endif

} // namespace can

#endif // CAN_MANAGER_H
