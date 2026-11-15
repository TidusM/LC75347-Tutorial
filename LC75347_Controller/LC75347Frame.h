// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>

namespace lc75347 {
constexpr uint8_t kAddress = 0x82;
constexpr uint8_t kMute = 96; // Software sentinel; transmitted mute code is 0x7f.
enum Channel : uint8_t { SBW, RR, RL, CENTER, FR, FL, CHANNEL_COUNT };

struct State {
  uint8_t attenuation[CHANNEL_COUNT]; // Positive dB attenuation 0..95; 96=mute.
  bool zeroCross;
  bool tonePath;
  int8_t bassDb;
  int8_t trebleDb;
  uint8_t portSinkMask; // bit 0=OUTP1 ... bit 3=OUTP4; 1=sink, 0=open.
};
struct Frame { uint8_t bytes[9]; };

constexpr State initialState() {
  return State{{96, 96, 96, 96, 96, 96}, false, false, 0, 0, 0};
}
constexpr uint8_t volumeCode(uint8_t attenuation, bool muted) {
  return muted || attenuation > 95 ? 0x7f : attenuation;
}
constexpr uint8_t toneCode(int8_t db) {
  return (db < -12 || db > 12 || db % 2 != 0) ? 0 :
         db < 0 ? static_cast<uint8_t>(8 + (-db / 2)) :
                  static_cast<uint8_t>(db / 2);
}
// Sanyo ENN*7378 pp. 5-8, 15. D0 is bit 0 of bytes[0].
// Keep D64=D65=1 to initialize/update both tone latches; D70=D71 always zero.
// Master mute forces immediate (non-zero-cross) mute codes on all six channels.
constexpr Frame makeFrame(const State &s, bool muted) {
  return Frame{{volumeCode(s.attenuation[SBW], muted),
                volumeCode(s.attenuation[RR], muted),
                volumeCode(s.attenuation[RL], muted),
                volumeCode(s.attenuation[CENTER], muted),
                volumeCode(s.attenuation[FR], muted),
                volumeCode(s.attenuation[FL], muted),
                static_cast<uint8_t>(((s.zeroCross && !muted) ? 0 : 0x3f) |
                                     (s.tonePath ? 0xc0 : 0)),
                static_cast<uint8_t>(toneCode(s.bassDb) |
                                     (toneCode(s.trebleDb) << 4)),
                static_cast<uint8_t>(0x03 | ((s.portSinkMask & 0x0f) << 2))}};
}
} // namespace lc75347
