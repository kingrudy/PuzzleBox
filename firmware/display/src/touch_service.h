#pragma once

#include "diagnostics/event_log.h"
#include "display_board_profile.h"

#if defined(ESP32_8048S050C)

#include <cstdint>

// Minimal raw-I2C GT911 capacitive touch reader feeding GameView's LVGL
// pointer input device (see game_view.cpp) — single-point only, no gesture
// support, no multi-touch, no calibration UI.
//
// Verified against real hardware via /debug's raw touch test (live x/y,
// raw point bytes, and GT911 config X_MAX/Y_MAX readout) and cross-checked
// against Espressif's own esp_lcd_touch_gt911 driver as used by
// mr-sven/esp32-8048S050C, a reference project for this exact board: no
// swap_xy/mirror needed, address 0x5D (INT is grounded rather than
// connected on this board, which forces that address rather than letting
// it float per spec/puzzlebox_hw.md section 6.1) — begin() still probes
// both 0x5D and 0x14 defensively in case a differently-strapped clone
// shows up.
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

  // Raw diagnostics for /debug-style screens: the last status byte and the
  // 8 raw point-register bytes, exactly as they came off the bus, before
  // any interpretation as track_id/X/Y/size. Lets you see whether the
  // *bytes* look like a real, moving coordinate or like bus noise/garbage,
  // instead of trusting the already-decoded X/Y.
  std::uint8_t lastStatusByte() const { return lastStatusByte_; }
  const std::uint8_t* lastRawPoint() const { return lastRawPoint_; }

  // GT911's own configured output resolution (registers 0x8048-0x804B),
  // read once at begin(). If one of these is far smaller than this panel's
  // actual 800x480, that axis's raw touch values are squashed into a tiny
  // range at the source -- not a byte-order/swap bug in this driver.
  std::uint16_t configXMax() const { return configXMax_; }
  std::uint16_t configYMax() const { return configYMax_; }

 private:
  void writeReg8(std::uint16_t reg, std::uint8_t value);
  std::uint8_t readReg8(std::uint16_t reg);
  bool readRegs(std::uint16_t reg, std::uint8_t* buf, std::uint8_t len);
  bool probeAddress(std::uint8_t addr);
  void readConfigResolution();

  // INT is documented as not connected on this board (spec/puzzlebox_hw.md
  // 6.1), so the GT911's address-select strapping was never actually
  // confirmed -- begin() probes both known addresses and uses whichever
  // acks, instead of trusting display_board::kTouchAddr blindly.
  std::uint8_t touchAddr_ = 0;
  bool pressed_ = false;
  std::int16_t lastX_ = 0;
  std::int16_t lastY_ = 0;
  std::uint8_t lastStatusByte_ = 0;
  std::uint8_t lastRawPoint_[8] = {0};
  std::uint16_t configXMax_ = 0;
  std::uint16_t configYMax_ = 0;
};

#endif  // ESP32_8048S050C
