#if !defined(SD_SPEED_TEST) && !defined(RX_LOAD_TEST)

#include <Arduino.h>

#include "can/can_manager.h"
#include "config/app_config.h"
#include "hardware/hardware_config.h"
#include "logging/log_writer.h"
#include "net/net_manager.h"
#include "rest/rest_api.h"
#include "storage/storage_manager.h"

#ifndef APP_NAME
#define APP_NAME "CAN-Grabber"
#endif
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif
#ifndef PIO_ENV_NAME
#define PIO_ENV_NAME "unknown"
#endif

static void printBuildInfo() {
  Serial.println();
  Serial.print("App: ");
  Serial.println(APP_NAME);
  Serial.print("Version: ");
  Serial.println(APP_VERSION);
  Serial.print("Build: ");
  Serial.print(__DATE__);
  Serial.print(" ");
  Serial.println(__TIME__);
  Serial.print("PIO Env: ");
  Serial.println(PIO_ENV_NAME);
}

void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  printBuildInfo();

  config::init();
  storage::init();
  can::init();
  logging::init();
  logging::start();
  net::init();
  net::connect();
  rest::init();
  rest::start();
}

void loop() {
  net::loop();
  rest::loop();
  delay(1000);
}

#endif // !SD_SPEED_TEST && !RX_LOAD_TEST
