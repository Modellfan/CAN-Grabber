#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

namespace fs {
class FS;
}

namespace storage {

constexpr uint8_t kFlagDownloaded = 1u << 0;
constexpr uint8_t kFlagUploaded = 1u << 1;
constexpr uint8_t kFlagActive = 1u << 2;

struct Stats {
  uint64_t total_bytes;
  uint64_t free_bytes;
};

struct FileInfo {
  char path[64];
  uint32_t start_ms;
  uint32_t end_ms;
  uint32_t size_bytes;
  uint32_t checksum;
  uint8_t bus_id;
  uint8_t flags;
};

void init();
bool is_ready();
fs::FS& card();
Stats get_stats();
size_t file_count();
bool get_file_info(size_t index, FileInfo* out);
bool find_file_info(const char* path, FileInfo* out, size_t* out_index);
bool delete_file(size_t index);

bool ensure_space(uint64_t min_free_bytes);
bool register_log_file(const char* path, uint8_t bus_id, uint32_t start_ms);
void finalize_log_file(const char* path,
                       uint64_t size_bytes,
                       uint32_t end_ms,
                       uint32_t checksum);
void mark_downloaded(const char* path);
void mark_uploaded(const char* path);

} // namespace storage

#endif // STORAGE_MANAGER_H
