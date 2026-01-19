#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stddef.h>
#include <stdint.h>

namespace storage {

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
Stats get_stats();
size_t file_count();
bool get_file_info(size_t index, FileInfo* out);

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
