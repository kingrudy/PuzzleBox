#pragma once

#include "diagnostics/event_log.h"
#include "display_board_profile.h"

#if defined(ESP32_8048S050C)

#include <cstdint>

// Minimal raw-I2C GT911 capacitive touch reader feeding GameView's LVGL
// pointer input device (see game_view.cpp) — single-point only, no gesture
// support, no multi-touch, no calibration UI.
//
// UNVERIFIED AGAINST REAL HARDWARE. The GT911's I2C address (0x14 vs 0x5D)
// is normally selected by the INT pin's level during reset, but
// spec/puzzlebox_hw.md section 6.1 documents INT as "not connected" on this
// board — so address selection here relies entirely on the board's own
// factory pull-resistor strapping defaulting to 0x5D
// (display_board::kTouchAddr). If touches don't register, that address is
// the first thing to check with a logic analyzer or I2C scanner.
class TouchService {
 public:
  void begin(diagnostics::EventLog& log);

  // Current pressed/released level state, in panel pixel coordinates —
  // meant to be called from an LVGL indev read callback every frame, not
  // edge-triggered. Returns true while a finger is down (x/y updated),
  // false once release is detected. The GT911 only raises its "buffer
  // ready" status flag when it has a fresh report; between reports (finger
  // still down, no new sample yet) this returns the last known state
  // unchanged rather than treating silence as a release.
  bool readState(std::int16_t& x, std::int16_t& y);

 private:
  void writeReg8(std::uint16_t reg, std::uint8_t value);
  std::uint8_t readReg8(std::uint16_t reg);
  void readRegs(std::uint16_t reg, std::uint8_t* buf, std::uint8_t len);

  bool pressed_ = false;
  std::int16_t lastX_ = 0;
  std::int16_t lastY_ = 0;
};

#endif  // ESP32_8048S050C
