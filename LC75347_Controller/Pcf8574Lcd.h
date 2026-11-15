// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Config.h"

// Minimal write-only HD44780 / common PCF8574 backpack interface. Wire is the
// only library dependency. Mapping matches the common LiquidCrystal_I2C layout:
// P0=RS, P1=RW(low), P2=E, P3=backlight, P4..7=D4..7. Not universal to backpacks.
class Pcf8574Lcd {
 public:
  bool online = false;
  bool begin() {
    Wire.begin();
    Wire.setClock(100000);
#if defined(__AVR__)
    // Required AVR core supports this API. A missing/failed LCD must not hang mute.
    Wire.setWireTimeout(2500, true);
    Wire.clearWireTimeoutFlag();
#endif
    Wire.beginTransmission(LCD_ADDRESS);
    online = Wire.endTransmission() == 0;
    if (!online) return false;
    delay(50);
    nibble(0x30); delayMicroseconds(4500);
    nibble(0x30); delayMicroseconds(4500);
    nibble(0x30); delayMicroseconds(150);
    nibble(0x20); // Four-bit mode.
    command(0x28); // Two-line controller mode, also used by 20x4 modules.
    command(0x08); command(0x01); delayMicroseconds(2000);
    command(0x06); command(0x0c);
    return online;
  }
  void row(uint8_t rowIndex, const char *text) {
    if (!online || rowIndex >= LCD_ROWS) return;
    const uint8_t offsets[] = {0, 0x40, LCD_COLUMNS, static_cast<uint8_t>(0x40 + LCD_COLUMNS)};
    command(0x80 | offsets[rowIndex]);
    for (uint8_t i = 0; i < LCD_COLUMNS && online; ++i) send(text[i], true);
  }
 private:
  void write(uint8_t v) {
    if (!online) return;
    Wire.beginTransmission(LCD_ADDRESS);
    Wire.write(v | 0x08); // Backlight on; RW is always low.
    if (Wire.endTransmission() != 0) online = false;
#if defined(__AVR__)
    if (Wire.getWireTimeoutFlag()) { online = false; Wire.clearWireTimeoutFlag(); }
#endif
  }
  void nibble(uint8_t v) {
    write(v); write(v | 0x04); delayMicroseconds(1);
    write(v & ~0x04); delayMicroseconds(50);
  }
  void send(uint8_t v, bool data) {
    const uint8_t rs = data ? 1 : 0;
    nibble((v & 0xf0) | rs); nibble(((v << 4) & 0xf0) | rs);
  }
  void command(uint8_t v) { send(v, false); }
};
