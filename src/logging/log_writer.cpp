#include "logging/log_writer.h"

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "storage/storage_manager.h"
#ifdef RX_LOAD_TEST
#include "dev/load_test_control.h"
#endif

namespace logging {

namespace {

struct BusLogState {
  File file;
  bool active;
  enum class LogState : uint8_t {
    kIdle,
    kOpening,
    kActive,
    kClosing,
    kError,
  };
  LogState state;
  char path[64];
  uint64_t bytes_written;
  uint32_t start_ms;
  uint32_t checksum;
  size_t buffered_len;
  static constexpr size_t kBufferSize = 2048;
  uint8_t buffer[kBufferSize];
};

BusLogState s_bus_logs[config::kMaxBuses];
bool s_started = false;
TaskHandle_t s_log_task = nullptr;
constexpr UBaseType_t kLogTaskPriority = configMAX_PRIORITIES - 1;
constexpr BaseType_t kLogTaskCore = 0;
portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
uint64_t s_total_bytes = 0;
uint32_t s_bytes_per_sec = 0;
uint32_t s_window_bytes = 0;
uint32_t s_window_start_ms = 0;
uint32_t s_loop_count = 0;
uint32_t s_pop_count = 0;
uint32_t s_last_write_ms = 0;
uint32_t s_open_failures = 0;
uint8_t s_active_buses = 0;
uint32_t s_write_calls = 0;
uint32_t s_write_failures = 0;
uint32_t s_last_write_len = 0;
uint32_t s_prealloc_attempts = 0;
uint32_t s_prealloc_failures = 0;
uint32_t s_reopen_attempts = 0;
uint32_t s_reopen_failures = 0;

uint32_t crc32_update(uint32_t crc, const uint8_t* data, size_t len) {
  uint32_t value = crc;
  for (size_t i = 0; i < len; ++i) {
    value ^= static_cast<uint32_t>(data[i]);
    for (uint8_t bit = 0; bit < 8; ++bit) {
      if (value & 1u) {
        value = (value >> 1) ^ 0xEDB88320u;
      } else {
        value >>= 1;
      }
    }
  }
  return value;
}

// Format a CAN frame as a SavvyCAN ASCII log line.
size_t format_savvy_line(const can::Frame& frame, char* out, size_t out_cap) {
  if (out_cap == 0) {
    return 0;
  }

  const uint32_t sec = static_cast<uint32_t>(frame.timestamp_us / 1000000ULL);
  const uint32_t usec = static_cast<uint32_t>(frame.timestamp_us % 1000000ULL);
  char* cursor = out;
  const auto left = [&]() -> size_t {
    return out_cap - static_cast<size_t>(cursor - out);
  };

  cursor += snprintf(cursor, left(), "%lu.%06lu ",
                     static_cast<unsigned long>(sec),
                     static_cast<unsigned long>(usec));
  cursor += snprintf(cursor, left(), "%uR%s ",
                     static_cast<unsigned>(frame.bus_id + 1),
                     frame.message.ext ? "29" : "11");

  const uint32_t id_masked =
      frame.message.ext ? (frame.message.id & 0x1FFFFFFF) : (frame.message.id & 0x7FFu);
  cursor += snprintf(cursor, left(), "%08X ", static_cast<unsigned>(id_masked));

  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t byte_val = (i < frame.message.len) ? frame.message.data[i] : 0u;
    cursor += snprintf(cursor,
                       left(),
                       (i == 7) ? "%02X" : "%02X ",
                       static_cast<unsigned>(byte_val));
  }

  if (left() > 0) {
    *cursor++ = '\n';
  }
  return static_cast<size_t>(cursor - out);
}

// Build the log file path for a bus and start timestamp.
void build_log_path(uint8_t bus_id,
                    const char* bus_name,
                    uint32_t start_ms,
                    char* out,
                    size_t out_len) {
  if (out_len == 0) {
    return;
  }
  const char* name = (bus_name != nullptr && bus_name[0] != '\0') ? bus_name : "bus";
  snprintf(out,
           out_len,
           "/log_%lu_bus%u_%s.sav",
           static_cast<unsigned long>(start_ms),
           static_cast<unsigned>(bus_id + 1),
           name);
  out[out_len - 1] = '\0';
}

// Write the SavvyCAN header into a newly opened log file.
size_t write_header(File& file, uint8_t bus_id, uint32_t* checksum) {
  char header[64];
  const int len = snprintf(header,
                           sizeof(header),
                           "# SavvyCAN ASCII log - bus %u\n",
                           static_cast<unsigned>(bus_id + 1));
  if (len <= 0) {
    return 0;
  }
  const size_t out =
      file.write(reinterpret_cast<const uint8_t*>(header), static_cast<size_t>(len));
  if (checksum != nullptr && out > 0) {
    *checksum = crc32_update(*checksum, reinterpret_cast<const uint8_t*>(header), out);
  }
  return out;
}

// Update shared statistics after writing bytes to storage.
void note_bytes_written(size_t len) {
  portENTER_CRITICAL(&s_stats_mux);
  s_total_bytes += len;
  s_window_bytes += static_cast<uint32_t>(len);
  s_last_write_ms = millis();
  s_last_write_len = static_cast<uint32_t>(len);
  portEXIT_CRITICAL(&s_stats_mux);
}

// Write raw bytes to the active log file and update counters.
size_t write_bytes(BusLogState& state, const uint8_t* data, size_t len) {
  if (!state.active || len == 0) {
    return 0;
  }
  portENTER_CRITICAL(&s_stats_mux);
  s_write_calls++;
  portEXIT_CRITICAL(&s_stats_mux);
  const size_t out = state.file.write(data, len);
  if (out > 0) {
    state.bytes_written += out;
    state.checksum = crc32_update(state.checksum, data, out);
    note_bytes_written(out);
  }
  if (out != len) {
    portENTER_CRITICAL(&s_stats_mux);
    s_write_failures++;
    portEXIT_CRITICAL(&s_stats_mux);
  }
  return out;
}

// Reopen a log file and seek to the last known write position.
bool reopen_log_file(BusLogState& state) {
  if (!state.path[0]) {
    return false;
  }
  state.state = BusLogState::LogState::kOpening;
  portENTER_CRITICAL(&s_stats_mux);
  s_reopen_attempts++;
  portEXIT_CRITICAL(&s_stats_mux);

  if (state.file) {
    state.file.flush();
    state.file.close();
  }

  File file = SD.open(state.path, FILE_WRITE);
  if (!file) {
    portENTER_CRITICAL(&s_stats_mux);
    s_reopen_failures++;
    portEXIT_CRITICAL(&s_stats_mux);
    state.active = false;
    state.state = BusLogState::LogState::kError;
    return false;
  }

  if (state.bytes_written > 0) {
    file.seek(state.bytes_written);
  }
  state.file = file;
  state.active = true;
  state.state = BusLogState::LogState::kActive;
  return true;
}

// Flush any buffered bytes to the file.
void flush_buffer(BusLogState& state) {
  if (state.buffered_len == 0) {
    return;
  }
  write_bytes(state, state.buffer, state.buffered_len);
  state.buffered_len = 0;
}

// Open a new log file for the given bus, preallocating if configured.
bool open_log_file(uint8_t bus_id) {
  const config::Config& cfg = config::get();
  const uint32_t max_size = cfg.global.max_file_size_bytes;
  BusLogState& state = s_bus_logs[bus_id];
  state.state = BusLogState::LogState::kOpening;
  if (max_size > 0 && !storage::ensure_space(max_size)) {
    state.state = BusLogState::LogState::kError;
    return false;
  }

  const uint32_t start_ms = millis();
  char path[64];
  const char* bus_name = cfg.buses[bus_id].name;
  build_log_path(bus_id, bus_name, start_ms, path, sizeof(path));

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    state.state = BusLogState::LogState::kError;
    return false;
  }

  if (max_size > 0) {
    portENTER_CRITICAL(&s_stats_mux);
    s_prealloc_attempts++;
    portEXIT_CRITICAL(&s_stats_mux);
    if (!file.seek(max_size - 1) || file.write(static_cast<uint8_t>(0)) != 1) {
      portENTER_CRITICAL(&s_stats_mux);
      s_prealloc_failures++;
      portEXIT_CRITICAL(&s_stats_mux);
    }
    file.flush();
    file.seek(0);
  }

  state.file = file;
  state.active = true;
  state.state = BusLogState::LogState::kActive;
  state.bytes_written = 0;
  state.start_ms = start_ms;
  state.checksum = 0xFFFFFFFFu;
  state.buffered_len = 0;
  strncpy(state.path, path, sizeof(state.path));
  state.path[sizeof(state.path) - 1] = '\0';

  const size_t header_bytes = write_header(state.file, bus_id, &state.checksum);
  state.bytes_written += header_bytes;
  note_bytes_written(header_bytes);
  storage::register_log_file(state.path, static_cast<uint8_t>(bus_id + 1), start_ms);
  return true;
}

// Flush and close an active log file, then finalize metadata.
void close_log_file(uint8_t bus_id) {
  BusLogState& state = s_bus_logs[bus_id];
  if (!state.active) {
    state.state = BusLogState::LogState::kIdle;
    return;
  }

  state.state = BusLogState::LogState::kClosing;
  if (state.file) {
    flush_buffer(state);
    state.file.flush();
    state.file.close();
  }

  const uint32_t end_ms = millis();
  const uint32_t checksum = state.checksum ^ 0xFFFFFFFFu;
  storage::finalize_log_file(state.path, state.bytes_written, end_ms, checksum);
  state.active = false;
  state.state = BusLogState::LogState::kIdle;
  state.path[0] = '\0';
  state.bytes_written = 0;
  state.start_ms = 0;
  state.checksum = 0;
}

// Rotate the log file if the next write would exceed max size.
bool rotate_if_needed(uint8_t bus_id, size_t next_len) {
  const uint32_t max_size = config::get().global.max_file_size_bytes;
  if (max_size == 0) {
    return true;
  }

  BusLogState& state = s_bus_logs[bus_id];
  if (!state.active || state.state != BusLogState::LogState::kActive) {
    return false;
  }

  if (state.bytes_written + state.buffered_len + next_len <= max_size) {
    return true;
  }

  close_log_file(bus_id);
  return open_log_file(bus_id);
}

// Single log writer task; lower priority than RX, pinned to core 1.
// Log writer task that drains per-bus blocks and writes them to SD.
void log_task(void*) {
  for (;;) {
    if (!s_started) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool any = false;
    const uint32_t now_ms = millis();
    portENTER_CRITICAL(&s_stats_mux);
    s_loop_count++;
    portEXIT_CRITICAL(&s_stats_mux);
    for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
      if (!s_bus_logs[bus_id].active) {
        continue;
      }

#ifdef RX_LOAD_TEST
      load_test::LogBlock block{};
      while (load_test::acquire_log_block(bus_id, &block)) {
        portENTER_CRITICAL(&s_stats_mux);
        s_pop_count += block.frames;
        portEXIT_CRITICAL(&s_stats_mux);

        if (!rotate_if_needed(bus_id, block.len)) {
          load_test::release_log_block(bus_id, block.index, 0);
          continue;
        }

        BusLogState& state = s_bus_logs[bus_id];
        if (!state.active) {
          load_test::release_log_block(bus_id, block.index, 0);
          continue;
        }

        flush_buffer(state);
        size_t out = write_bytes(state, block.data, block.len);
        if (out != block.len) {
          if (reopen_log_file(state)) {
            out = write_bytes(state, block.data, block.len);
          }
        }
        const uint32_t flushed_frames = (out == block.len) ? block.frames : 0;
        load_test::release_log_block(bus_id, block.index, flushed_frames);
        any = true;
      }
#else
      can::LogBlock block{};
      while (can::acquire_log_block(bus_id, &block)) {
        portENTER_CRITICAL(&s_stats_mux);
        s_pop_count += block.frames;
        portEXIT_CRITICAL(&s_stats_mux);

        if (!rotate_if_needed(bus_id, block.len)) {
          can::release_log_block(bus_id, block.index, 0);
          continue;
        }

        BusLogState& state = s_bus_logs[bus_id];
        if (!state.active) {
          can::release_log_block(bus_id, block.index, 0);
          continue;
        }

        flush_buffer(state);
        size_t out = write_bytes(state, block.data, block.len);
        if (out != block.len) {
          if (reopen_log_file(state)) {
            out = write_bytes(state, block.data, block.len);
          }
        }
        const uint32_t flushed_frames = (out == block.len) ? block.frames : 0;
        can::release_log_block(bus_id, block.index, flushed_frames);
        any = true;
      }
#endif
    }

    if (s_window_start_ms == 0) {
      s_window_start_ms = now_ms;
    } else {
      const uint32_t elapsed = now_ms - s_window_start_ms;
      if (elapsed >= 1000) {
        portENTER_CRITICAL(&s_stats_mux);
        s_bytes_per_sec = static_cast<uint32_t>((s_window_bytes * 1000ULL) / elapsed);
        s_window_bytes = 0;
        s_window_start_ms = now_ms;
        portEXIT_CRITICAL(&s_stats_mux);
      }
    }

    // Always yield a tick so lower-priority tasks (e.g., Arduino loop/Serial) can run.
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

} // namespace

// Initialize log writer state and stats counters.
void init() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    s_bus_logs[i].active = false;
    s_bus_logs[i].state = BusLogState::LogState::kIdle;
    s_bus_logs[i].path[0] = '\0';
    s_bus_logs[i].bytes_written = 0;
    s_bus_logs[i].start_ms = 0;
    s_bus_logs[i].checksum = 0;
  }
  s_started = false;
  portENTER_CRITICAL(&s_stats_mux);
  s_total_bytes = 0;
  s_bytes_per_sec = 0;
  s_window_bytes = 0;
  s_window_start_ms = 0;
  s_loop_count = 0;
  s_pop_count = 0;
  s_last_write_ms = 0;
  s_open_failures = 0;
  s_active_buses = 0;
  s_write_calls = 0;
  s_write_failures = 0;
  s_last_write_len = 0;
  s_prealloc_attempts = 0;
  s_prealloc_failures = 0;
  s_reopen_attempts = 0;
  s_reopen_failures = 0;
  portEXIT_CRITICAL(&s_stats_mux);
}

// Start logging for enabled buses and spawn the writer task.
void start() {
  if (!storage::is_ready()) {
    return;
  }

  const config::Config& cfg = config::get();
  portENTER_CRITICAL(&s_stats_mux);
  s_open_failures = 0;
  s_active_buses = 0;
  portEXIT_CRITICAL(&s_stats_mux);
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    close_log_file(i);
    if (!cfg.buses[i].enabled || !cfg.buses[i].logging) {
      continue;
    }

    if (open_log_file(i)) {
      portENTER_CRITICAL(&s_stats_mux);
      s_active_buses++;
      portEXIT_CRITICAL(&s_stats_mux);
    } else {
      portENTER_CRITICAL(&s_stats_mux);
      s_open_failures++;
      portEXIT_CRITICAL(&s_stats_mux);
    }
  }

  s_started = true;
  if (s_log_task == nullptr) {
    // Log writer runs on core 1 at lower priority than RX tasks.
    xTaskCreatePinnedToCore(log_task,
                            "log_writer",
                            4096,
                            nullptr,
                            kLogTaskPriority,
                            &s_log_task,
                            kLogTaskCore);
  }
}

// Stop logging and tear down the writer task.
void stop() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    close_log_file(static_cast<uint8_t>(i));
  }
  if (s_log_task != nullptr) {
    vTaskDelete(s_log_task);
    s_log_task = nullptr;
  }
  s_started = false;
}

// Close a specific bus log file without stopping the logging task.
void close_file(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return;
  }
  close_log_file(bus_id);
}

// Close current files and open fresh ones for active buses.
void rotate_files() {
  if (!s_started) {
    return;
  }

  const config::Config& cfg = config::get();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    close_log_file(i);
    if (!cfg.buses[i].enabled || !cfg.buses[i].logging) {
      continue;
    }
    open_log_file(i);
  }
}

// Placeholder for per-frame enqueue; logging is block-based now.
bool enqueue(const Frame& frame) {
  (void)frame;
  return s_started;
}

// Snapshot current log writer statistics.
Stats get_stats() {
  Stats stats{};
  portENTER_CRITICAL(&s_stats_mux);
  stats.total_bytes = s_total_bytes;
  stats.bytes_per_sec = s_bytes_per_sec;
  stats.loop_count = s_loop_count;
  stats.pop_count = s_pop_count;
  stats.last_write_ms = s_last_write_ms;
  stats.open_failures = s_open_failures;
  stats.active_buses = s_active_buses;
  stats.write_calls = s_write_calls;
  stats.write_failures = s_write_failures;
  stats.last_write_len = s_last_write_len;
  stats.prealloc_attempts = s_prealloc_attempts;
  stats.prealloc_failures = s_prealloc_failures;
  stats.reopen_attempts = s_reopen_attempts;
  stats.reopen_failures = s_reopen_failures;
  stats.started = s_started;
  portEXIT_CRITICAL(&s_stats_mux);
  return stats;
}

} // namespace logging
