#if defined(RTC_MODULE_TEST)

#include <Arduino.h>
#include <RTClib.h>
#include <Wire.h>

#include "hardware/hardware_config.h"

namespace {

RTC_DS3231 rtc;
String inputLine;
bool rtcPresent = false;

bool parseDateTime(const String &value, DateTime &dt) {
  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;

  int parsed =
      sscanf(value.c_str(), "%d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second);

  if (parsed != 6) {
    return false;
  }

  if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 ||
      hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 59) {
    return false;
  }

  dt = DateTime(year, month, day, hour, minute, second);
  return dt.isValid();
}

void printNow() {
  if (!rtcPresent) {
    Serial.println("ERR: RTC not available");
    return;
  }

  DateTime now = rtc.now();
  Serial.printf("RTC now: %04d-%02d-%02d %02d:%02d:%02d (unix=%lu)\n",
                now.year(),
                now.month(),
                now.day(),
                now.hour(),
                now.minute(),
                now.second(),
                static_cast<unsigned long>(now.unixtime()));
}

void printHelp() {
  Serial.println("RTC command list:");
  Serial.println("  help");
  Serial.println("  read");
  Serial.println("  lost");
  Serial.println("  temp");
  Serial.println("  set YYYY-MM-DD HH:MM:SS");
  Serial.println("  setunix <unix_seconds>");
  Serial.println("  unix");
  Serial.println("  sync_build");
}

void handleCommand(const String &line) {
  if (line.length() == 0) {
    return;
  }

  if (line == "help") {
    printHelp();
    return;
  }

  if (line == "read") {
    printNow();
    return;
  }

  if (line == "lost") {
    if (!rtcPresent) {
      Serial.println("ERR: RTC not available");
      return;
    }
    Serial.printf("lostPower: %s\n", rtc.lostPower() ? "true" : "false");
    return;
  }

  if (line == "temp") {
    if (!rtcPresent) {
      Serial.println("ERR: RTC not available");
      return;
    }
    Serial.printf("temperature: %.2f C\n", rtc.getTemperature());
    return;
  }

  if (line == "unix") {
    if (!rtcPresent) {
      Serial.println("ERR: RTC not available");
      return;
    }
    DateTime now = rtc.now();
    Serial.printf("unix: %lu\n", static_cast<unsigned long>(now.unixtime()));
    return;
  }

  if (line == "sync_build") {
    if (!rtcPresent) {
      Serial.println("ERR: RTC not available");
      return;
    }
    DateTime buildTime(F(__DATE__), F(__TIME__));
    rtc.adjust(buildTime);
    Serial.println("RTC set to firmware build time.");
    printNow();
    return;
  }

  if (line.startsWith("setunix ")) {
    if (!rtcPresent) {
      Serial.println("ERR: RTC not available");
      return;
    }

    uint32_t ts = static_cast<uint32_t>(strtoul(line.substring(8).c_str(), nullptr, 10));
    rtc.adjust(DateTime(ts));
    Serial.println("RTC updated from unix timestamp.");
    printNow();
    return;
  }

  if (line.startsWith("set ")) {
    if (!rtcPresent) {
      Serial.println("ERR: RTC not available");
      return;
    }

    DateTime newTime;
    String value = line.substring(4);
    if (!parseDateTime(value, newTime)) {
      Serial.println("ERR: invalid format. Use: set YYYY-MM-DD HH:MM:SS");
      return;
    }
    rtc.adjust(newTime);
    Serial.println("RTC updated.");
    printNow();
    return;
  }

  Serial.println("ERR: unknown command. Type 'help'.");
}

} // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  if (I2C_SDA_PIN >= 0 && I2C_SCL_PIN >= 0) {
    Wire.begin(static_cast<int>(I2C_SDA_PIN), static_cast<int>(I2C_SCL_PIN));
    Serial.printf("I2C started on SDA=%d, SCL=%d\n", I2C_SDA_PIN, I2C_SCL_PIN);
  } else {
    Wire.begin();
    Serial.println("I2C started on board defaults");
  }

  rtcPresent = rtc.begin();
  if (rtcPresent) {
    Serial.println("RTC_DS3231 detected.");
    printNow();
    Serial.printf("lostPower: %s\n", rtc.lostPower() ? "true" : "false");
  } else {
    Serial.println("RTC_DS3231 not found on I2C. Connect module and reset.");
  }

  printHelp();
}

void loop() {
  while (Serial.available() > 0) {
    char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      inputLine.trim();
      if (inputLine.length() > 0) {
        handleCommand(inputLine);
      }
      inputLine = "";
    } else {
      inputLine += c;
    }
  }
}

#endif // RTC_MODULE_TEST
