#ifndef COMPRESSOR_H
#define COMPRESSOR_H

#include <stdint.h>

namespace compressor {

struct Stats {
  bool active;
  uint32_t current_input_done_bytes;
  uint32_t current_input_total_bytes;
  uint32_t outstanding_files;
  uint64_t outstanding_bytes;
  uint32_t compressed_files_total;
  uint64_t compressed_input_bytes_total;
};

void init();
Stats get_stats();

} // namespace compressor

#endif // COMPRESSOR_H
