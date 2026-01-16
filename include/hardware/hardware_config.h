#ifndef HARDWARE_CONFIG_H
#define HARDWARE_CONFIG_H

#include <Arduino.h>

// SD card SPI pins (ESP32-S3, dedicated HSPI in reference design)
constexpr uint8_t SD_MOSI_PIN = 35;
constexpr uint8_t SD_MISO_PIN = 37;
constexpr uint8_t SD_SCK_PIN = 36;
constexpr uint8_t SD_CS_PIN = 39;
constexpr uint32_t SD_SPI_CLOCK_HZ = 20000000UL;

// MCP2515 SPI pins (shared SPI bus for CAN controllers)
constexpr uint8_t CAN_SPI_SCK_PIN = 12;
constexpr uint8_t CAN_SPI_MOSI_PIN = 11;
constexpr uint8_t CAN_SPI_MISO_PIN = 13;

// CAN controller chip-select and interrupt pins
constexpr uint8_t CAN1_CS_PIN = 10;
constexpr uint8_t CAN1_INT_PIN = 9;
constexpr uint8_t CAN2_CS_PIN = 7;
constexpr uint8_t CAN2_INT_PIN = 8;

// CAN termination control pins (optional; -1 means not wired)
constexpr int8_t CAN_TERM_PINS[6] = { -1, -1, -1, -1, -1, -1 };

// I2C bus pins (RTC and other peripherals; -1 uses board defaults)
constexpr int8_t I2C_SDA_PIN = -1;
constexpr int8_t I2C_SCL_PIN = -1;

// RTC pins (optional; -1 means not wired)
constexpr int8_t RTC_INT_PIN = -1;

// MCP2515 clock and SPI settings
constexpr uint32_t CAN_SPI_CLOCK_HZ = 10UL * 1000UL * 1000UL;
constexpr uint32_t MCP2515_QUARTZ_HZ = 8UL * 1000UL * 1000UL;

// Status LED
constexpr uint8_t STATUS_LED_PIN = 48;

#endif // HARDWARE_CONFIG_H
