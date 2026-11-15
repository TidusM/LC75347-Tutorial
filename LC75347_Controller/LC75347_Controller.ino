// SPDX-License-Identifier: MIT
// LC75347E six-channel audio controller with two encoders and an I2C LCD.
#include <Arduino.h>
#include <string.h>
#include "LC75347Frame.h"
#include "FrontPanel.h"
#include "Pcf8574Lcd.h"

// CCB control signals. Audio does not pass through the Arduino.
constexpr uint8_t PIN_CL = 7;
constexpr uint8_t PIN_DI = 8;
constexpr uint8_t PIN_CE = 9;
constexpr uint8_t PIN_MUTE = 11; // LC75347E pin 35; LOW=mute.
constexpr unsigned int EDGE_US = 5;

lc75347::State state = lc75347::initialState();
bool muted = true;
char line[48];
uint8_t used = 0;
bool overflow = false;
AudioLevels audio;
EncoderInput volumeEncoder, settingsEncoder;
DebouncedButton volumePush, settingsPush;
Pcf8574Lcd display;
bool home = true, editing = false, displayDirty = true;
uint8_t menuItem = MENU_BASS;
int savedValue = 0;
bool savedEnabled = false;
uint32_t lastDisplay = 0;
char lastRows[LCD_ROWS][LCD_COLUMNS + 1] = {};

lc75347::Frame currentFrame() {
  return lc75347::makeFrame(state, muted);
}

// Clock each bit on a rising edge, LSB first, leaving CL
// high at the end of each byte. CE rises after the eighth address clock.
void writeByteLsb(uint8_t value) {
  for (uint8_t bit = 0; bit < 8; ++bit) {
    digitalWrite(PIN_CL, LOW);
    delayMicroseconds(EDGE_US);
    digitalWrite(PIN_DI, (value >> bit) & 1);
    delayMicroseconds(EDGE_US);
    digitalWrite(PIN_CL, HIGH);
    delayMicroseconds(EDGE_US);
  }
}

void transmit() {
  const lc75347::Frame frame = currentFrame();
  digitalWrite(PIN_CE, LOW);
  delayMicroseconds(EDGE_US);
  writeByteLsb(lc75347::kAddress);
  digitalWrite(PIN_CE, HIGH);
  delayMicroseconds(EDGE_US);
  for (uint8_t i = 0; i < 9; ++i) writeByteLsb(frame.bytes[i]);
  digitalWrite(PIN_CE, LOW); // Latch the complete 72-bit payload.
  delayMicroseconds(EDGE_US);
  digitalWrite(PIN_CL, LOW);
  digitalWrite(PIN_DI, LOW);
  delayMicroseconds(EDGE_US);
}

void releaseHardwareMute() {
  digitalWrite(PIN_MUTE, HIGH);
  delayMicroseconds(EDGE_US);
  // Apply the requested levels again after releasing external mute.
  transmit();
}

void initializeChip(bool startUnmuted = false) {
  digitalWrite(PIN_MUTE, LOW);
  muted = true;
  state = lc75347::initialState();
  state.tonePath = true; // Front tone circuitry enabled by default; bass/treble start flat.
  audio = AudioLevels(); // All channels enabled at -30 dB, with zero offsets.
  audio.sync(state);
  home = true; editing = false; menuItem = MENU_BASS; displayDirty = true;
  // Allow the supply/reference to begin settling before the first CCB frame.
  delay(100);
  transmit();
  // Keep the initial muted frame active while the analog reference settles.
  delay(100);
  if (startUnmuted) {
    muted = false;
    transmit(); // Latch the starting levels while hardware mute is active.
    releaseHardwareMute();
  }
}

void showState() {
  Serial.print(F("Requested attenuation [SBW RR RL C FR FL], 96=mute: "));
  for (uint8_t i = 0; i < 6; ++i) {
    Serial.print(state.attenuation[i]); Serial.print(' ');
  }
  Serial.print(F("\nMaster mute=")); Serial.print(muted);
  Serial.print(F(" master attenuation=")); Serial.print(audio.master);
  Serial.print(F(" balance=")); Serial.print(audio.balance);
  Serial.print(F(" zero-cross=")); Serial.print(state.zeroCross);
  Serial.print(F(" tone-path=")); Serial.print(state.tonePath);
  Serial.print(F(" bass=")); Serial.print(state.bassDb);
  Serial.print(F(" treble=")); Serial.println(state.trebleDb);
  Serial.print(F("Port sink mask: ")); Serial.println(state.portSinkMask);
  Serial.println(F("Requested settings only; no processor readback."));
}

void help() {
  Serial.println(F("master N: master attenuation 0..95 dB; preserve channel offsets"));
  Serial.println(F("v N: all channels 0..95 dB; 96=off; reset offsets/balance"));
  Serial.println(F("c CH N: CH=sbw rr rl c fr fl; attenuation 0..95; 96=off"));
  Serial.println(F("m/u: hardware + data mute/unmute; r: reset muted; s: settings; h: help"));
  Serial.println(F("b/t N: bass/treble -12..12 dB in steps of 2"));
  Serial.println(F("bal N: front balance -24..24; positive=right"));
  Serial.println(F("p 0|1: front tone bypass/enable; z 0|1: zero-cross off/on"));
  Serial.println(F("o MASK: output ports 0..15; bit0=OUTP1; 1=sink, 0=open"));
  Serial.println(F("lcd: retry the display after an I2C error"));
}

bool number(const char *token, int &value) {
  if (!token || !*token) return false;
  bool negative = *token == '-';
  if (*token == '-' || *token == '+') ++token;
  if (!*token) return false;
  int result = 0;
  while (*token) {
    if (*token < '0' || *token > '9' || result > 100) return false;
    result = result * 10 + (*token++ - '0');
  }
  value = negative ? -result : result;
  return true;
}

int channelIndex(const char *token) {
  if (!token) return -1;
  const char *names[] = {"sbw", "rr", "rl", "c", "fr", "fl"};
  for (uint8_t i = 0; i < 6; ++i) if (strcmp(token, names[i]) == 0) return i;
  int result;
  return number(token, result) && result >= 0 && result < 6 ? result : -1;
}

void volumeInterrupt() {
  volumeEncoder.edge(digitalRead(PIN_VOLUME_A), digitalRead(PIN_VOLUME_B), micros());
}
void settingsInterrupt() {
  settingsEncoder.edge(digitalRead(PIN_SETTINGS_A), digitalRead(PIN_SETTINGS_B), micros());
}

void applyLevels() {
  audio.sync(state);
  transmit();
  displayDirty = true;
}

void setMuted(bool value) {
  if (value) digitalWrite(PIN_MUTE, LOW);
  muted = value;
  transmit();
  if (!value) releaseHardwareMute();
  displayDirty = true;
}

int settingValue() {
  const int ch = menuChannel(menuItem);
  if (ch >= 0) return audio.attenuation(ch);
  switch (menuItem) {
    case MENU_BASS: return state.bassDb;
    case MENU_TREBLE: return state.trebleDb;
    case MENU_BALANCE: return audio.balance;
    case MENU_TONE: return state.tonePath;
    case MENU_ZERO: return state.zeroCross;
    default: return 0;
  }
}

void startEdit() {
  const int ch = menuChannel(menuItem);
  savedValue = ch >= 0 ? audio.trim[ch] : settingValue();
  savedEnabled = ch >= 0 && (audio.enabled & (1 << ch));
  editing = true;
}

void cancelEdit() {
  if (!editing) return;
  const int ch = menuChannel(menuItem);
  if (ch >= 0) {
    audio.trim[ch] = static_cast<int8_t>(savedValue);
    if (savedEnabled) audio.enabled |= 1 << ch;
    else audio.enabled &= ~(1 << ch);
  } else {
    switch (menuItem) {
      case MENU_BASS: state.bassDb = savedValue; break;
      case MENU_TREBLE: state.trebleDb = savedValue; break;
      case MENU_BALANCE: audio.balance = savedValue; break;
      case MENU_TONE: state.tonePath = savedValue; break;
      case MENU_ZERO: state.zeroCross = savedValue; break;
      default: break;
    }
  }
  editing = false;
  applyLevels(); // Restores only the selected setting, never independent volume/mute.
}

void adjustSetting(int steps) {
  if (!steps || menuItem >= MENU_COUNT) return;
  const int old = settingValue();
  int value = old;
  const int ch = menuChannel(menuItem);
  if (ch >= 0) {
    // Clockwise louder; 96 is the independent channel OFF sentinel.
    value = clampValue(old - steps, 0, 96);
    if (value != old) audio.channel(ch, value);
  } else {
    switch (menuItem) {
      case MENU_BASS: value = clampValue(old + steps * 2, -12, 12); state.bassDb = value; break;
      case MENU_TREBLE: value = clampValue(old + steps * 2, -12, 12); state.trebleDb = value; break;
      case MENU_BALANCE: value = clampValue(old + steps, -24, 24); audio.balance = value; break;
      case MENU_TONE: value = clampValue(old + steps, 0, 1); state.tonePath = value; break;
      case MENU_ZERO: value = clampValue(old + steps, 0, 1); state.zeroCross = value; break;
      default: break;
    }
  }
  if (value != old) applyLevels();
}

void turnVolume(int steps) {
  if (!steps) return;
  const uint8_t value = clampValue(audio.master - steps, 0, 95);
  if (value != audio.master) { audio.master = value; applyLevels(); }
}

void turnSettings(int steps) {
  if (!steps) return;
  if (home) {
    home = false; menuItem = MENU_BASS;
    // The first detent opens Bass; remaining detents move through the menu.
    steps -= steps > 0 ? 1 : -1;
  }
  if (editing) adjustSetting(steps);
  else {
    const int next = (menuItem + steps) % MENU_COUNT;
    menuItem = next < 0 ? next + MENU_COUNT : next;
  }
  displayDirty = true;
}

void settingsClick() {
  if (home) { home = false; menuItem = MENU_BASS; }
  else if (editing) editing = false;
  else startEdit();
  displayDirty = true;
}

void pollControls() {
  const uint32_t now = millis();
  if (volumePush.poll(digitalRead(PIN_VOLUME_PUSH), now) == BUTTON_PRESS) setMuted(!muted);
  const ButtonEvent event = settingsPush.poll(digitalRead(PIN_SETTINGS_PUSH), now);
  if (event == BUTTON_CLICK) settingsClick();
  else if (event == BUTTON_HOLD) {
    cancelEdit(); home = true; displayDirty = true;
  }
  turnVolume(volumeEncoder.take() * VOLUME_DIRECTION);
  turnSettings(settingsEncoder.take() * SETTINGS_DIRECTION);
}

const char *menuName() {
  switch (menuItem) {
    case MENU_BASS: return "Bass";
    case MENU_TREBLE: return "Treble";
    case MENU_BALANCE: return "Balance";
    case MENU_TONE: return "Tone path";
    case MENU_ZERO: return "Zero cross";
    case MENU_FL: return "FL level";
    case MENU_FR: return "FR level";
    case MENU_CENTER: return "C level";
    case MENU_RL: return "RL level";
    case MENU_RR: return "RR level";
    case MENU_SBW: return "SBW level";
    default: return "";
  }
}

void drawDisplay(bool force = false) {
  if (!display.online || !displayDirty) return;
  const uint32_t now = millis();
  if (!force && static_cast<uint32_t>(now - lastDisplay) < LCD_REFRESH_MS) return;
  lastDisplay = now;
  for (uint8_t row = 0; row < LCD_ROWS; ++row) {
    RowText out;
    if (row == 0) {
      out.add("Vol -"); out.number(audio.master); out.add("dB ");
      out.add(muted ? "MUTE" : "PLAY");
    } else if (row == 1) {
      if (home) out.add("Settings: push");
      else {
        out.add(editing ? "*" : ">"); out.add(menuName());
        if (menuItem < MENU_COUNT) {
          out.add(":");
          const int value = settingValue();
          if (menuChannel(menuItem) >= 0) {
            if (value == 96) out.add("OFF");
            else { out.add("-"); out.number(value); out.add("dB"); }
          } else if (menuItem == MENU_TONE) out.add(value ? "ON" : "BYP");
          else if (menuItem == MENU_ZERO) out.add(value ? "ON" : "OFF");
          else { out.number(value, true); if (menuItem != MENU_BALANCE) out.add("dB"); }
        }
      }
    } else if (row == 2) {
      out.add("FL:"); if (state.attenuation[lc75347::FL] == 96) out.add("OFF");
      else out.number(-state.attenuation[lc75347::FL]);
      out.add(" FR:"); if (state.attenuation[lc75347::FR] == 96) out.add("OFF");
      else out.number(-state.attenuation[lc75347::FR]);
    } else out.add(editing ? "Push=save Hold=undo" : "Push=select");
    out.finish();
    if (force || strcmp(out.text, lastRows[row]) != 0) {
      display.row(row, out.text);
      for (uint8_t i = 0; i <= LCD_COLUMNS; ++i) lastRows[row][i] = out.text[i];
    }
  }
  displayDirty = false;
  if (!display.online) Serial.println(F("LCD offline; audio controls remain active. Send lcd to retry."));
}

void command(char *input) {
  char *cmd = strtok(input, " \t");
  if (!cmd) return;
  char *first = strtok(nullptr, " \t");
  char *second = strtok(nullptr, " \t");
  char *extra = strtok(nullptr, " \t");
  int a = 0, b = 0;
  bool ok = false;
  bool send = false;
  bool releaseMute = false;
  if (!extra) {
    if (!first && strlen(cmd) == 1) {
      switch (*cmd) {
        case 'm':
          digitalWrite(PIN_MUTE, LOW); muted = true; ok = send = true; break;
        case 'u':
          muted = false; releaseMute = true; ok = send = true; break;
        case 'r': initializeChip(); ok = true; break;
        case 's': ok = true; break;
        case 'h': help(); return;
      }
    } else if (strcmp(cmd, "c") == 0) {
      a = channelIndex(first);
      if (a >= 0 && number(second, b) && b >= 0 && b <= 96) {
        audio.channel(a, static_cast<uint8_t>(b)); ok = send = true;
      }
    } else if (!second && number(first, a)) {
      if (strcmp(cmd, "master") == 0 && a >= 0 && a <= 95) {
        audio.master = static_cast<uint8_t>(a); ok = send = true;
      } else if (strcmp(cmd, "bal") == 0 && a >= -24 && a <= 24) {
        audio.balance = static_cast<int8_t>(a); ok = send = true;
      } else if (strlen(cmd) == 1) {
        switch (*cmd) {
          case 'v':
            if (a >= 0 && a <= 96) {
              audio.all(static_cast<uint8_t>(a));
              ok = send = true;
            }
            break;
          case 'o':
            if (a >= 0 && a <= 15) {
              state.portSinkMask = static_cast<uint8_t>(a); ok = send = true;
            }
            break;
          case 'z': if (a == 0 || a == 1) { state.zeroCross = a; ok = send = true; } break;
          case 'p': if (a == 0 || a == 1) { state.tonePath = a; ok = send = true; } break;
          case 'b': case 't':
            if (a >= -12 && a <= 12 && a % 2 == 0) {
              if (*cmd == 'b') state.bassDb = static_cast<int8_t>(a);
              else state.trebleDb = static_cast<int8_t>(a);
              ok = send = true;
            }
            break;
        }
      }
    }
  }
  if (!first && strcmp(cmd, "lcd") == 0) {
    display.begin(); displayDirty = true; drawDisplay(true);
    Serial.println(display.online ? F("LCD online.") : F("LCD not found or bus error."));
    return;
  }
  if (!ok) { Serial.println(F("Rejected command; send h for help.")); return; }
  // Serial is another control surface: retain a live preview and end its edit
  // transaction before allowing later panel undo to overwrite a serial change.
  editing = false;
  audio.sync(state);
  if (send) transmit();
  if (releaseMute) releaseHardwareMute();
  displayDirty = true;
  showState();
}

void setup() {
  const uint8_t pins[] = {PIN_MUTE, PIN_CE, PIN_CL, PIN_DI};
  for (uint8_t i = 0; i < sizeof(pins); ++i) {
    digitalWrite(pins[i], LOW);
    pinMode(pins[i], OUTPUT);
  }
  // IC VDD must already be stable above 4.5 V. Send r if the IC is powered later.
  initializeChip(true);
  Serial.begin(115200);
  Serial.println(F("LC75347E controller; starts playing at -30 dB."));
  const uint8_t inputs[] = {PIN_VOLUME_A, PIN_VOLUME_B, PIN_SETTINGS_A,
    PIN_SETTINGS_B, PIN_VOLUME_PUSH, PIN_SETTINGS_PUSH};
  for (uint8_t i = 0; i < sizeof(inputs); ++i) pinMode(inputs[i], INPUT_PULLUP);
  volumeEncoder.lastA = digitalRead(PIN_VOLUME_A);
  settingsEncoder.lastA = digitalRead(PIN_SETTINGS_A);
  volumePush.begin(digitalRead(PIN_VOLUME_PUSH), millis());
  settingsPush.begin(digitalRead(PIN_SETTINGS_PUSH), millis());
  attachInterrupt(digitalPinToInterrupt(PIN_VOLUME_A), volumeInterrupt, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_SETTINGS_A), settingsInterrupt, CHANGE);
  if (!display.begin()) Serial.println(F("LCD not found; check Config.h/address/wiring. Serial and knobs still work."));
  drawDisplay(true);
  help();
  showState();
}

void loop() {
  pollControls();
  // Bound work per pass so a continuous Serial stream cannot starve buttons.
  uint8_t serialBudget = 32;
  while (Serial.available() && serialBudget--) {
    const char ch = static_cast<char>(Serial.read());
    // Accept LF, CRLF and CR-only terminals; an empty second line is harmless.
    if (ch == '\n' || ch == '\r') {
      if (overflow) Serial.println(F("Rejected overlong command."));
      else { line[used] = '\0'; command(line); }
      used = 0; overflow = false;
    } else if (!overflow) {
      if (used < sizeof(line) - 1) line[used++] = ch;
      else overflow = true;
    }
  }
  drawDisplay();
}
