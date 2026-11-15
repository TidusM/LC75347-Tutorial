// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <stdint.h>

// Arduino Uno / ATmega328P. Encoder commons and switches connect to GND.
constexpr uint8_t PIN_VOLUME_A = 2, PIN_VOLUME_B = 4, PIN_VOLUME_PUSH = 6;
constexpr uint8_t PIN_SETTINGS_A = 3, PIN_SETTINGS_B = 5, PIN_SETTINGS_PUSH = 12;
constexpr int8_t VOLUME_DIRECTION = 1, SETTINGS_DIRECTION = -1; // Match the installed settings encoder.
constexpr uint8_t ENCODER_A_EDGES_PER_STEP = 2; // Two A changes per full quadrature cycle.
constexpr uint32_t ENCODER_SETTLE_US = 1500;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30, SETTINGS_HOLD_MS = 800;

// Winstar WH1602L (16x2) with an I2C backpack. Set its actual I2C address.
constexpr uint8_t LCD_ADDRESS = 0x3f, LCD_COLUMNS = 16, LCD_ROWS = 2;
constexpr uint32_t LCD_REFRESH_MS = 100;
static_assert(PIN_VOLUME_A != SDA && PIN_VOLUME_A != SCL &&
              PIN_VOLUME_B != SDA && PIN_VOLUME_B != SCL &&
              PIN_SETTINGS_A != SDA && PIN_SETTINGS_A != SCL &&
              PIN_SETTINGS_B != SDA && PIN_SETTINGS_B != SCL &&
              PIN_VOLUME_PUSH != SDA && PIN_VOLUME_PUSH != SCL &&
              PIN_SETTINGS_PUSH != SDA && PIN_SETTINGS_PUSH != SCL,
              "Encoder/button pins overlap SDA/SCL on this board; remap them before using the LCD");
static_assert(LCD_COLUMNS == 16 || LCD_COLUMNS == 20, "Use 16 or 20 LCD columns");
static_assert(LCD_ROWS == 2 || LCD_ROWS == 4, "Use 2 or 4 LCD rows");
static_assert(ENCODER_A_EDGES_PER_STEP == 1 || ENCODER_A_EDGES_PER_STEP == 2,
              "Use 1 for a half-cycle detent, or 2 for a full-cycle detent");
