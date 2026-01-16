#include "can/can_manager.h"

#include <ACAN2515.h>
#include <SPI.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdio.h>
#include <string.h>

#include "config/app_config.h"
#include "hardware/hardware_config.h"

namespace can {

namespace {

constexpr size_t kBlockSize = 8192;
constexpr uint8_t kBlockCount = 2;
constexpr UBaseType_t kRxTaskPriority = configMAX_PRIORITIES - 2;
constexpr BaseType_t kRxTaskCore = 0;

ACAN2515 s_can0(CAN1_CS_PIN, SPI, CAN1_INT_PIN);
ACAN2515 s_can1(CAN2_CS_PIN, SPI, CAN2_INT_PIN);
bool s_initialized = false;
TaskHandle_t s_rx_tasks[config::kMaxBuses] = {};
char s_rx_task_names[config::kMaxBuses][12] = {};

struct LogBlockState {
  uint8_t data[kBlockSize];
  size_t len = 0;
  uint32_t frames = 0;
  uint8_t state = 0; // 0=free, 1=ready, 2=logger_in_use, 3=rx_active
};

struct BusState {
  ACAN2515* driver = nullptr;
  bool enabled = false;
  LogBlockState blocks[kBlockCount];
  uint8_t write_index = 0;
  uint32_t drops = 0;
  uint32_t high_water = 0; // bytes pending
};

BusState s_buses[config::kMaxBuses];
portMUX_TYPE s_ring_mux[config::kMaxBuses];

// ISR trampoline for CAN0 controller.
void can0_isr() {
  s_can0.isr();
}

// ISR trampoline for CAN1 controller.
void can1_isr() {
  s_can1.isr();
}

// Configure an MCP2515 bus with the requested bitrate and ISR.
void configure_can_bus(ACAN2515& bus, uint32_t bitrate, void (*isr)()) {
  ACAN2515Settings settings(MCP2515_QUARTZ_HZ, bitrate);
  settings.mRequestedMode = ACAN2515Settings::NormalMode;
  bus.begin(settings, isr);
}

// Format a received CAN frame into a SavvyCAN ASCII line.
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

// Reset per-bus state, block buffers, and RX task names.
void init_bus_state() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    s_buses[i].driver = nullptr;
    s_buses[i].enabled = false;
    s_buses[i].write_index = 0;
    s_buses[i].drops = 0;
    s_buses[i].high_water = 0;
    for (uint8_t b = 0; b < kBlockCount; ++b) {
      s_buses[i].blocks[b].len = 0;
      s_buses[i].blocks[b].frames = 0;
      s_buses[i].blocks[b].state = 0;
    }
    s_ring_mux[i] = portMUX_INITIALIZER_UNLOCKED;
    snprintf(s_rx_task_names[i],
             sizeof(s_rx_task_names[i]),
             "can_rx%u",
             static_cast<unsigned>(i));
  }

  s_buses[0].driver = &s_can0;
  s_buses[1].driver = &s_can1;
}

// One RX task per bus; pinned to core 0 at highest priority.
// Per-bus RX task that drains MCP2515 and fills log blocks.
void rx_task(void* param) {
  const uint8_t bus_id = static_cast<uint8_t>(
      reinterpret_cast<uintptr_t>(param));
  CANMessage msg;

  for (;;) {
    if (!s_initialized) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    if (bus_id >= config::kMaxBuses) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    BusState& bus = s_buses[bus_id];
    if (!bus.enabled || bus.driver == nullptr) {
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }

    bool any = false;
    while (bus.driver->available()) {
      bus.driver->receive(msg);
      const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
      char line[96];
      const size_t len = format_savvy_line(bus_id, now, msg, line, sizeof(line));
      if (len == 0 || len > kBlockSize) {
        continue;
      }

      portENTER_CRITICAL(&s_ring_mux[bus_id]);
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

      any = true;
    }

    if (!any) {
      vTaskDelay(pdMS_TO_TICKS(1));
    }
  }
}

} // namespace

// Initialize CAN buses and start RX tasks for enabled channels.
void init() {
  SPI.setFrequency(CAN_SPI_CLOCK_HZ);
  SPI.begin(CAN_SPI_SCK_PIN, CAN_SPI_MISO_PIN, CAN_SPI_MOSI_PIN);

  const config::Config& cfg = config::get();
  init_bus_state();

  if (cfg.buses[0].enabled) {
    configure_can_bus(s_can0, cfg.buses[0].bitrate, can0_isr);
    s_buses[0].enabled = true;
  }
  if (cfg.buses[1].enabled) {
    configure_can_bus(s_can1, cfg.buses[1].bitrate, can1_isr);
    s_buses[1].enabled = true;
  }

  s_initialized = true;

  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    if (!s_buses[bus_id].enabled || s_buses[bus_id].driver == nullptr) {
      continue;
    }
    if (s_rx_tasks[bus_id] == nullptr) {
      // RX tasks are pinned to core 0 at highest priority to avoid drops.
      xTaskCreatePinnedToCore(
          rx_task,
          s_rx_task_names[bus_id],
          4096,
          reinterpret_cast<void*>(static_cast<uintptr_t>(bus_id)),
          kRxTaskPriority,
          &s_rx_tasks[bus_id],
          kRxTaskCore);
    }
  }
}

// Stop all RX tasks and mark the CAN subsystem uninitialized.
void deinit() {
  s_initialized = false;
  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    if (s_rx_tasks[bus_id] != nullptr) {
      vTaskDelete(s_rx_tasks[bus_id]);
      s_rx_tasks[bus_id] = nullptr;
    }
  }
}

// Placeholder for per-frame pop; logging is block-based now.
bool pop_rx_frame(uint8_t bus_id, Frame& frame) {
  (void)bus_id;
  (void)frame;
  return false;
}

// Acquire the next ready log block for a bus.
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

// Release a log block after it has been written.
void release_log_block(uint8_t bus_id, uint8_t index, uint32_t flushed_frames) {
  if (bus_id >= config::kMaxBuses || index >= kBlockCount) {
    return;
  }

  (void)flushed_frames;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  LogBlockState& block = s_buses[bus_id].blocks[index];
  block.state = 0;
  block.len = 0;
  block.frames = 0;
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
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
