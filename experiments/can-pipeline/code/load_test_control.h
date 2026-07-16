#ifndef LOAD_TEST_CONTROL_H
#define LOAD_TEST_CONTROL_H

#include <stddef.h>
#include <stdint.h>

namespace load_test {

void set_fps(uint32_t fps);
uint32_t get_fps();
uint32_t produced(uint8_t bus_id);
uint32_t consumed(uint8_t bus_id);
uint32_t queue_depth(uint8_t bus_id);
uint32_t queue_capacity();
uint8_t blocks_free(uint8_t bus_id);
uint8_t blocks_ready(uint8_t bus_id);
uint8_t blocks_in_use(uint8_t bus_id);

struct LogBlock {
  const uint8_t* data;
  size_t len;
  uint32_t frames;
  uint8_t index;
};

bool acquire_log_block(uint8_t bus_id, LogBlock* out);
void release_log_block(uint8_t bus_id, uint8_t index, uint32_t flushed_frames);

} // namespace load_test

#endif // LOAD_TEST_CONTROL_H
