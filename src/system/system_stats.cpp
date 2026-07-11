#include "system/system_stats.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace system_stats {

namespace {

portMUX_TYPE s_stats_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_initialized = false;
ComponentStat s_component_stats[kComponentCount];
TaskStat s_task_stats[kMaxTasks];

struct HeapDiag {
  uint32_t free_heap;
  uint32_t free_internal;
  uint32_t largest_internal;
};

HeapDiag capture_heap_diag() {
  HeapDiag diag{};
  diag.free_heap = static_cast<uint32_t>(ESP.getFreeHeap());
  diag.free_internal =
      static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  diag.largest_internal = static_cast<uint32_t>(
      heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  return diag;
}

const char* source_name(rtc_clock::Source source) {
  switch (source) {
    case rtc_clock::Source::kRtc:
      return "rtc";
    case rtc_clock::Source::kManualConfig:
      return "config";
    case rtc_clock::Source::kBuildTime:
      return "build";
    case rtc_clock::Source::kUnavailable:
    default:
      return "unavailable";
  }
}

void copy_text(char* dst, size_t dst_len, const char* src) {
  if (dst == nullptr || dst_len == 0) {
    return;
  }
  if (src == nullptr) {
    dst[0] = '\0';
    return;
  }
  strncpy(dst, src, dst_len);
  dst[dst_len - 1] = '\0';
}

TaskHandle_t normalize_task(TaskHandle_t task) {
  return (task != nullptr) ? task : xTaskGetCurrentTaskHandle();
}

uint16_t stack_words_for(TaskHandle_t task) {
  if (task == nullptr) {
    return 0;
  }
  const UBaseType_t words = uxTaskGetStackHighWaterMark(task);
  return (words > 0xFFFFu) ? 0xFFFFu : static_cast<uint16_t>(words);
}

const char* task_name_for(TaskHandle_t task) {
  if (task == nullptr) {
    return "";
  }
  const char* name = pcTaskGetName(task);
  return (name != nullptr) ? name : "";
}

size_t task_slot_for(const char* name) {
  if (name == nullptr || name[0] == '\0') {
    return kMaxTasks;
  }

  for (size_t i = 0; i < kMaxTasks; ++i) {
    if (s_task_stats[i].valid && strcmp(s_task_stats[i].name, name) == 0) {
      return i;
    }
  }
  for (size_t i = 0; i < kMaxTasks; ++i) {
    if (!s_task_stats[i].valid) {
      return i;
    }
  }
  return kMaxTasks;
}

void update_task_stat_locked(const char* name, uint16_t stack_words) {
  const size_t index = task_slot_for(name);
  if (index >= kMaxTasks) {
    return;
  }

  TaskStat& stat = s_task_stats[index];
  if (!stat.valid) {
    memset(&stat, 0, sizeof(stat));
    stat.valid = true;
    copy_text(stat.name, sizeof(stat.name), name);
    stat.min_stack_words = stack_words;
  }
  stat.sample_count++;
  stat.last_stack_words = stack_words;
  if (stat.min_stack_words == 0 || stack_words < stat.min_stack_words) {
    stat.min_stack_words = stack_words;
  }
}

} // namespace

const char* component_name(Component component) {
  switch (component) {
    case Component::kBoot:
      return "boot";
    case Component::kRtc:
      return "rtc";
    case Component::kStorage:
      return "storage";
    case Component::kCan:
      return "can";
    case Component::kLogging:
      return "logging";
    case Component::kUpload:
      return "upload";
    case Component::kNet:
      return "net";
    case Component::kRest:
      return "rest";
    case Component::kWeb:
      return "web";
    case Component::kLoop:
      return "loop";
    case Component::kCount:
    default:
      return "unknown";
  }
}

void init() {
  portENTER_CRITICAL(&s_stats_mux);
  if (!s_initialized) {
    memset(s_component_stats, 0, sizeof(s_component_stats));
    memset(s_task_stats, 0, sizeof(s_task_stats));
    s_initialized = true;
  }
  portEXIT_CRITICAL(&s_stats_mux);
}

void sample_task(const char* name, TaskHandle_t handle) {
  if (!s_initialized) {
    init();
  }

  const TaskHandle_t normalized = normalize_task(handle);
  const uint16_t stack_words = stack_words_for(normalized);
  const char* resolved_name =
      (name != nullptr && name[0] != '\0') ? name : task_name_for(normalized);

  portENTER_CRITICAL(&s_stats_mux);
  update_task_stat_locked(resolved_name, stack_words);
  portEXIT_CRITICAL(&s_stats_mux);
}

void sample(Component component, const char* detail, TaskHandle_t task) {
  if (!s_initialized) {
    init();
  }

  const size_t index = static_cast<size_t>(component);
  if (index >= kComponentCount) {
    return;
  }

  const HeapDiag heap = capture_heap_diag();
  const TaskHandle_t normalized = normalize_task(task);
  const uint16_t stack_words = stack_words_for(normalized);
  const char* task_name = task_name_for(normalized);
  const uint64_t unix_ms = rtc_clock::now_unix_ms();
  const uint32_t uptime_ms = millis();

  portENTER_CRITICAL(&s_stats_mux);
  ComponentStat& stat = s_component_stats[index];
  if (!stat.valid) {
    memset(&stat, 0, sizeof(stat));
    stat.valid = true;
    stat.min_free_heap = heap.free_heap;
    stat.min_free_internal = heap.free_internal;
    stat.min_largest_internal = heap.largest_internal;
    stat.min_stack_words = stack_words;
  }
  stat.sample_count++;
  stat.last_unix_ms = unix_ms;
  stat.last_uptime_ms = uptime_ms;
  stat.last_free_heap = heap.free_heap;
  stat.last_free_internal = heap.free_internal;
  stat.last_largest_internal = heap.largest_internal;
  stat.last_stack_words = stack_words;
  if (heap.free_heap < stat.min_free_heap) {
    stat.min_free_heap = heap.free_heap;
  }
  if (heap.free_internal < stat.min_free_internal) {
    stat.min_free_internal = heap.free_internal;
  }
  if (heap.largest_internal < stat.min_largest_internal) {
    stat.min_largest_internal = heap.largest_internal;
  }
  if (stat.min_stack_words == 0 || stack_words < stat.min_stack_words) {
    stat.min_stack_words = stack_words;
  }
  copy_text(stat.last_detail, sizeof(stat.last_detail), detail);
  copy_text(stat.task_name, sizeof(stat.task_name), task_name);
  update_task_stat_locked(task_name, stack_words);
  portEXIT_CRITICAL(&s_stats_mux);
}

Snapshot snapshot() {
  if (!s_initialized) {
    init();
  }

  Snapshot snap{};
  snap.unix_ms = rtc_clock::now_unix_ms();
  snap.uptime_ms = millis();
  snap.rtc_valid = rtc_clock::is_valid();
  snap.rtc_available = rtc_clock::is_available();
  snap.rtc_running = rtc_clock::is_running();
  snap.rtc_source = rtc_clock::source();

  portENTER_CRITICAL(&s_stats_mux);
  memcpy(snap.components, s_component_stats, sizeof(s_component_stats));
  memcpy(snap.tasks, s_task_stats, sizeof(s_task_stats));
  portEXIT_CRITICAL(&s_stats_mux);
  return snap;
}

void print_component(Component component) {
  const Snapshot snap = snapshot();
  const size_t index = static_cast<size_t>(component);
  if (index >= kComponentCount || !snap.components[index].valid) {
    Serial.printf("[system] component=%s has no samples\n", component_name(component));
    return;
  }

  const ComponentStat& stat = snap.components[index];
  Serial.printf(
      "[system] component=%s samples=%lu last=%s heap=%lu internal=%lu largest=%lu "
      "minHeap=%lu minInternal=%lu minLargest=%lu task=%s stackWords=%u minStackWords=%u "
      "unixMs=%llu uptimeMs=%lu\n",
      component_name(component),
      static_cast<unsigned long>(stat.sample_count),
      stat.last_detail,
      static_cast<unsigned long>(stat.last_free_heap),
      static_cast<unsigned long>(stat.last_free_internal),
      static_cast<unsigned long>(stat.last_largest_internal),
      static_cast<unsigned long>(stat.min_free_heap),
      static_cast<unsigned long>(stat.min_free_internal),
      static_cast<unsigned long>(stat.min_largest_internal),
      stat.task_name,
      static_cast<unsigned>(stat.last_stack_words),
      static_cast<unsigned>(stat.min_stack_words),
      static_cast<unsigned long long>(stat.last_unix_ms),
      static_cast<unsigned long>(stat.last_uptime_ms));
}

void print_summary(const char* reason) {
  const Snapshot snap = snapshot();
  Serial.printf(
      "[system] summary reason=%s unixMs=%llu uptimeMs=%lu rtc=%s valid=%s available=%s running=%s\n",
      (reason != nullptr) ? reason : "manual",
      static_cast<unsigned long long>(snap.unix_ms),
      static_cast<unsigned long>(snap.uptime_ms),
      source_name(snap.rtc_source),
      snap.rtc_valid ? "true" : "false",
      snap.rtc_available ? "true" : "false",
      snap.rtc_running ? "true" : "false");

  for (size_t i = 0; i < kComponentCount; ++i) {
    if (!snap.components[i].valid) {
      continue;
    }
    const ComponentStat& stat = snap.components[i];
    Serial.printf(
        "[system] component=%s samples=%lu last=%s heap=%lu internal=%lu largest=%lu "
        "minHeap=%lu minInternal=%lu minLargest=%lu task=%s stackWords=%u minStackWords=%u "
        "unixMs=%llu uptimeMs=%lu\n",
        component_name(static_cast<Component>(i)),
        static_cast<unsigned long>(stat.sample_count),
        stat.last_detail,
        static_cast<unsigned long>(stat.last_free_heap),
        static_cast<unsigned long>(stat.last_free_internal),
        static_cast<unsigned long>(stat.last_largest_internal),
        static_cast<unsigned long>(stat.min_free_heap),
        static_cast<unsigned long>(stat.min_free_internal),
        static_cast<unsigned long>(stat.min_largest_internal),
        stat.task_name,
        static_cast<unsigned>(stat.last_stack_words),
        static_cast<unsigned>(stat.min_stack_words),
        static_cast<unsigned long long>(stat.last_unix_ms),
        static_cast<unsigned long>(stat.last_uptime_ms));
  }

  for (size_t i = 0; i < kMaxTasks; ++i) {
    const TaskStat& task = snap.tasks[i];
    if (!task.valid) {
      continue;
    }
    Serial.printf(
        "[system] task=%s samples=%lu stackWords=%u minStackWords=%u\n",
        task.name,
        static_cast<unsigned long>(task.sample_count),
        static_cast<unsigned>(task.last_stack_words),
        static_cast<unsigned>(task.min_stack_words));
  }
}

} // namespace system_stats
