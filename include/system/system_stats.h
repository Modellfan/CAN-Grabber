#ifndef SYSTEM_SYSTEM_STATS_H
#define SYSTEM_SYSTEM_STATS_H

#include <stddef.h>
#include <stdint.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "rtc/rtc_clock.h"

namespace system_stats {

enum class Component : uint8_t {
  kBoot = 0,
  kRtc,
  kStorage,
  kCan,
  kLogging,
  kUpload,
  kNet,
  kRest,
  kWeb,
  kLoop,
  kCount,
};

constexpr size_t kComponentCount = static_cast<size_t>(Component::kCount);
constexpr size_t kMaxTasks = 12;
constexpr size_t kDetailLength = 24;
constexpr size_t kTaskNameLength = 16;

struct ComponentStat {
  bool valid;
  uint32_t sample_count;
  uint64_t last_unix_ms;
  uint32_t last_uptime_ms;
  uint32_t last_free_heap;
  uint32_t last_free_internal;
  uint32_t last_largest_internal;
  uint32_t min_free_heap;
  uint32_t min_free_internal;
  uint32_t min_largest_internal;
  uint16_t last_stack_words;
  uint16_t min_stack_words;
  char last_detail[kDetailLength];
  char task_name[kTaskNameLength];
};

struct TaskStat {
  bool valid;
  uint32_t sample_count;
  uint16_t last_stack_words;
  uint16_t min_stack_words;
  char name[kTaskNameLength];
};

struct Snapshot {
  uint64_t unix_ms;
  uint32_t uptime_ms;
  bool rtc_valid;
  bool rtc_available;
  bool rtc_running;
  rtc_clock::Source rtc_source;
  ComponentStat components[kComponentCount];
  TaskStat tasks[kMaxTasks];
};

void init();
void sample(Component component, const char* detail = nullptr, TaskHandle_t task = nullptr);
void sample_task(const char* name, TaskHandle_t handle);
Snapshot snapshot();
void print_component(Component component);
void print_summary(const char* reason = nullptr);
const char* component_name(Component component);

} // namespace system_stats

#endif // SYSTEM_SYSTEM_STATS_H
