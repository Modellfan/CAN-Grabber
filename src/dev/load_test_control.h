#ifndef LOAD_TEST_CONTROL_H
#define LOAD_TEST_CONTROL_H

#include <stdint.h>

namespace load_test {

void set_fps(uint32_t fps);
uint32_t get_fps();
uint32_t produced(uint8_t bus_id);
uint32_t consumed(uint8_t bus_id);
uint32_t queue_depth(uint8_t bus_id);
uint32_t queue_capacity();

} // namespace load_test

#endif // LOAD_TEST_CONTROL_H
