#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include <stdint.h>

namespace storage {

struct Stats {
  uint64_t total_bytes;
  uint64_t free_bytes;
};

void init();
bool is_ready();
Stats get_stats();

bool ensure_space(uint64_t min_free_bytes);
bool register_log_file(const char* path, uint8_t bus_id, uint32_t start_ms);
void finalize_log_file(const char* path, uint64_t size_bytes);
void mark_downloaded(const char* path);
void mark_uploaded(const char* path);

} // namespace storage

#endif // STORAGE_MANAGER_H
