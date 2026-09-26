#include "touch_service.h"

#if defined(ESP32_8048S050C)

#include <Arduino.h>
#include <Wire.h>

#include <cstring>

namespace {
// GT911 lives on its own I2C bus (spec/puzzlebox_hw.md 6.1: SDA19/SCL20),
// separate from the main controller's shared bus (this is a different
// board entirely) — use the ESP32's second I2C peripheral so nothing else
// on this node needs to share it.
TwoWire touchWire(1);

constexpr std::uint16_t kRegStatus = 0x814E;
// Confirmed against Espressif's own esp_lcd_touch_gt911 driver (used by
// mr-sven/esp32-8048S050C, a reference project for this exact board):
// track_id=0x814F, X low/high=0x8150/0x8151, Y low/high=0x8152/0x8153,
// strength low/high=0x8154/0x8155, reserved=0x8156. Earlier attempts here
// mis-set this to 0x8151 (skipping track_id at the wrong offset), which
// happened to straddle X's high byte and Y's low byte into one field that
// looked like it tracked position -- explaining why one "axis" worked and
// the other stayed stuck near 0 no matter where the screen was touched.
constexpr std::uint16_t kRegPoint1 = 0x8150;
}  // namespace

bool TouchService::probeAddress(std::uint8_t addr) {
  touchWire.beginTransmission(addr);
  touchWire.write(static_cast<std::uint8_t>(kRegStatus >> 8));
  touchWire.write(static_cast<std::uint8_t>(kRegStatus & 0xFF));
  return touchWire.endTransmission() == 0;  // 0 = ACK received
}

void TouchService::begin(diagnostics::EventLog& log) {
  pinMode(display_board::kTouchRst, OUTPUT);
  digitalWrite(display_board::kTouchRst, LOW);
  delay(10);
  digitalWrite(display_board::kTouchRst, HIGH);
  delay(50);

  touchWire.begin(display_board::kTouchSda, display_board::kTouchScl, display_board::kTouchI2cHz);

  // INT is "not connected" per spec/puzzlebox_hw.md 6.1, so the address
  // select strap was never verified -- probe both documented GT911
  // addresses (0x5D from display_board::kTouchAddr, 0x14 the other common
  // one) instead of assuming the configured constant is right.
  constexpr std::uint8_t kAltAddr = 0x14;
  if (probeAddress(display_board::kTouchAddr)) {
    touchAddr_ = display_board::kTouchAddr;
  } else if (probeAddress(kAltAddr)) {
    touchAddr_ = kAltAddr;
  } else {
    touchAddr_ = display_board::kTouchAddr;  // neither acked; keep old behaviour
  }
  log.logf("touch", "GT911 reset sent, using addr 0x%02X%s", touchAddr_,
           touchAddr_ == display_board::kTouchAddr ? "" : " (fallback)");

  readConfigResolution();
  log.logf("touch", "GT911 config X_MAX=%u Y_MAX=%u", configXMax_, configYMax_);
}

void TouchService::readConfigResolution() {
  // Registers 0x8048-0x804B hold the GT911's own configured output
  // resolution for X and Y (2 bytes LE each) -- separate from the panel's
  // actual pixel size. On some Sunton-family boards this ships
  // misconfigured (e.g. one axis set far smaller than the real panel),
  // which squashes that axis's raw touch reports into a tiny range no
  // matter where you touch, independent of any byte-order/axis-swap issue
  // in how the point registers are read.
  constexpr std::uint16_t kRegXMaxLow = 0x8048;
  std::uint8_t buf[4] = {0};
  if (readRegs(kRegXMaxLow, buf, 4)) {
    configXMax_ = static_cast<std::uint16_t>(buf[0] | (buf[1] << 8));
    configYMax_ = static_cast<std::uint16_t>(buf[2] | (buf[3] << 8));
  }
}

void TouchService::writeReg8(std::uint16_t reg, std::uint8_t value) {
  touchWire.beginTransmission(touchAddr_);
  touchWire.write(static_cast<std::uint8_t>(reg >> 8));
  touchWire.write(static_cast<std::uint8_t>(reg & 0xFF));
  touchWire.write(value);
  touchWire.endTransmission();
}

std::uint8_t TouchService::readReg8(std::uint16_t reg) {
  touchWire.beginTransmission(touchAddr_);
  touchWire.write(static_cast<std::uint8_t>(reg >> 8));
  touchWire.write(static_cast<std::uint8_t>(reg & 0xFF));
  touchWire.endTransmission(false);
  touchWire.requestFrom(static_cast<int>(touchAddr_), 1);
  if (touchWire.available()) {
    return static_cast<std::uint8_t>(touchWire.read());
  }
  return 0;
}

bool TouchService::readRegs(std::uint16_t reg, std::uint8_t* buf, std::uint8_t len) {
  touchWire.beginTransmission(touchAddr_);
  touchWire.write(static_cast<std::uint8_t>(reg >> 8));
  touchWire.write(static_cast<std::uint8_t>(reg & 0xFF));
  touchWire.endTransmission(false);
  touchWire.requestFrom(static_cast<int>(touchAddr_), static_cast<int>(len));
  std::uint8_t i = 0;
  for (; i < len && touchWire.available(); ++i) {
    buf[i] = static_cast<std::uint8_t>(touchWire.read());
  }
  return i == len;  // false if the bus gave us fewer bytes than requested
}

bool TouchService::readState(std::int16_t& x, std::int16_t& y) {
  const std::uint8_t status = readReg8(kRegStatus);
  lastStatusByte_ = status;
  const bool dataReady = (status & 0x80) != 0;

  if (!dataReady) {
    // No fresh report since the last ack — not a release, just nothing new
    // yet (the GT911 reports periodically while held, not continuously).
    x = lastX_;
    y = lastY_;
    return pressed_;
  }

  const std::uint8_t pointCount = status & 0x0F;
  if (pointCount == 0) {
    writeReg8(kRegStatus, 0x00);
    pressed_ = false;
    x = lastX_;
    y = lastY_;
    return false;
  }

  std::uint8_t point[8] = {0};
  const bool gotAllBytes = readRegs(kRegPoint1, point, 8);
  memcpy(lastRawPoint_, point, 8);
  writeReg8(kRegStatus, 0x00);  // ack — lets the GT911 prepare the next report

  if (!gotAllBytes) {
    // The bus gave us fewer bytes than requested -- point[] would otherwise
    // be trusted with a partially-stale/zeroed buffer. Treat as no new data
    // rather than reporting a bogus coordinate.
    x = lastX_;
    y = lastY_;
    return pressed_;
  }

  // point[0..1] = X low/high (registers 0x8150-0x8151), point[2..3] = Y
  // low/high (0x8152-0x8153) -- matches Espressif's reference GT911 driver
  // for this exact board (mr-sven/esp32-8048S050C), which uses no
  // swap_xy/mirror. Confirmed against the panel's own 800x480 via /debug.
  const std::int16_t rawX = static_cast<std::int16_t>(point[0] | (point[1] << 8));
  const std::int16_t rawY = static_cast<std::int16_t>(point[2] | (point[3] << 8));

  lastX_ = rawX;
  lastY_ = rawY;
  pressed_ = true;
  x = lastX_;
  y = lastY_;
  return true;
}

#endif  // ESP32_8048S050C
