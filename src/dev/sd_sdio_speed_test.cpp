#ifdef SD_SDIO_SPEED_TEST

#include <Arduino.h>
#include <SD_MMC.h>

#include "hardware/hardware_config.h"

namespace {

constexpr uint32_t kTestBytes = 16UL * 1024UL * 1024UL;
constexpr size_t kBlockSizes[] = {4096, 8192, 16384, 32768, 65536};
constexpr bool kPreallocateOptions[] = {true, false};
constexpr size_t kMaxBlockSize = 65536;

// Default SDIO width for speed tests: 4-bit mode.
constexpr bool kUseOneBitMode = false;
constexpr bool kFormatIfMountFailed = false;

static uint8_t s_buffer[kMaxBlockSize];

void print_result(uint32_t bytes, uint32_t elapsed_ms) {
  const float mb = static_cast<float>(bytes) / (1024.0f * 1024.0f);
  const float sec = static_cast<float>(elapsed_ms) / 1000.0f;
  const float mbps = (sec > 0.0f) ? (mb / sec) : 0.0f;
  Serial.print("Wrote ");
  Serial.print(mb, 2);
  Serial.print(" MB in ");
  Serial.print(sec, 2);
  Serial.print(" s (");
  Serial.print(mbps, 2);
  Serial.println(" MB/s)");
}

bool init_sd_mmc() {
  SD_MMC.end();
  delay(50);

  const bool customPinsConfigured =
      SDIO_CLK_PIN >= 0 && SDIO_CMD_PIN >= 0 && SDIO_D0_PIN >= 0 &&
      SDIO_D1_PIN >= 0 && SDIO_D2_PIN >= 0 && SDIO_D3_PIN >= 0;

  if (customPinsConfigured) {
    if (!SD_MMC.setPins(SDIO_CLK_PIN, SDIO_CMD_PIN, SDIO_D0_PIN, SDIO_D1_PIN,
                        SDIO_D2_PIN, SDIO_D3_PIN)) {
      Serial.println("SD_MMC.setPins failed.");
      return false;
    }
    Serial.printf("SDIO pins CLK=%d CMD=%d D0=%d D1=%d D2=%d D3=%d\n",
                  SDIO_CLK_PIN, SDIO_CMD_PIN, SDIO_D0_PIN, SDIO_D1_PIN,
                  SDIO_D2_PIN, SDIO_D3_PIN);
  } else {
    Serial.println("SDIO pinout: board defaults (configure in hardware_config.h)");
  }

  return SD_MMC.begin("/sdcard", kUseOneBitMode, kFormatIfMountFailed);
}

float run_write_test(size_t block_size, bool preallocate, uint32_t* out_bytes,
                     uint32_t* out_elapsed_ms) {
  SD_MMC.remove("/speed_test_sdio.bin");
  File file = SD_MMC.open("/speed_test_sdio.bin", FILE_WRITE);
  if (!file) {
    Serial.println("Open file failed.");
    return 0.0f;
  }

  if (preallocate && kTestBytes > 0) {
    if (file.seek(kTestBytes - 1)) {
      file.write(static_cast<uint8_t>(0));
      file.flush();
      file.seek(0);
    }
  }

  uint32_t written = 0;
  const uint32_t start_ms = millis();
  while (written < kTestBytes) {
    const size_t to_write = (kTestBytes - written) < block_size
                                ? static_cast<size_t>(kTestBytes - written)
                                : block_size;
    const size_t out = file.write(s_buffer, to_write);
    if (out != to_write) {
      Serial.println("Write failed.");
      break;
    }
    written += static_cast<uint32_t>(out);
  }
  file.flush();
  file.close();

  const uint32_t elapsed_ms = millis() - start_ms;
  if (out_bytes) {
    *out_bytes = written;
  }
  if (out_elapsed_ms) {
    *out_elapsed_ms = elapsed_ms;
  }

  const float mb = static_cast<float>(written) / (1024.0f * 1024.0f);
  const float sec = static_cast<float>(elapsed_ms) / 1000.0f;
  return (sec > 0.0f) ? (mb / sec) : 0.0f;
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("SD_MMC (SDIO) write speed test sweep");
  Serial.println("Mode: 4-bit SDIO");

  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    s_buffer[i] = static_cast<uint8_t>(i & 0xFF);
  }

  if (!init_sd_mmc()) {
    Serial.println("SD_MMC.begin failed.");
    Serial.println("Check SDIO pin wiring and pull-up resistors.");
    return;
  }

  float best_mbps = 0.0f;
  size_t best_block = 0;
  bool best_prealloc = false;

  for (const bool preallocate : kPreallocateOptions) {
    for (const size_t block_size : kBlockSizes) {
      if (block_size > kMaxBlockSize) {
        continue;
      }

      Serial.print("SDIO block ");
      Serial.print(block_size);
      Serial.print(", prealloc ");
      Serial.print(preallocate ? "yes" : "no");
      Serial.print(": ");

      uint32_t written = 0;
      uint32_t elapsed_ms = 0;
      const float mbps =
          run_write_test(block_size, preallocate, &written, &elapsed_ms);
      print_result(written, elapsed_ms);

      if (mbps > best_mbps) {
        best_mbps = mbps;
        best_block = block_size;
        best_prealloc = preallocate;
      }

      delay(50);
    }
  }

  Serial.println();
  Serial.print("Best: block ");
  Serial.print(best_block);
  Serial.print(", prealloc ");
  Serial.print(best_prealloc ? "yes" : "no");
  Serial.print(" => ");
  Serial.print(best_mbps, 2);
  Serial.println(" MB/s");
  Serial.println("Done.");
}

void loop() {
  delay(1000);
}

#endif // SD_SDIO_SPEED_TEST
