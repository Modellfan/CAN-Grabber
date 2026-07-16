#ifdef SD_SPEED_TEST

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "hardware/hardware_config.h"

namespace {

SPIClass s_sd_spi(HSPI);

constexpr uint32_t kTestBytes = 16UL * 1024UL * 1024UL;
constexpr uint32_t kSpiMinMHz = 10;
constexpr uint32_t kSpiMaxMHz = 50;
constexpr uint32_t kSpiStepMHz = 5;
constexpr size_t kBlockSizes[] = {4096, 8192, 16384, 32768, 65536};
constexpr bool kPreallocateOptions[] = {true, false};
constexpr size_t kMaxBlockSize = 65536;

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

bool init_sd(uint32_t spi_clock_hz) {
  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, s_sd_spi, spi_clock_hz)) {
    return false;
  }
  return true;
}

float run_write_test(size_t block_size, bool preallocate, uint32_t* out_bytes,
                     uint32_t* out_elapsed_ms) {
  SD.remove("/speed_test.bin");
  File file = SD.open("/speed_test.bin", FILE_WRITE);
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
  Serial.println("SD write speed test sweep");

  for (size_t i = 0; i < kMaxBlockSize; ++i) {
    s_buffer[i] = static_cast<uint8_t>(i & 0xFF);
  }

  float best_mbps = 0.0f;
  uint32_t best_spi_hz = 0;
  size_t best_block = 0;
  bool best_prealloc = false;

  for (uint32_t mhz = kSpiMinMHz; mhz <= kSpiMaxMHz; mhz += kSpiStepMHz) {
    const uint32_t spi_hz = mhz * 1000000UL;
    SD.end();
    delay(50);
    if (!init_sd(spi_hz)) {
      Serial.print("SD.begin failed at ");
      Serial.print(mhz);
      Serial.println(" MHz.");
      continue;
    }

    for (const bool preallocate : kPreallocateOptions) {
      for (const size_t block_size : kBlockSizes) {
        if (block_size > kMaxBlockSize) {
          continue;
        }

        Serial.print("SPI ");
        Serial.print(mhz);
        Serial.print(" MHz, block ");
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
          best_spi_hz = spi_hz;
          best_block = block_size;
          best_prealloc = preallocate;
        }

        delay(50);
      }
    }
  }

  Serial.println();
  Serial.print("Best: ");
  Serial.print(best_spi_hz / 1000000UL);
  Serial.print(" MHz, block ");
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

#endif // SD_SPEED_TEST
