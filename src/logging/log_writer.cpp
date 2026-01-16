#include "logging/log_writer.h"

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "storage/storage_manager.h"

namespace logging {

namespace {

struct BusLogState {
  File file;
  bool active;
  char path[64];
  uint64_t bytes_written;
  uint32_t start_ms;
  size_t buffered_len;
  static constexpr size_t kBufferSize = 4096;
  uint8_t buffer[kBufferSize];
};

BusLogState s_bus_logs[config::kMaxBuses];
bool s_started = false;
TaskHandle_t s_log_task = nullptr;
constexpr UBaseType_t kLogTaskPriority = configMAX_PRIORITIES - 2;
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

void build_log_path(uint8_t bus_id, uint32_t start_ms, char* out, size_t out_len) {
  if (out_len == 0) {
    return;
  }
  snprintf(out,
           out_len,
           "/log_%lu_bus%u.sav",
           static_cast<unsigned long>(start_ms),
           static_cast<unsigned>(bus_id + 1));
  out[out_len - 1] = '\0';
}

size_t write_header(File& file, uint8_t bus_id) {
  size_t written = 0;
  written += file.print("# SavvyCAN ASCII log - bus ");
  written += file.println(static_cast<unsigned>(bus_id + 1));
  return written;
}

void note_bytes_written(size_t len) {
  portENTER_CRITICAL(&s_stats_mux);
  s_total_bytes += len;
  s_window_bytes += static_cast<uint32_t>(len);
  s_last_write_ms = millis();
  portEXIT_CRITICAL(&s_stats_mux);
}

void write_bytes(BusLogState& state, const uint8_t* data, size_t len) {
  if (!state.active || len == 0) {
    return;
  }
  const size_t out = state.file.write(data, len);
  if (out > 0) {
    state.bytes_written += out;
    note_bytes_written(out);
  }
}

void flush_buffer(BusLogState& state) {
  if (state.buffered_len == 0) {
    return;
  }
  write_bytes(state, state.buffer, state.buffered_len);
  state.buffered_len = 0;
}

bool open_log_file(uint8_t bus_id) {
  const config::Config& cfg = config::get();
  const uint32_t max_size = cfg.global.max_file_size_bytes;
  if (max_size > 0 && !storage::ensure_space(max_size)) {
    return false;
  }

  const uint32_t start_ms = millis();
  char path[64];
  build_log_path(bus_id, start_ms, path, sizeof(path));

  File file = SD.open(path, FILE_WRITE);
  if (!file) {
    return false;
  }

  BusLogState& state = s_bus_logs[bus_id];
  state.file = file;
  state.active = true;
  state.bytes_written = 0;
  state.start_ms = start_ms;
  state.buffered_len = 0;
  strncpy(state.path, path, sizeof(state.path));
  state.path[sizeof(state.path) - 1] = '\0';

  const size_t header_bytes = write_header(state.file, bus_id);
  state.bytes_written += header_bytes;
  note_bytes_written(header_bytes);
  storage::register_log_file(state.path, static_cast<uint8_t>(bus_id + 1), start_ms);
  return true;
}

void close_log_file(uint8_t bus_id) {
  BusLogState& state = s_bus_logs[bus_id];
  if (!state.active) {
    return;
  }

  if (state.file) {
    flush_buffer(state);
    state.file.flush();
    state.file.close();
  }

  storage::finalize_log_file(state.path, state.bytes_written);
  state.active = false;
  state.path[0] = '\0';
  state.bytes_written = 0;
  state.start_ms = 0;
}

bool rotate_if_needed(uint8_t bus_id, size_t next_len) {
  const uint32_t max_size = config::get().global.max_file_size_bytes;
  if (max_size == 0) {
    return true;
  }

  BusLogState& state = s_bus_logs[bus_id];
  if (!state.active) {
    return false;
  }

  if (state.bytes_written + state.buffered_len + next_len <= max_size) {
    return true;
  }

  close_log_file(bus_id);
  return open_log_file(bus_id);
}

// Single log writer task; lower priority than RX, pinned to core 1.
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

      can::Frame frame{};
      while (can::pop_rx_frame(bus_id, frame)) {
        portENTER_CRITICAL(&s_stats_mux);
        s_pop_count++;
        portEXIT_CRITICAL(&s_stats_mux);
        char line[96];
        const size_t len = format_savvy_line(frame, line, sizeof(line));
        if (len == 0) {
          continue;
        }

        if (!rotate_if_needed(bus_id, len)) {
          break;
        }

        BusLogState& state = s_bus_logs[bus_id];
        if (!state.active) {
          continue;
        }

        if (len > BusLogState::kBufferSize) {
          flush_buffer(state);
          write_bytes(state, reinterpret_cast<const uint8_t*>(line), len);
        } else {
          if (state.buffered_len + len > BusLogState::kBufferSize) {
            flush_buffer(state);
          }
          memcpy(state.buffer + state.buffered_len, line, len);
          state.buffered_len += len;
        }
        any = true;
      }
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

void init() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    s_bus_logs[i].active = false;
    s_bus_logs[i].path[0] = '\0';
    s_bus_logs[i].bytes_written = 0;
    s_bus_logs[i].start_ms = 0;
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
  portEXIT_CRITICAL(&s_stats_mux);
}

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

bool enqueue(const Frame& frame) {
  (void)frame;
  return s_started;
}

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
  stats.started = s_started;
  portEXIT_CRITICAL(&s_stats_mux);
  return stats;
}

} // namespace logging
