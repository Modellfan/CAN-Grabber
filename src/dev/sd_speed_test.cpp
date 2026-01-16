#ifdef SD_SPEED_TEST

#include <Arduino.h>
#include <SD.h>
#include <SPI.h>

#include "hardware/hardware_config.h"

namespace {

SPIClass s_sd_spi(HSPI);

constexpr size_t kBufferSize = 32768;
constexpr uint32_t kTestBytes = 16UL * 1024UL * 1024UL;
constexpr uint32_t kSdSpiClockHz = 20000000UL;

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

} // namespace

void setup() {
  Serial.begin(115200);
  delay(200);

  Serial.println();
  Serial.println("SD write speed test");

  s_sd_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (!SD.begin(SD_CS_PIN, s_sd_spi, kSdSpiClockHz)) {
    Serial.println("SD.begin failed.");
    for (;;) {
      delay(1000);
    }
  }

  SD.remove("/speed_test.bin");
  File file = SD.open("/speed_test.bin", FILE_WRITE);
  if (!file) {
    Serial.println("Open file failed.");
    for (;;) {
      delay(1000);
    }
  }

  if (kTestBytes > 0) {
    if (file.seek(kTestBytes - 1)) {
      file.write(static_cast<uint8_t>(0));
      file.flush();
      file.seek(0);
    }
  }

  static uint8_t buffer[kBufferSize];
  for (size_t i = 0; i < kBufferSize; ++i) {
    buffer[i] = static_cast<uint8_t>(i & 0xFF);
  }

  uint32_t written = 0;
  const uint32_t start_ms = millis();
  while (written < kTestBytes) {
    const size_t to_write = (kTestBytes - written) < kBufferSize
                                ? static_cast<size_t>(kTestBytes - written)
                                : kBufferSize;
    const size_t out = file.write(buffer, to_write);
    if (out != to_write) {
      Serial.println("Write failed.");
      break;
    }
    written += static_cast<uint32_t>(out);
  }
  file.flush();
  file.close();

  const uint32_t elapsed_ms = millis() - start_ms;
  print_result(written, elapsed_ms);
  Serial.println("Done.");
}

void loop() {
  delay(1000);
}

#endif // SD_SPEED_TEST
