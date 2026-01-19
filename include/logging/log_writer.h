#ifndef LOG_WRITER_H
#define LOG_WRITER_H

#include <stddef.h>
#include <stdint.h>

namespace logging {

struct Stats {
  uint64_t total_bytes;
  uint32_t bytes_per_sec;
  uint32_t loop_count;
  uint32_t pop_count;
  uint32_t last_write_ms;
  uint32_t open_failures;
  uint8_t active_buses;
  uint32_t write_calls;
  uint32_t write_failures;
  uint32_t last_write_len;
  uint32_t prealloc_attempts;
  uint32_t prealloc_failures;
  uint32_t reopen_attempts;
  uint32_t reopen_failures;
  bool started;
};

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
void close_file(uint8_t bus_id);
void rotate_files();
bool enqueue(const Frame& frame);
Stats get_stats();

} // namespace logging

#endif // LOG_WRITER_H
