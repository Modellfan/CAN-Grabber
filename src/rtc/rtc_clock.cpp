#include "rtc/rtc_clock.h"

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>
#include <sys/time.h>

#include "config/app_config.h"
#include "hardware/hardware_config.h"

namespace rtc_clock {

namespace {

RTC_DS1307 s_rtc;
bool s_initialized = false;
bool s_wire_started = false;
bool s_available = false;
bool s_running = false;
bool s_valid = false;
uint32_t s_base_unix_sec = 0;
uint32_t s_base_millis = 0;
Source s_source = Source::kUnavailable;

void sync_system_time(uint32_t epoch_sec, uint32_t millis_part) {
  timeval tv{};
  tv.tv_sec = static_cast<time_t>(epoch_sec);
  tv.tv_usec = static_cast<suseconds_t>(millis_part * 1000UL);
  settimeofday(&tv, nullptr);
}

void apply_epoch(uint32_t epoch_sec, Source source_value) {
  s_base_unix_sec = epoch_sec;
  s_base_millis = millis();
  s_source = source_value;
  s_valid = (epoch_sec != 0);
  sync_system_time(epoch_sec, 0);
}

void ensure_wire_started() {
  if (s_wire_started) {
    return;
  }

  if (I2C_SDA_PIN >= 0 && I2C_SCL_PIN >= 0) {
    Wire.begin(static_cast<int>(I2C_SDA_PIN), static_cast<int>(I2C_SCL_PIN));
    Serial.printf("[rtc] I2C started on SDA=%d, SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
  } else {
    Wire.begin();
    Serial.println("[rtc] I2C started on board defaults");
  }
  s_wire_started = true;
}

uint32_t build_time_epoch() {
  const DateTime build_time(F(__DATE__), F(__TIME__));
  return build_time.isValid() ? build_time.unixtime() : 0;
}

bool adopt_rtc_time() {
  if (!s_available || !s_running) {
    return false;
  }

  const DateTime now = s_rtc.now();
  if (!now.isValid()) {
    return false;
  }

  const uint32_t epoch_sec = now.unixtime();
  if (epoch_sec == 0) {
    return false;
  }

  apply_epoch(epoch_sec, Source::kRtc);
  Serial.printf("[rtc] Startup epoch from RTC: %lu\n",
                static_cast<unsigned long>(epoch_sec));
  return true;
}

void adopt_fallback_epoch(uint32_t epoch_sec,
                          Source source_value,
                          const char* source_label,
                          bool update_rtc) {
  if (epoch_sec == 0) {
    return;
  }

  apply_epoch(epoch_sec, source_value);
  Serial.printf("[rtc] Startup epoch from %s: %lu\n",
                source_label != nullptr ? source_label : "fallback",
                static_cast<unsigned long>(epoch_sec));

  if (update_rtc && s_available) {
    s_rtc.adjust(DateTime(epoch_sec));
    s_running = true;
    Serial.println("[rtc] RTC updated from fallback source");
  }
}

} // namespace

void init() {
  if (s_initialized) {
    return;
  }
  s_initialized = true;

  ensure_wire_started();

  s_available = s_rtc.begin();
  s_running = s_available && s_rtc.isrunning();
  if (s_available) {
    Serial.println("[rtc] RTC_DS1307 detected");
  } else {
    Serial.println("[rtc] RTC_DS1307 not found");
  }

  if (adopt_rtc_time()) {
    return;
  }

  const int64_t manual_epoch = config::get().global.manual_time_epoch;
  if (manual_epoch > 0 && manual_epoch <= 0xFFFFFFFFLL) {
    adopt_fallback_epoch(static_cast<uint32_t>(manual_epoch),
                         Source::kManualConfig,
                         "config",
                         true);
    return;
  }

  const uint32_t build_epoch = build_time_epoch();
  if (build_epoch != 0) {
    adopt_fallback_epoch(build_epoch, Source::kBuildTime, "build time", false);
    return;
  }

  s_base_unix_sec = 0;
  s_base_millis = millis();
  s_source = Source::kUnavailable;
  s_valid = false;
  sync_system_time(0, 0);
}

bool is_available() {
  return s_available;
}

bool is_running() {
  return s_running;
}

bool is_valid() {
  return s_valid;
}

uint32_t now_unix_sec() {
  if (!s_valid) {
    return 0;
  }
  const uint32_t elapsed_ms = millis() - s_base_millis;
  return s_base_unix_sec + (elapsed_ms / 1000UL);
}

uint64_t now_unix_ms() {
  if (!s_valid) {
    return 0;
  }
  const uint32_t elapsed_ms = millis() - s_base_millis;
  return (static_cast<uint64_t>(s_base_unix_sec) * 1000ULL) +
         static_cast<uint64_t>(elapsed_ms);
}

uint64_t now_unix_us() {
  if (!s_valid) {
    return 0;
  }
  return now_unix_ms() * 1000ULL;
}

bool set_unix_epoch(uint32_t epoch_sec, bool update_rtc) {
  if (!s_initialized) {
    init();
  }
  if (epoch_sec == 0) {
    return false;
  }

  apply_epoch(epoch_sec,
              (update_rtc && s_available) ? Source::kRtc : Source::kManualConfig);
  if (update_rtc && s_available) {
    s_rtc.adjust(DateTime(epoch_sec));
    s_running = true;
    Serial.printf("[rtc] RTC updated to unix=%lu\n",
                  static_cast<unsigned long>(epoch_sec));
  }
  return true;
}

Source source() {
  return s_source;
}

const char* source_name() {
  switch (s_source) {
    case Source::kRtc:
      return "rtc";
    case Source::kManualConfig:
      return "config";
    case Source::kBuildTime:
      return "build";
    case Source::kUnavailable:
    default:
      return "unavailable";
  }
}

} // namespace rtc_clock
