#if defined(CAN_TO_FILE_MODULES) && !ENABLE_COMPRESSOR

#include "compress/compressor.h"

namespace compressor {

void init() {}

Stats get_stats() {
  Stats stats{};
  return stats;
}

} // namespace compressor

#endif
