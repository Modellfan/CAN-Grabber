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

} // namespace storage

#endif // STORAGE_MANAGER_H
