#ifdef RX_LOAD_TEST

#include "can/can_manager.h"

#include <stdio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <string.h>

#include "config/app_config.h"
#include "dev/load_test_control.h"

namespace {

constexpr size_t kBlockSize = 8192;
constexpr uint8_t kBlockCount = 2;
constexpr UBaseType_t kRxTaskPriority = configMAX_PRIORITIES - 2;
constexpr BaseType_t kRxTaskCore = 0;
constexpr uint32_t kLoadTestYieldEvery = 500;
constexpr uint64_t kIdleYieldIntervalUs = 5000;

struct LogBlockState {
  uint8_t data[kBlockSize];
  size_t len = 0;
  uint32_t frames = 0;
  uint8_t state = 0; // 0=free, 1=ready, 2=logger_in_use, 3=rx_active
};

struct BusState {
  LogBlockState blocks[kBlockCount];
  uint8_t write_index = 0;
  uint32_t drops = 0;
  uint32_t high_water = 0; // bytes pending
};

BusState s_buses[config::kMaxBuses];
portMUX_TYPE s_ring_mux[config::kMaxBuses];
TaskHandle_t s_rx_tasks[config::kMaxBuses] = {};
char s_rx_task_names[config::kMaxBuses][12] = {};

volatile uint32_t s_target_fps = 0;
uint32_t s_produced[config::kMaxBuses] = {};
uint32_t s_consumed[config::kMaxBuses] = {};
bool s_initialized = false;

// Reset per-bus block state, counters, and mutexes for the load test.
void init_bus_state() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    s_buses[i].drops = 0;
    s_buses[i].high_water = 0;
    s_buses[i].write_index = 0;
    for (uint8_t b = 0; b < kBlockCount; ++b) {
      s_buses[i].blocks[b].len = 0;
      s_buses[i].blocks[b].frames = 0;
      s_buses[i].blocks[b].state = 0;
    }
    s_consumed[i] = 0;
    s_ring_mux[i] = portMUX_INITIALIZER_UNLOCKED;
    snprintf(s_rx_task_names[i],
             sizeof(s_rx_task_names[i]),
             "rx_load%u",
             static_cast<unsigned>(i));
  }
}

// Format a synthetic CAN frame into a SavvyCAN ASCII line.
size_t format_savvy_line(uint8_t bus_id,
                         uint64_t timestamp_us,
                         const CANMessage& msg,
                         char* out,
                         size_t out_cap) {
  if (out_cap == 0) {
    return 0;
  }

  const uint32_t sec = static_cast<uint32_t>(timestamp_us / 1000000ULL);
  const uint32_t usec = static_cast<uint32_t>(timestamp_us % 1000000ULL);
  char* cursor = out;
  const auto left = [&]() -> size_t {
    return out_cap - static_cast<size_t>(cursor - out);
  };

  cursor += snprintf(cursor, left(), "%lu.%06lu ",
                     static_cast<unsigned long>(sec),
                     static_cast<unsigned long>(usec));
  cursor += snprintf(cursor, left(), "%uR%s ",
                     static_cast<unsigned>(bus_id + 1),
                     msg.ext ? "29" : "11");

  const uint32_t id_masked =
      msg.ext ? (msg.id & 0x1FFFFFFF) : (msg.id & 0x7FFu);
  cursor += snprintf(cursor, left(), "%08X ", static_cast<unsigned>(id_masked));

  for (uint8_t i = 0; i < 8; ++i) {
    const uint8_t byte_val = (i < msg.len) ? msg.data[i] : 0u;
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

// Per-bus task that generates synthetic frames at the target rate.
void load_task(void* param) {
  const uint8_t bus_id = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(param));
  uint64_t next_tick = esp_timer_get_time();
  uint64_t last_idle_yield = next_tick;
  uint32_t yield_count = 0;

  for (;;) {
    if (!s_initialized) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    uint32_t target_fps = 0;
    portENTER_CRITICAL(&s_ring_mux[bus_id]);
    target_fps = s_target_fps;
    portEXIT_CRITICAL(&s_ring_mux[bus_id]);

    if (target_fps == 0) {
      vTaskDelay(pdMS_TO_TICKS(10));
      next_tick = esp_timer_get_time();
      continue;
    }

    const uint32_t interval_us = 1000000UL / target_fps;
    const uint64_t now = esp_timer_get_time();
    if (now < next_tick) {
      const uint64_t remaining = next_tick - now;
      if (remaining > 1000) {
        vTaskDelay(pdMS_TO_TICKS(static_cast<uint32_t>(remaining / 1000)));
      } else {
        taskYIELD();
      }
      continue;
    }

    CANMessage msg{};
    msg.ext = false;
    msg.rtr = false;
    msg.len = 8;
    msg.id = 0x100u + bus_id;
    const uint32_t seq = s_produced[bus_id];
    msg.data[0] = static_cast<uint8_t>(seq);
    msg.data[1] = static_cast<uint8_t>(seq >> 8);
    msg.data[2] = static_cast<uint8_t>(seq >> 16);
    msg.data[3] = static_cast<uint8_t>(seq >> 24);
    msg.data[4] = static_cast<uint8_t>(bus_id);
    msg.data[5] = 0xA5;
    msg.data[6] = 0x5A;
    msg.data[7] = 0x00;

    char line[96];
    const size_t len = format_savvy_line(bus_id, now, msg, line, sizeof(line));
    if (len == 0 || len > kBlockSize) {
      continue;
    }

    portENTER_CRITICAL(&s_ring_mux[bus_id]);
    BusState& bus = s_buses[bus_id];
    s_produced[bus_id]++;
    LogBlockState* block = &bus.blocks[bus.write_index];
    if (block->state != 3) {
      bool found = false;
      for (uint8_t i = 0; i < kBlockCount; ++i) {
        const uint8_t candidate = (bus.write_index + i) % kBlockCount;
        if (bus.blocks[candidate].state == 0) {
          bus.write_index = candidate;
          block = &bus.blocks[candidate];
          block->len = 0;
          block->frames = 0;
          block->state = 3;
          found = true;
          break;
        }
      }
      if (!found) {
        block = nullptr;
      }
    }

    if (block != nullptr && block->len + len > kBlockSize) {
      if (block->len > 0) {
        block->state = 1;
      }
      bool found = false;
      for (uint8_t i = 0; i < kBlockCount; ++i) {
        const uint8_t candidate = (bus.write_index + 1 + i) % kBlockCount;
        if (bus.blocks[candidate].state == 0) {
          bus.write_index = candidate;
          block = &bus.blocks[candidate];
          block->len = 0;
          block->frames = 0;
          block->state = 3;
          found = true;
          break;
        }
      }
      if (!found) {
        block = nullptr;
      }
    }

    if (block == nullptr) {
      bus.drops++;
      portEXIT_CRITICAL(&s_ring_mux[bus_id]);
      continue;
    }

    memcpy(block->data + block->len, line, len);
    block->len += len;
    block->frames++;

    if (block->len >= kBlockSize) {
      block->state = 1;
    }

    size_t pending = 0;
    for (uint8_t i = 0; i < kBlockCount; ++i) {
      pending += bus.blocks[i].len;
    }
    if (pending > bus.high_water) {
      bus.high_water = static_cast<uint32_t>(pending);
    }
    portEXIT_CRITICAL(&s_ring_mux[bus_id]);

    next_tick += interval_us;
    if (++yield_count >= kLoadTestYieldEvery) {
      yield_count = 0;
      vTaskDelay(pdMS_TO_TICKS(1));
      last_idle_yield = esp_timer_get_time();
      continue;
    }

    const uint64_t after_push = esp_timer_get_time();
    if ((after_push - last_idle_yield) >= kIdleYieldIntervalUs) {
      vTaskDelay(pdMS_TO_TICKS(1));
      last_idle_yield = esp_timer_get_time();
    }
  }
}

} // namespace

namespace can {

// Initialize load-test RX tasks for each configured bus.
void init() {
  init_bus_state();
  s_initialized = true;

  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    if (s_rx_tasks[bus_id] == nullptr) {
      xTaskCreatePinnedToCore(
          load_task,
          s_rx_task_names[bus_id],
          4096,
          reinterpret_cast<void*>(static_cast<uintptr_t>(bus_id)),
          kRxTaskPriority,
          &s_rx_tasks[bus_id],
          kRxTaskCore);
    }
  }
}

// Stop all load-test RX tasks and reset initialized state.
void deinit() {
  s_initialized = false;
  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    if (s_rx_tasks[bus_id] != nullptr) {
      vTaskDelete(s_rx_tasks[bus_id]);
      s_rx_tasks[bus_id] = nullptr;
    }
  }
}

// Load-test build does not provide real RX frames.
bool pop_rx_frame(uint8_t bus_id, Frame& frame) {
  (void)bus_id;
  (void)frame;
  return false;
}

// Return the number of dropped frames for a bus.
uint32_t drop_count(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }

  uint32_t count = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  count = s_buses[bus_id].drops;
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return count;
}

// Return the maximum queued bytes observed for a bus.
uint32_t high_water(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }

  uint32_t value = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  value = s_buses[bus_id].high_water;
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return value;
}

} // namespace can

namespace load_test {

// Set the synthetic frame rate used by the load test.
void set_fps(uint32_t fps) {
  portENTER_CRITICAL(&s_ring_mux[0]);
  s_target_fps = fps;
  portEXIT_CRITICAL(&s_ring_mux[0]);
}

// Read the current synthetic frame rate.
uint32_t get_fps() {
  uint32_t fps = 0;
  portENTER_CRITICAL(&s_ring_mux[0]);
  fps = s_target_fps;
  portEXIT_CRITICAL(&s_ring_mux[0]);
  return fps;
}

// Return the number of frames produced for a bus.
uint32_t produced(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint32_t value = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  value = s_produced[bus_id];
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return value;
}

// Return the number of frames consumed for a bus.
uint32_t consumed(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint32_t value = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  value = s_consumed[bus_id];
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return value;
}

// Return the current queued bytes across blocks for a bus.
uint32_t queue_depth(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint32_t depth = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  for (uint8_t i = 0; i < kBlockCount; ++i) {
    depth += static_cast<uint32_t>(s_buses[bus_id].blocks[i].len);
  }
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return depth;
}

// Return the total queue capacity in bytes for one bus.
uint32_t queue_capacity() {
  return static_cast<uint32_t>(kBlockSize * kBlockCount);
}

// Count the number of free blocks for a bus.
uint8_t blocks_free(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint8_t count = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  for (uint8_t i = 0; i < kBlockCount; ++i) {
    if (s_buses[bus_id].blocks[i].state == 0) {
      count++;
    }
  }
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return count;
}

// Count the number of ready-to-flush blocks for a bus.
uint8_t blocks_ready(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint8_t count = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  for (uint8_t i = 0; i < kBlockCount; ++i) {
    if (s_buses[bus_id].blocks[i].state == 1) {
      count++;
    }
  }
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return count;
}

// Count the number of blocks owned by RX or logger for a bus.
uint8_t blocks_in_use(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint8_t count = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  for (uint8_t i = 0; i < kBlockCount; ++i) {
    const uint8_t state = s_buses[bus_id].blocks[i].state;
    if (state == 2 || state == 3) {
      count++;
    }
  }
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return count;
}

// Acquire the next ready block for logging.
bool acquire_log_block(uint8_t bus_id, LogBlock* out) {
  if (bus_id >= config::kMaxBuses || out == nullptr) {
    return false;
  }

  bool found = false;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  for (uint8_t i = 0; i < kBlockCount; ++i) {
    LogBlockState& block = s_buses[bus_id].blocks[i];
    if (block.state == 1) {
      block.state = 2;
      out->data = block.data;
      out->len = block.len;
      out->frames = block.frames;
      out->index = i;
      found = true;
      break;
    }
  }
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return found;
}

// Release a block after logging and update consumed counters.
void release_log_block(uint8_t bus_id, uint8_t index, uint32_t flushed_frames) {
  if (bus_id >= config::kMaxBuses || index >= kBlockCount) {
    return;
  }

  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  LogBlockState& block = s_buses[bus_id].blocks[index];
  block.state = 0;
  block.len = 0;
  block.frames = 0;
  s_consumed[bus_id] += flushed_frames;
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
}

} // namespace load_test

#endif // RX_LOAD_TEST
