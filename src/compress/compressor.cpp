#include "compress/compressor.h"

#include <Arduino.h>
#include <SD.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "esp32s3/rom/miniz.h"
#include "logging/log_writer.h"
#include "storage/storage_manager.h"

namespace compressor {

namespace {

constexpr char kCompressedDir[] = "/cmp";
constexpr uint32_t kTaskSleepMs = 1000;
constexpr uint32_t kBetweenFilesPauseMs = 15000;
constexpr uint32_t kBusyPauseMs = 3000;
constexpr UBaseType_t kTaskPriority = tskIDLE_PRIORITY;
constexpr BaseType_t kTaskCore = 1;

TaskHandle_t s_task = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
bool s_initialized = false;
bool s_active = false;
uint32_t s_current_done = 0;
uint32_t s_current_total = 0;
uint32_t s_compressed_files_total = 0;
uint64_t s_compressed_input_total = 0;

bool low_space_for_compression() {
  const storage::Stats st = storage::get_stats();
  const config::GlobalConfig& global = config::get().global;
  const uint64_t reserve = static_cast<uint64_t>(global.low_space_threshold_bytes) +
                           static_cast<uint64_t>(global.max_file_size_bytes);
  return st.free_bytes <= reserve;
}

bool logger_or_can_busy() {
  const logging::Stats log = logging::get_stats();
  if (!log.started) {
    return false;
  }
  if (log.bytes_per_sec > (64UL * 1024UL)) {
    return true;
  }
  for (uint8_t bus = 0; bus < config::kMaxBuses; ++bus) {
    if (can::bus_load_pct(bus) >= 20) {
      return true;
    }
  }
  return false;
}

struct OutputCtx {
  File* out;
  bool failed;
};

bool build_compressed_path(const char* src, char* out, size_t out_len) {
  if (src == nullptr || out == nullptr || out_len == 0) {
    return false;
  }
  const char* base = strrchr(src, '/');
  base = base ? (base + 1) : src;
  const int n = snprintf(out, out_len, "%s/%s.mz", kCompressedDir, base);
  if (n <= 0 || static_cast<size_t>(n) >= out_len) {
    if (out_len > 0) {
      out[0] = '\0';
    }
    return false;
  }
  return true;
}

bool build_temp_path(const char* final_path, char* out, size_t out_len) {
  if (final_path == nullptr || out == nullptr || out_len == 0) {
    return false;
  }
  const int n = snprintf(out, out_len, "%s.tmp", final_path);
  if (n <= 0 || static_cast<size_t>(n) >= out_len) {
    out[0] = '\0';
    return false;
  }
  return true;
}

mz_bool put_buf_cb(const void* pBuf, int len, void* pUser) {
  if (pBuf == nullptr || pUser == nullptr || len <= 0) {
    return MZ_FALSE;
  }
  OutputCtx* ctx = static_cast<OutputCtx*>(pUser);
  if (ctx->failed || ctx->out == nullptr || !(*ctx->out)) {
    return MZ_FALSE;
  }
  const size_t written =
      ctx->out->write(reinterpret_cast<const uint8_t*>(pBuf), static_cast<size_t>(len));
  if (written != static_cast<size_t>(len)) {
    ctx->failed = true;
    return MZ_FALSE;
  }
  return MZ_TRUE;
}

bool compress_file(const storage::FileInfo& info) {
  char out_path[96];
  if (!build_compressed_path(info.path, out_path, sizeof(out_path))) {
    return false;
  }
  if (SD.exists(out_path)) {
    return true;
  }
  if (!SD.exists(kCompressedDir)) {
    SD.mkdir(kCompressedDir);
  }

  char temp_path[100];
  if (!build_temp_path(out_path, temp_path, sizeof(temp_path))) {
    return false;
  }

  File in = SD.open(info.path, FILE_READ);
  if (!in) {
    return false;
  }
  if (SD.exists(temp_path)) {
    SD.remove(temp_path);
  }
  File out = SD.open(temp_path, FILE_WRITE);
  if (!out) {
    in.close();
    return false;
  }

  OutputCtx ctx{&out, false};
  tdefl_compressor* comp =
      static_cast<tdefl_compressor*>(malloc(sizeof(tdefl_compressor)));
  if (comp == nullptr) {
    in.close();
    out.close();
    SD.remove(temp_path);
    return false;
  }
  const int flags = TDEFL_WRITE_ZLIB_HEADER | TDEFL_DEFAULT_MAX_PROBES;
  if (tdefl_init(comp, put_buf_cb, &ctx, flags) < 0) {
    free(comp);
    in.close();
    out.close();
    SD.remove(temp_path);
    return false;
  }

  uint8_t buffer[1024];
  bool ok = true;
  for (;;) {
    const size_t n = in.read(buffer, sizeof(buffer));
    if (n == 0) {
      break;
    }
    const tdefl_status st = tdefl_compress_buffer(comp, buffer, n, TDEFL_NO_FLUSH);
    if (st < 0 || ctx.failed) {
      ok = false;
      break;
    }
    portENTER_CRITICAL(&s_mux);
    s_current_done += static_cast<uint32_t>(n);
    portEXIT_CRITICAL(&s_mux);
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (ok) {
    const tdefl_status st = tdefl_compress_buffer(comp, nullptr, 0, TDEFL_FINISH);
    if (st < 0 || ctx.failed) {
      ok = false;
    }
  }
  free(comp);

  in.close();
  out.flush();
  out.close();

  if (!ok) {
    SD.remove(temp_path);
    return false;
  }

  if (SD.exists(out_path)) {
    SD.remove(out_path);
  }
  if (!SD.rename(temp_path, out_path)) {
    SD.remove(temp_path);
    return false;
  }

  portENTER_CRITICAL(&s_mux);
  s_compressed_files_total++;
  s_compressed_input_total += info.size_bytes;
  portEXIT_CRITICAL(&s_mux);
  return true;
}

bool pick_next_candidate(storage::FileInfo* out) {
  if (out == nullptr) {
    return false;
  }
  const size_t count = storage::file_count();
  for (size_t i = 0; i < count; ++i) {
    storage::FileInfo info{};
    if (!storage::get_file_info(i, &info)) {
      continue;
    }
    if (info.flags & storage::kFlagActive) {
      continue;
    }
    char cmp_path[96];
    if (!build_compressed_path(info.path, cmp_path, sizeof(cmp_path))) {
      continue;
    }
    if (SD.exists(cmp_path)) {
      continue;
    }
    *out = info;
    return true;
  }
  return false;
}

void task_entry(void*) {
  for (;;) {
    if (!s_initialized || !storage::is_ready()) {
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    if (!config::get().global.compressor_enabled) {
      portENTER_CRITICAL(&s_mux);
      s_active = false;
      s_current_done = 0;
      s_current_total = 0;
      portEXIT_CRITICAL(&s_mux);
      vTaskDelay(pdMS_TO_TICKS(kTaskSleepMs));
      continue;
    }
    if (logger_or_can_busy() || low_space_for_compression()) {
      vTaskDelay(pdMS_TO_TICKS(kBusyPauseMs));
      continue;
    }

    storage::FileInfo info{};
    if (!pick_next_candidate(&info)) {
      vTaskDelay(pdMS_TO_TICKS(kTaskSleepMs));
      continue;
    }

    portENTER_CRITICAL(&s_mux);
    s_active = true;
    s_current_done = 0;
    s_current_total = info.size_bytes;
    portEXIT_CRITICAL(&s_mux);

    const bool ok = compress_file(info);
    Serial.printf("[COMPRESS] %s %s\n", ok ? "ok" : "fail", info.path);

    portENTER_CRITICAL(&s_mux);
    s_active = false;
    s_current_done = 0;
    s_current_total = 0;
    portEXIT_CRITICAL(&s_mux);

    vTaskDelay(pdMS_TO_TICKS(kBetweenFilesPauseMs));
  }
}

} // namespace

void init() {
  if (s_initialized) {
    return;
  }
  s_initialized = true;
  xTaskCreatePinnedToCore(task_entry,
                          "compress_task",
                          12288,
                          nullptr,
                          kTaskPriority,
                          &s_task,
                          kTaskCore);
}

Stats get_stats() {
  Stats stats{};
  if (!config::get().global.compressor_enabled) {
    return stats;
  }
  portENTER_CRITICAL(&s_mux);
  stats.active = s_active;
  stats.current_input_done_bytes = s_current_done;
  stats.current_input_total_bytes = s_current_total;
  stats.compressed_files_total = s_compressed_files_total;
  stats.compressed_input_bytes_total = s_compressed_input_total;
  portEXIT_CRITICAL(&s_mux);

  uint32_t outstanding_files = 0;
  uint64_t outstanding_bytes = 0;
  const size_t count = storage::file_count();
  for (size_t i = 0; i < count; ++i) {
    storage::FileInfo info{};
    if (!storage::get_file_info(i, &info)) {
      continue;
    }
    if (info.flags & storage::kFlagActive) {
      continue;
    }
    char cmp_path[96];
    if (!build_compressed_path(info.path, cmp_path, sizeof(cmp_path))) {
      continue;
    }
    if (SD.exists(cmp_path)) {
      continue;
    }
    outstanding_files++;
    outstanding_bytes += info.size_bytes;
  }

  stats.outstanding_files = outstanding_files;
  stats.outstanding_bytes = outstanding_bytes;
  return stats;
}

} // namespace compressor
