// SPDX-License-Identifier: MIT
#pragma once
#include <Arduino.h>
#include "Config.h"
#include "LC75347Frame.h"

inline int clampValue(int v, int low, int high) {
  return v < low ? low : v > high ? high : v;
}

// Canonical master + trims avoid losing channel offsets at an attenuation limit.
// Balance adds attenuation only to the opposite front channel; positive=right.
struct AudioLevels {
  uint8_t master = 30;
  uint8_t enabled = 0x3f;
  int8_t trim[6] = {}; // No per-channel offset; actual level follows the master.
  int8_t balance = 0;

  int balanceCut(uint8_t ch) const {
    return ch == lc75347::FL && balance > 0 ? balance :
           ch == lc75347::FR && balance < 0 ? -balance : 0;
  }
  uint8_t attenuation(uint8_t ch) const {
    return !(enabled & (1 << ch)) ? 96 :
      static_cast<uint8_t>(clampValue(master + trim[ch] + balanceCut(ch), 0, 95));
  }
  void sync(lc75347::State &s) const {
    for (uint8_t ch = 0; ch < 6; ++ch) s.attenuation[ch] = attenuation(ch);
  }
  void channel(uint8_t ch, uint8_t value) {
    if (value == 96) enabled &= ~(1 << ch);
    else {
      enabled |= 1 << ch;
      // Balance can add 24 dB, so the difference can be as small as -119.
      trim[ch] = static_cast<int8_t>(value - master - balanceCut(ch));
    }
  }
  void all(uint8_t value) {
    balance = 0;
    for (uint8_t ch = 0; ch < 6; ++ch) trim[ch] = 0;
    enabled = value == 96 ? 0 : 0x3f;
    if (value != 96) master = value;
  }
};

// ISR only records A transitions and B direction. Signed half-steps cancel
// ordinary A contact chatter. The main loop waits for a short quiet interval
// before draining them. Set the edge count in Config.h to match the encoder.
struct EncoderInput {
  volatile int16_t edges = 0;
  volatile uint32_t lastEdge = 0;
  volatile uint8_t lastA = HIGH;

  void edge(uint8_t a, uint8_t b, uint32_t now) {
    if (a == lastA) return;
    lastA = a;
    const int delta = a == b ? 1 : -1;
    const int next = edges + delta;
    edges = static_cast<int16_t>(clampValue(next, -64, 64));
    lastEdge = now;
  }
  int take() {
    // Called only from the main loop. AVR multi-byte fields need atomic access.
    noInterrupts();
    const uint32_t now = micros();
    int result = 0;
    if (static_cast<uint32_t>(now - lastEdge) >= ENCODER_SETTLE_US) {
      result = edges / ENCODER_A_EDGES_PER_STEP;
      edges -= result * ENCODER_A_EDGES_PER_STEP;
    }
    interrupts();
    return result;
  }
};

enum ButtonEvent : uint8_t { BUTTON_NONE, BUTTON_PRESS, BUTTON_CLICK, BUTTON_HOLD };
struct DebouncedButton {
  uint8_t raw = HIGH, stable = HIGH;
  uint32_t changedAt = 0, pressedAt = 0;
  bool armed = false, held = false;

  void begin(uint8_t level, uint32_t now) {
    raw = stable = level;
    changedAt = pressedAt = now;
    armed = level == HIGH; // Held at boot must be released before it can act.
    held = false;
  }
  ButtonEvent poll(uint8_t level, uint32_t now) {
    if (level != raw) { raw = level; changedAt = now; }
    if (raw != stable && static_cast<uint32_t>(now - changedAt) >= BUTTON_DEBOUNCE_MS) {
      stable = raw;
      if (stable == LOW) {
        pressedAt = now; held = false;
        return armed ? BUTTON_PRESS : BUTTON_NONE;
      }
      const bool click = armed && !held;
      armed = true;
      return click ? BUTTON_CLICK : BUTTON_NONE;
    }
    if (stable == LOW && raw == LOW && armed && !held &&
        static_cast<uint32_t>(now - pressedAt) >= SETTINGS_HOLD_MS) {
      held = true;
      return BUTTON_HOLD;
    }
    return BUTTON_NONE;
  }
};

enum MenuItem : uint8_t {
  MENU_BASS, MENU_TREBLE, MENU_BALANCE, MENU_TONE, MENU_ZERO,
  MENU_FL, MENU_FR, MENU_CENTER, MENU_RL, MENU_RR, MENU_SBW,
  MENU_COUNT
};
inline int menuChannel(uint8_t menu) {
  // Reverse channel order: UI front first, bus SBW first.
  return menu >= MENU_FL && menu <= MENU_SBW ? 5 - (menu - MENU_FL) : -1;
}
struct RowText {
  char text[LCD_COLUMNS + 1] = {};
  uint8_t used = 0;
  void add(const char *s) { while (*s && used < LCD_COLUMNS) text[used++] = *s++; }
  void number(int v, bool plus = false) {
    if (v < 0) { add("-"); v = -v; } else if (plus) add("+");
    char digits[6]; uint8_t n = 0;
    do { digits[n++] = '0' + v % 10; v /= 10; } while (v && n < sizeof(digits));
    while (n && used < LCD_COLUMNS) text[used++] = digits[--n];
  }
  void finish() { while (used < LCD_COLUMNS) text[used++] = ' '; text[used] = 0; }
};
