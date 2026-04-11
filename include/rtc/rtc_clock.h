#ifndef RTC_CLOCK_H
#define RTC_CLOCK_H

#include <stdint.h>

namespace rtc_clock {

enum class Source : uint8_t {
  kUnavailable = 0,
  kRtc,
  kManualConfig,
  kBuildTime,
};

void init();

bool is_available();
bool is_running();
bool is_valid();

uint32_t now_unix_sec();
uint64_t now_unix_ms();
uint64_t now_unix_us();

bool set_unix_epoch(uint32_t epoch_sec, bool update_rtc);

Source source();
const char* source_name();

} // namespace rtc_clock

#endif // RTC_CLOCK_H
