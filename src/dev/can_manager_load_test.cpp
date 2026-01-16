#ifdef RX_LOAD_TEST

#include "can/can_manager.h"

#include <stdio.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "config/app_config.h"
#include "dev/load_test_control.h"

namespace {

constexpr size_t kRingCapacity = 256;
constexpr UBaseType_t kRxTaskPriority = configMAX_PRIORITIES - 2;
constexpr BaseType_t kRxTaskCore = 0;
constexpr uint32_t kLoadTestYieldEvery = 500;
constexpr uint64_t kIdleYieldIntervalUs = 5000;

struct Ring {
  static_assert((kRingCapacity & (kRingCapacity - 1)) == 0,
                "kRingCapacity must be power of two");

  bool push(const can::Frame& frame) {
    const size_t next = (head + 1) & mask;
    if (next == tail) {
      return false;
    }
    buffer[head] = frame;
    head = next;
    return true;
  }

  bool pop(can::Frame& frame) {
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
  can::Frame buffer[kRingCapacity];
};

struct BusState {
  Ring ring;
  uint32_t drops = 0;
  uint32_t high_water = 0;
};

BusState s_buses[config::kMaxBuses];
portMUX_TYPE s_ring_mux[config::kMaxBuses];
TaskHandle_t s_rx_tasks[config::kMaxBuses] = {};
char s_rx_task_names[config::kMaxBuses][12] = {};

volatile uint32_t s_target_fps = 0;
uint32_t s_produced[config::kMaxBuses] = {};
uint32_t s_consumed[config::kMaxBuses] = {};
bool s_initialized = false;

void init_bus_state() {
  for (size_t i = 0; i < config::kMaxBuses; ++i) {
    s_buses[i].ring.head = 0;
    s_buses[i].ring.tail = 0;
    s_buses[i].drops = 0;
    s_buses[i].high_water = 0;
    s_consumed[i] = 0;
    s_ring_mux[i] = portMUX_INITIALIZER_UNLOCKED;
    snprintf(s_rx_task_names[i],
             sizeof(s_rx_task_names[i]),
             "rx_load%u",
             static_cast<unsigned>(i));
  }
}

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

    can::Frame frame{};
    frame.timestamp_us = static_cast<uint64_t>(now);
    frame.bus_id = bus_id;
    frame.message = msg;

    portENTER_CRITICAL(&s_ring_mux[bus_id]);
    const bool pushed = s_buses[bus_id].ring.push(frame);
    const size_t depth = s_buses[bus_id].ring.size();
    if (depth > s_buses[bus_id].high_water) {
      s_buses[bus_id].high_water = static_cast<uint32_t>(depth);
    }
    if (!pushed) {
      s_buses[bus_id].drops++;
    }
    s_produced[bus_id]++;
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

  bool ok = false;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  ok = s_buses[bus_id].ring.pop(frame);
  if (ok) {
    s_consumed[bus_id]++;
  }
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

namespace load_test {

void set_fps(uint32_t fps) {
  portENTER_CRITICAL(&s_ring_mux[0]);
  s_target_fps = fps;
  portEXIT_CRITICAL(&s_ring_mux[0]);
}

uint32_t get_fps() {
  uint32_t fps = 0;
  portENTER_CRITICAL(&s_ring_mux[0]);
  fps = s_target_fps;
  portEXIT_CRITICAL(&s_ring_mux[0]);
  return fps;
}

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

uint32_t queue_depth(uint8_t bus_id) {
  if (bus_id >= config::kMaxBuses) {
    return 0;
  }
  uint32_t depth = 0;
  portENTER_CRITICAL(&s_ring_mux[bus_id]);
  depth = static_cast<uint32_t>(s_buses[bus_id].ring.size());
  portEXIT_CRITICAL(&s_ring_mux[bus_id]);
  return depth;
}

uint32_t queue_capacity() {
  return static_cast<uint32_t>(kRingCapacity);
}

} // namespace load_test

#endif // RX_LOAD_TEST
