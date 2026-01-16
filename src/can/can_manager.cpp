#include "can/can_manager.h"

#include <ACAN2515.h>
#include <SPI.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config/app_config.h"
#include "hardware/hardware_config.h"

namespace can {

namespace {

constexpr size_t kRingCapacity = 256;
constexpr UBaseType_t kRxTaskPriority = configMAX_PRIORITIES - 1;
constexpr BaseType_t kRxTaskCore = 0;

ACAN2515 s_can0(CAN1_CS_PIN, SPI, CAN1_INT_PIN);
ACAN2515 s_can1(CAN2_CS_PIN, SPI, CAN2_INT_PIN);
bool s_initialized = false;
TaskHandle_t s_rx_tasks[config::kMaxBuses] = {};
char s_rx_task_names[config::kMaxBuses][12] = {};

// Per-bus ring buffer for RX frames.
struct Ring {
  static_assert((kRingCapacity & (kRingCapacity - 1)) == 0,
                "kRingCapacity must be power of two");

  bool push(const Frame& frame) {
    const size_t next = (head + 1) & mask;
    if (next == tail) {
      return false;
    }
    buffer[head] = frame;
    head = next;
    return true;
  }

  bool pop(Frame& frame) {
    if (tail == head) {
      return false;
    }
    frame = buffer[tail];
    tail = (tail + 1) & mask;
    return true;
  }

  size_t size() const {
    return (head - tail) & mask;
  }

  static constexpr size_t mask = kRingCapacity - 1;
  volatile size_t head = 0;
  volatile size_t tail = 0;
  Frame buffer[kRingCapacity];
};

struct BusState {
  ACAN2515* driver = nullptr;
  bool enabled = false;
  Ring ring;
  uint32_t drops = 0;
  uint32_t high_water = 0;
};

BusState s_buses[config::kMaxBuses];
portMUX_TYPE s_ring_mux[config::kMaxBuses];

void can0_isr() {
  s_can0.isr();
}

void can1_isr() {
  s_can1.isr();
}

void configure_can_bus(ACAN2515& bus, uint32_t bitrate, void (*isr)()) {
  ACAN2515Settings settings(MCP2515_QUARTZ_HZ, bitrate);
  settings.mRequestedMode = ACAN2515Settings::NormalMode;
  bus.begin(settings, isr);
}

void init_bus_state() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    s_buses[i].driver = nullptr;
    s_buses[i].enabled = false;
    s_buses[i].ring.head = 0;
    s_buses[i].ring.tail = 0;
    s_buses[i].drops = 0;
    s_buses[i].high_water = 0;
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
      Frame frame{};
      frame.timestamp_us = static_cast<uint64_t>(esp_timer_get_time());
      frame.bus_id = bus_id;
      frame.message = msg;

      portENTER_CRITICAL(&s_ring_mux[bus_id]);
      const bool pushed = bus.ring.push(frame);
      const size_t depth = bus.ring.size();
      if (depth > bus.high_water) {
        bus.high_water = static_cast<uint32_t>(depth);
      }
      if (!pushed) {
        bus.drops++;
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

void deinit() {
  s_initialized = false;
  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    if (s_rx_tasks[bus_id] != nullptr) {
      vTaskDelete(s_rx_tasks[bus_id]);
      s_rx_tasks[bus_id] = nullptr;
    }
  }
}

bool pop_rx_frame(uint8_t bus_id, Frame& frame) {
  if (bus_id >= config::kMaxBuses) {
    return false;
  }

  BusState& bus = s_buses[bus_id];
  if (!bus.enabled) {
    return false;
  }

  bool ok = false;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  ok = bus.ring.pop(frame);
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return ok;
}

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
