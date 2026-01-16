#include "storage/storage_manager.h"

#include <ArduinoJson.h>
#include <SD.h>
#include <SPI.h>
#include <string.h>

#include "config/app_config.h"
#include "hardware/hardware_config.h"

namespace storage {

namespace {

SPIClass s_sd_spi(HSPI);
bool s_ready = false;

constexpr char kMetaDir[] = "/meta";
constexpr char kStatusPath[] = "/meta/file_status.json";
constexpr uint8_t kSdMaxFiles = 12;
constexpr uint8_t kFlagDownloaded = 1u << 0;
constexpr uint8_t kFlagUploaded = 1u << 1;
constexpr uint8_t kFlagActive = 1u << 2;
constexpr size_t kMaxEntries = 128;

struct FileStatusEntry {
  char path[64];
  uint32_t start_ms;
  uint32_t size_bytes;
  uint8_t bus_id;
  uint8_t flags;
};

FileStatusEntry s_entries[kMaxEntries];
size_t s_entry_count = 0;

void ensure_dir(const char* path) {
  if (!SD.exists(path)) {
    SD.mkdir(path);
  }
}

int find_entry(const char* path) {
  for (size_t i = 0; i < s_entry_count; ++i) {
    if (strcmp(s_entries[i].path, path) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

void remove_entry(size_t index) {
  if (index >= s_entry_count) {
    return;
  }
  for (size_t i = index + 1; i < s_entry_count; ++i) {
    s_entries[i - 1] = s_entries[i];
  }
  if (s_entry_count > 0) {
    --s_entry_count;
  }
}

bool load_status() {
  s_entry_count = 0;
  if (!SD.exists(kStatusPath)) {
    return true;
  }

  File file = SD.open(kStatusPath, FILE_READ);
  if (!file) {
    return false;
  }

  DynamicJsonDocument doc(16384);
  DeserializationError err = deserializeJson(doc, file);
  file.close();
  if (err) {
    return false;
  }

  JsonArray files = doc["files"].as<JsonArray>();
  for (JsonObject obj : files) {
    if (s_entry_count >= kMaxEntries) {
      break;
    }
    const char* path = obj["path"] | "";
    if (path[0] == '\0') {
      continue;
    }
    FileStatusEntry& entry = s_entries[s_entry_count++];
    strncpy(entry.path, path, sizeof(entry.path));
    entry.path[sizeof(entry.path) - 1] = '\0';
    entry.bus_id = static_cast<uint8_t>(obj["bus"] | 0);
    entry.start_ms = static_cast<uint32_t>(obj["start_ms"] | 0);
    entry.size_bytes = static_cast<uint32_t>(obj["size"] | 0);
    entry.flags = static_cast<uint8_t>(obj["flags"] | 0);
  }

  return true;
}

bool save_status() {
  ensure_dir(kMetaDir);
  if (SD.exists(kStatusPath)) {
    SD.remove(kStatusPath);
  }
  File file = SD.open(kStatusPath, FILE_WRITE);
  if (!file) {
    return false;
  }

  DynamicJsonDocument doc(16384);
  doc["version"] = 1;
  JsonArray files = doc.createNestedArray("files");
  for (size_t i = 0; i < s_entry_count; ++i) {
    JsonObject obj = files.createNestedObject();
    obj["path"] = s_entries[i].path;
    obj["bus"] = s_entries[i].bus_id;
    obj["start_ms"] = s_entries[i].start_ms;
    obj["size"] = s_entries[i].size_bytes;
    obj["flags"] = s_entries[i].flags;
  }

  serializeJson(doc, file);
  file.close();
  return true;
}

bool upsert_entry(const char* path,
                  uint8_t bus_id,
                  uint32_t start_ms,
                  uint32_t size_bytes,
                  uint8_t flags) {
  int index = find_entry(path);
  if (index < 0) {
    if (s_entry_count >= kMaxEntries) {
      return false;
    }
    index = static_cast<int>(s_entry_count++);
  }

  FileStatusEntry& entry = s_entries[index];
  strncpy(entry.path, path, sizeof(entry.path));
  entry.path[sizeof(entry.path) - 1] = '\0';
  entry.bus_id = bus_id;
  entry.start_ms = start_ms;
  entry.size_bytes = size_bytes;
  entry.flags = flags;
  return true;
}

uint8_t delete_priority(const FileStatusEntry& entry) {
  return (entry.flags & (kFlagDownloaded | kFlagUploaded)) ? 0 : 1;
}

int pick_deletion_candidate() {
  int best_index = -1;
  uint8_t best_priority = 0;
  uint32_t best_start = 0;
  for (size_t i = 0; i < s_entry_count; ++i) {
    const FileStatusEntry& entry = s_entries[i];
    if (entry.flags & kFlagActive) {
      continue;
    }
    const uint8_t priority = delete_priority(entry);
    if (best_index < 0 || priority < best_priority ||
        (priority == best_priority && entry.start_ms < best_start)) {
      best_index = static_cast<int>(i);
      best_priority = priority;
      best_start = entry.start_ms;
    }
  }
  return best_index;
}

bool parse_log_filename(const char* name, uint32_t* start_ms) {
  const char* base = strrchr(name, '/');
  base = base ? base + 1 : name;

  if (strncmp(base, "log_", 4) != 0) {
    return false;
  }

  const char* ms_start = base + 4;
  char* endptr = nullptr;
  unsigned long ms = strtoul(ms_start, &endptr, 10);
  if (endptr == ms_start || strncmp(endptr, "_bus", 4) != 0) {
    return false;
  }

  const char* bus_start = endptr + 4;
  char* endptr2 = nullptr;
  (void)strtoul(bus_start, &endptr2, 10);
  if (endptr2 == bus_start || strcmp(endptr2, ".sav") != 0) {
    return false;
  }

  if (start_ms != nullptr) {
    *start_ms = static_cast<uint32_t>(ms);
  }
  return true;
}

bool find_oldest_log_file(char* out, size_t out_len) {
  File root = SD.open("/");
  if (!root) {
    return false;
  }

  bool found = false;
  uint32_t best_start = 0;
  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }
    const char* name = file.name();
    uint32_t start_ms = 0;
    if (parse_log_filename(name, &start_ms)) {
      if (!found || start_ms < best_start) {
        found = true;
        best_start = start_ms;
        strncpy(out, name, out_len);
        out[out_len - 1] = '\0';
      }
    }
    file.close();
  }

  root.close();
  return found;
}

} // namespace

void init() {
  s_ready = false;

  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, s_sd_spi, SD_SPI_CLOCK_HZ, "/sd", kSdMaxFiles)) {
    return;
  }

  s_ready = true;

  for (uint8_t i = 0; i < 6; ++i) {
    char path[8];
    snprintf(path, sizeof(path), "/can%u", static_cast<unsigned>(i));
    ensure_dir(path);
  }
  ensure_dir(kMetaDir);

  if (!load_status()) {
    save_status();
  } else if (!SD.exists(kStatusPath)) {
    save_status();
  }
}

bool is_ready() {
  return s_ready;
}

Stats get_stats() {
  Stats stats{};
  if (!s_ready) {
    return stats;
  }

  stats.total_bytes = SD.totalBytes();
  const uint64_t used = SD.usedBytes();
  stats.free_bytes = stats.total_bytes >= used ? (stats.total_bytes - used) : 0;
  return stats;
}

bool ensure_space(uint64_t min_free_bytes) {
  if (!s_ready) {
    return false;
  }
  if (min_free_bytes == 0) {
    return true;
  }

  Stats stats = get_stats();
  if (stats.free_bytes >= min_free_bytes) {
    return true;
  }

  for (uint32_t guard = 0; guard < 64 && stats.free_bytes < min_free_bytes; ++guard) {
    int index = pick_deletion_candidate();
    if (index >= 0) {
      const char* path = s_entries[index].path;
      if (SD.exists(path)) {
        SD.remove(path);
      }
      remove_entry(static_cast<size_t>(index));
      save_status();
      stats = get_stats();
      continue;
    }

    char fallback[64];
    if (find_oldest_log_file(fallback, sizeof(fallback))) {
      SD.remove(fallback);
      stats = get_stats();
      continue;
    }
    break;
  }

  return stats.free_bytes >= min_free_bytes;
}

bool register_log_file(const char* path, uint8_t bus_id, uint32_t start_ms) {
  if (!s_ready || path == nullptr) {
    return false;
  }

  if (!upsert_entry(path, bus_id, start_ms, 0, kFlagActive)) {
    return false;
  }

  save_status();
  return true;
}

void finalize_log_file(const char* path, uint64_t size_bytes) {
  if (!s_ready || path == nullptr) {
    return;
  }

  int index = find_entry(path);
  if (index < 0) {
    return;
  }

  FileStatusEntry& entry = s_entries[index];
  entry.size_bytes = static_cast<uint32_t>(size_bytes);
  entry.flags &= static_cast<uint8_t>(~kFlagActive);
  save_status();
}

void mark_downloaded(const char* path) {
  if (!s_ready || path == nullptr) {
    return;
  }

  int index = find_entry(path);
  if (index < 0) {
    if (!upsert_entry(path, 0, 0, 0, kFlagDownloaded)) {
      return;
    }
  } else {
    s_entries[index].flags |= kFlagDownloaded;
  }
  save_status();
}

void mark_uploaded(const char* path) {
  if (!s_ready || path == nullptr) {
    return;
  }

  int index = find_entry(path);
  if (index < 0) {
    if (!upsert_entry(path, 0, 0, 0, kFlagUploaded)) {
      return;
    }
  } else {
    s_entries[index].flags |= kFlagUploaded;
  }
  save_status();
}

} // namespace storage
