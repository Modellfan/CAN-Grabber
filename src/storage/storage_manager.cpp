#include "storage/storage_manager.h"

#include <Arduino.h>
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
constexpr char kCompressedDir[] = "/cmp";
constexpr uint8_t kSdMaxFiles = 12;
constexpr size_t kMaxEntries = 128;

struct FileStatusEntry {
  char path[64];
  uint32_t start_ms;
  uint32_t end_ms;
  uint32_t size_bytes;
  uint32_t checksum;
  uint8_t bus_id;
  uint8_t flags;
};

FileStatusEntry s_entries[kMaxEntries];
size_t s_entry_count = 0;

bool parse_bus_id_from_log_path(const char* path, uint8_t* bus_id) {
  if (path == nullptr || bus_id == nullptr) {
    return false;
  }
  const char* bus = strstr(path, "_bus");
  if (bus == nullptr) {
    return false;
  }
  bus += 4;
  if (*bus < '0' || *bus > '9') {
    return false;
  }
  char* endptr = nullptr;
  const unsigned long parsed = strtoul(bus, &endptr, 10);
  if (endptr == bus || parsed > 255) {
    return false;
  }
  if (*endptr != '_' && *endptr != '.') {
    return false;
  }
  *bus_id = static_cast<uint8_t>(parsed);
  return true;
}

bool build_compressed_sidecar_path(const char* src, char* out, size_t out_len) {
  if (src == nullptr || out == nullptr || out_len == 0) {
    return false;
  }
  const char* base = strrchr(src, '/');
  base = base ? (base + 1) : src;
  const int n = snprintf(out, out_len, "%s/%s.mz", kCompressedDir, base);
  if (n <= 0 || static_cast<size_t>(n) >= out_len) {
    out[0] = '\0';
    return false;
  }
  return true;
}

bool normalize_abs_path(const char* in, char* out, size_t out_len) {
  if (in == nullptr || out == nullptr || out_len == 0) {
    return false;
  }
  if (in[0] == '/') {
    strncpy(out, in, out_len);
    out[out_len - 1] = '\0';
    return true;
  }
  const int n = snprintf(out, out_len, "/%s", in);
  if (n <= 0 || static_cast<size_t>(n) >= out_len) {
    out[0] = '\0';
    return false;
  }
  return true;
}

void remove_compressed_sidecar(const char* src_path) {
  char cmp[96];
  if (!build_compressed_sidecar_path(src_path, cmp, sizeof(cmp))) {
    return;
  }
  if (SD.exists(cmp)) {
    SD.remove(cmp);
  }
}

// Clear stale "active" flags after reboot and stamp a closed time if missing.
bool clear_active_flags_on_boot() {
  bool changed = false;
  for (size_t i = 0; i < s_entry_count; ++i) {
    FileStatusEntry& entry = s_entries[i];
    if (entry.flags & kFlagActive) {
      entry.flags &= static_cast<uint8_t>(~kFlagActive);
      if (entry.end_ms == 0) {
        entry.end_ms = entry.start_ms;
      }
      changed = true;
    }
  }
  return changed;
}

// Create a directory if it does not already exist.
void ensure_dir(const char* path) {
  if (!SD.exists(path)) {
    SD.mkdir(path);
  }
}

// Return the index of the status entry matching the path, or -1 if missing.
int find_entry(const char* path) {
  for (size_t i = 0; i < s_entry_count; ++i) {
    if (strcmp(s_entries[i].path, path) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

// Remove a status entry by index and compact the array.
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

// Load file status metadata from disk into memory.
bool load_status() {
  s_entry_count = 0;
  if (!SD.exists(kStatusPath)) {
    return true;
  }

  File file = SD.open(kStatusPath, FILE_READ);
  if (!file) {
    return false;
  }

  JsonDocument doc;
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
    uint8_t bus_from_name = 0;
    if (parse_bus_id_from_log_path(entry.path, &bus_from_name)) {
      // Normalize legacy metadata that stored bus as 1-based while filenames are bus0..bus5.
      entry.bus_id = bus_from_name;
    }
    entry.start_ms = static_cast<uint32_t>(obj["start_ms"] | 0);
    entry.end_ms = static_cast<uint32_t>(obj["end_ms"] | 0);
    entry.size_bytes = static_cast<uint32_t>(obj["size"] | 0);
    entry.checksum = static_cast<uint32_t>(obj["checksum"] | 0);
    entry.flags = static_cast<uint8_t>(obj["flags"] | 0);
  }

  return true;
}

// Persist current file status metadata to disk.
bool save_status() {
  ensure_dir(kMetaDir);
  if (SD.exists(kStatusPath)) {
    SD.remove(kStatusPath);
  }
  File file = SD.open(kStatusPath, FILE_WRITE);
  if (!file) {
    return false;
  }

  JsonDocument doc;
  doc["version"] = 1;
  JsonArray files = doc["files"].to<JsonArray>();
  for (size_t i = 0; i < s_entry_count; ++i) {
    JsonObject obj = files.add<JsonObject>();
    obj["path"] = s_entries[i].path;
    obj["bus"] = s_entries[i].bus_id;
    obj["start_ms"] = s_entries[i].start_ms;
    obj["end_ms"] = s_entries[i].end_ms;
    obj["size"] = s_entries[i].size_bytes;
    obj["checksum"] = s_entries[i].checksum;
    obj["flags"] = s_entries[i].flags;
  }

  serializeJson(doc, file);
  file.close();
  return true;
}

// Insert or update a status entry for a log file.
bool upsert_entry(const char* path,
                  uint8_t bus_id,
                  uint32_t start_ms,
                  uint32_t end_ms,
                  uint32_t size_bytes,
                  uint32_t checksum,
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
  entry.end_ms = end_ms;
  entry.size_bytes = size_bytes;
  entry.checksum = checksum;
  entry.flags = flags;
  return true;
}

// Rank deletion priority; lower values are deleted first.
uint8_t delete_priority(const FileStatusEntry& entry) {
  return (entry.flags & (kFlagDownloaded | kFlagUploaded)) ? 0 : 1;
}

// Choose a non-active file to delete, prioritizing downloaded/uploaded and oldest.
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

// Parse log filename and extract start timestamp if it matches expected pattern.
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

// Scan the root directory for the oldest log filename.
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

bool is_log_file_path(const char* path) {
  if (path == nullptr) {
    return false;
  }
  const char* base = strrchr(path, '/');
  base = base ? base + 1 : path;
  if (strncmp(base, "log_", 4) != 0) {
    return false;
  }
  const size_t len = strlen(base);
  return (len >= 4) && (strcmp(base + (len - 4), ".sav") == 0);
}

// Remove stale placeholder logs left behind by unclean shutdown:
// metadata says zero payload and no proper end timestamp.
bool remove_stale_placeholder_logs_on_boot() {
  bool changed = false;
  for (size_t i = 0; i < s_entry_count;) {
    const FileStatusEntry& entry = s_entries[i];
    if (entry.size_bytes != 0 || entry.end_ms != entry.start_ms) {
      ++i;
      continue;
    }
    if (!is_log_file_path(entry.path)) {
      ++i;
      continue;
    }

    if (SD.exists(entry.path)) {
      SD.remove(entry.path);
    }
    remove_compressed_sidecar(entry.path);
    remove_entry(i);
    changed = true;
  }
  return changed;
}

bool remove_empty_logs_on_boot() {
  bool changed = false;

  for (size_t i = 0; i < s_entry_count;) {
    const FileStatusEntry entry = s_entries[i];
    if (!is_log_file_path(entry.path) || !SD.exists(entry.path)) {
      ++i;
      continue;
    }

    File file = SD.open(entry.path, FILE_READ);
    if (!file) {
      ++i;
      continue;
    }

    const size_t size = file.size();
    file.close();
    if (size != 0) {
      ++i;
      continue;
    }

    SD.remove(entry.path);
    remove_compressed_sidecar(entry.path);
    remove_entry(i);
    changed = true;
  }

  File root = SD.open("/");
  if (!root) {
    return changed;
  }

  for (File file = root.openNextFile(); file; file = root.openNextFile()) {
    if (file.isDirectory()) {
      file.close();
      continue;
    }

    const String name = file.name();
    char abs_path[96];
    if (!normalize_abs_path(name.c_str(), abs_path, sizeof(abs_path))) {
      file.close();
      continue;
    }
    const bool empty_log = is_log_file_path(abs_path) && (file.size() == 0);
    file.close();
    if (!empty_log) {
      continue;
    }

    SD.remove(abs_path);
    remove_compressed_sidecar(abs_path);
    const int index = find_entry(abs_path);
    if (index >= 0) {
      remove_entry(static_cast<size_t>(index));
    }
    changed = true;
  }

  root.close();
  return changed;
}

} // namespace

// Initialize the SD card, folders, and file status database.
void init() {
  s_ready = false;

  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  const uint32_t clocks[] = {SD_SPI_CLOCK_HZ, 10000000UL, 4000000UL};
  bool mounted = false;
  for (size_t i = 0; i < (sizeof(clocks) / sizeof(clocks[0])); ++i) {
    if (i > 0) {
      SD.end();
      delay(50);
    }
    if (SD.begin(SD_CS_PIN, s_sd_spi, clocks[i], "/sd", kSdMaxFiles)) {
      mounted = true;
      Serial.printf("[storage] SD mounted at %lu Hz\n",
                    static_cast<unsigned long>(clocks[i]));
      break;
    }
  }
  if (!mounted) {
    Serial.println("[storage] SD mount failed on all clock fallbacks");
    return;
  }

  s_ready = true;

  for (uint8_t i = 0; i < 6; ++i) {
    char path[8];
    snprintf(path, sizeof(path), "/can%u", static_cast<unsigned>(i));
    ensure_dir(path);
  }
  ensure_dir(kMetaDir);

  bool metadata_changed = false;
  if (!load_status()) {
    save_status();
  } else if (!SD.exists(kStatusPath)) {
    save_status();
  } else if (clear_active_flags_on_boot()) {
    metadata_changed = true;
  }

  if (remove_empty_logs_on_boot()) {
    metadata_changed = true;
  }
  if (remove_stale_placeholder_logs_on_boot()) {
    metadata_changed = true;
  }

  if (metadata_changed) {
    save_status();
  }
}

// Report whether storage is initialized and ready for use.
bool is_ready() {
  return s_ready;
}

// Read SD capacity and free space stats.
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

size_t file_count() {
  return s_entry_count;
}

bool get_file_info(size_t index, FileInfo* out) {
  if (index >= s_entry_count || out == nullptr) {
    return false;
  }
  const FileStatusEntry& entry = s_entries[index];
  strncpy(out->path, entry.path, sizeof(out->path));
  out->path[sizeof(out->path) - 1] = '\0';
  out->start_ms = entry.start_ms;
  out->end_ms = entry.end_ms;
  out->size_bytes = entry.size_bytes;
  out->checksum = entry.checksum;
  out->bus_id = entry.bus_id;
  out->flags = entry.flags;
  return true;
}

bool find_file_info(const char* path, FileInfo* out, size_t* out_index) {
  if (path == nullptr || out == nullptr) {
    return false;
  }
  for (size_t i = 0; i < s_entry_count; ++i) {
    if (strcmp(s_entries[i].path, path) != 0) {
      continue;
    }
    if (out_index != nullptr) {
      *out_index = i;
    }
    return get_file_info(i, out);
  }
  return false;
}

// Ensure at least min_free_bytes are free, deleting old files if needed.
bool ensure_space(uint64_t min_free_bytes) {
  if (!s_ready) {
    return false;
  }
  const uint64_t threshold = config::get().global.low_space_threshold_bytes;
  uint64_t required = min_free_bytes;
  if (threshold > required) {
    required = threshold;
  }
  if (required == 0) {
    return true;
  }

  Stats stats = get_stats();
  if (stats.free_bytes >= required) {
    return true;
  }

  for (uint32_t guard = 0; guard < 64 && stats.free_bytes < required; ++guard) {
    int index = pick_deletion_candidate();
    if (index >= 0) {
      const char* path = s_entries[index].path;
      if (SD.exists(path)) {
        SD.remove(path);
      }
      remove_compressed_sidecar(path);
      remove_entry(static_cast<size_t>(index));
      save_status();
      stats = get_stats();
      continue;
    }

    char fallback[64];
    if (find_oldest_log_file(fallback, sizeof(fallback))) {
      SD.remove(fallback);
      remove_compressed_sidecar(fallback);
      stats = get_stats();
      continue;
    }
    break;
  }

  return stats.free_bytes >= required;
}

// Record a newly opened log file as active.
bool register_log_file(const char* path, uint8_t bus_id, uint32_t start_ms) {
  if (!s_ready || path == nullptr) {
    return false;
  }

  if (!upsert_entry(path, bus_id, start_ms, 0, 0, 0, kFlagActive)) {
    return false;
  }

  save_status();
  return true;
}

// Mark a log file as closed and record its final size.
void finalize_log_file(const char* path,
                       uint64_t size_bytes,
                       uint32_t end_ms,
                       uint32_t checksum) {
  if (!s_ready || path == nullptr) {
    return;
  }

  int index = find_entry(path);
  if (index < 0) {
    return;
  }

  FileStatusEntry& entry = s_entries[index];
  entry.size_bytes = static_cast<uint32_t>(size_bytes);
  entry.end_ms = end_ms;
  entry.checksum = checksum;
  entry.flags &= static_cast<uint8_t>(~kFlagActive);
  save_status();
}

// Mark a file as downloaded in the metadata table.
void mark_downloaded(const char* path) {
  if (!s_ready || path == nullptr) {
    return;
  }

  int index = find_entry(path);
  if (index < 0) {
    if (!upsert_entry(path, 0, 0, 0, 0, 0, kFlagDownloaded)) {
      return;
    }
  } else {
    s_entries[index].flags |= kFlagDownloaded;
  }
  save_status();
}

// Mark a file as uploaded in the metadata table.
void mark_uploaded(const char* path) {
  if (!s_ready || path == nullptr) {
    return;
  }

  int index = find_entry(path);
  if (index < 0) {
    if (!upsert_entry(path, 0, 0, 0, 0, 0, kFlagUploaded)) {
      return;
    }
  } else {
    s_entries[index].flags |= kFlagUploaded;
  }
  save_status();
}

bool delete_file(size_t index) {
  if (!s_ready || index >= s_entry_count) {
    return false;
  }

  const FileStatusEntry entry = s_entries[index];
  if (entry.flags & kFlagActive) {
    return false;
  }

  if (SD.exists(entry.path) && !SD.remove(entry.path)) {
    return false;
  }
  remove_compressed_sidecar(entry.path);

  remove_entry(index);
  save_status();
  return true;
}

} // namespace storage
