#ifdef RX_LOAD_TEST

#include <Arduino.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "dev/load_test_control.h"
#include "logging/log_writer.h"
#include "storage/storage_manager.h"

namespace {

void enable_all_buses() {
  config::Config& cfg = config::get_mutable();
  for (uint8_t i = 0; i < config::kMaxBuses; ++i) {
    cfg.buses[i].enabled = true;
    cfg.buses[i].logging = true;
  }
}

void print_stats() {
  const uint32_t fps = load_test::get_fps();
  Serial.print("Target FPS per bus: ");
  Serial.println(fps);
  Serial.print("Queue capacity: ");
  Serial.println(load_test::queue_capacity());
  const logging::Stats log_stats = logging::get_stats();
  Serial.print("Log write rate: ");
  Serial.print(log_stats.bytes_per_sec);
  Serial.println(" B/s");
  Serial.print("Log started: ");
  Serial.print(log_stats.started ? "yes" : "no");
  Serial.print(" | Active buses: ");
  Serial.print(log_stats.active_buses);
  Serial.print(" | Open failures: ");
  Serial.println(log_stats.open_failures);
  Serial.print("Log loops: ");
  Serial.print(log_stats.loop_count);
  Serial.print(" | Pops: ");
  Serial.print(log_stats.pop_count);
  Serial.print(" | Last write age (ms): ");
  if (log_stats.last_write_ms == 0) {
    Serial.println("n/a");
  } else {
    Serial.println(millis() - log_stats.last_write_ms);
  }
  for (uint8_t bus_id = 0; bus_id < config::kMaxBuses; ++bus_id) {
    Serial.print("Bus ");
    Serial.print(bus_id + 1);
    Serial.print(" | Produced: ");
    Serial.print(load_test::produced(bus_id));
    Serial.print(" | Consumed: ");
    Serial.print(load_test::consumed(bus_id));
    Serial.print(" | Depth: ");
    Serial.print(load_test::queue_depth(bus_id));
    Serial.print(" | Drops: ");
    Serial.print(can::drop_count(bus_id));
    Serial.print(" | High-water: ");
    Serial.println(can::high_water(bus_id));
  }
}

void handle_serial() {
  static char line[32];
  static size_t len = 0;
  static uint32_t last_rx_ms = 0;

  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    last_rx_ms = millis();
    if (c == '\n' || c == '\r') {
      line[len] = '\0';
      if (len > 0) {
        if (line[0] == 'f' || line[0] == 'F') {
          const uint32_t fps = static_cast<uint32_t>(strtoul(line + 1, nullptr, 10));
          if (fps > 0) {
            load_test::set_fps(fps);
            Serial.print("Set target FPS per bus to ");
            Serial.println(fps);
          } else {
            Serial.println("Ignored FPS (must be > 0).");
          }
        } else if (line[0] == 's' || line[0] == 'S') {
          print_stats();
        }
      }
      len = 0;
    } else if (len + 1 < sizeof(line)) {
      line[len++] = c;
    }
  }

  if (len > 0 && (millis() - last_rx_ms) > 250) {
    line[len] = '\0';
    if (line[0] == 'f' || line[0] == 'F') {
      const uint32_t fps = static_cast<uint32_t>(strtoul(line + 1, nullptr, 10));
      if (fps > 0) {
        load_test::set_fps(fps);
        Serial.print("Set target FPS per bus to ");
        Serial.println(fps);
      } else {
        Serial.println("Ignored FPS (must be > 0).");
      }
    } else if (line[0] == 's' || line[0] == 'S') {
      print_stats();
    }
    len = 0;
  }
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("RX load test: 'f <fps>' sets per-bus rate, 's' prints stats.");

  // Relax watchdog monitoring for the stress test to avoid IDLE0 trips.
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(0));
  esp_task_wdt_delete(xTaskGetIdleTaskHandleForCPU(1));

  config::init();
  enable_all_buses();
  storage::init();
  can::init();
  logging::init();
  if (storage::is_ready()) {
    logging::start();
  } else {
    Serial.println("SD init failed, logging disabled.");
  }

  // ~4k FPS ~= 500 kbps for classic 11-bit CAN with 8-byte payload (approx).
  load_test::set_fps(4000);
}

void loop() {
  handle_serial();
  delay(200);
}

#endif // RX_LOAD_TEST
