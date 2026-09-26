#include "touch_service.h"

#if defined(ESP32_8048S050C)

#include <Arduino.h>
#include <Wire.h>

namespace {
// GT911 lives on its own I2C bus (spec/puzzlebox_hw.md 6.1: SDA19/SCL20),
// separate from the main controller's shared bus (this is a different
// board entirely) — use the ESP32's second I2C peripheral so nothing else
// on this node needs to share it.
TwoWire touchWire(1);

constexpr std::uint16_t kRegStatus = 0x814E;
constexpr std::uint16_t kRegPoint1 = 0x8150;
}  // namespace

void TouchService::begin(diagnostics::EventLog& log) {
  pinMode(display_board::kTouchRst, OUTPUT);
  digitalWrite(display_board::kTouchRst, LOW);
  delay(10);
  digitalWrite(display_board::kTouchRst, HIGH);
  delay(50);

  touchWire.begin(display_board::kTouchSda, display_board::kTouchScl, display_board::kTouchI2cHz);
  log.logf("touch", "GT911 reset sent, addr by strapping");
}

void TouchService::writeReg8(std::uint16_t reg, std::uint8_t value) {
  touchWire.beginTransmission(display_board::kTouchAddr);
  touchWire.write(static_cast<std::uint8_t>(reg >> 8));
  touchWire.write(static_cast<std::uint8_t>(reg & 0xFF));
  touchWire.write(value);
  touchWire.endTransmission();
}

std::uint8_t TouchService::readReg8(std::uint16_t reg) {
  touchWire.beginTransmission(display_board::kTouchAddr);
  touchWire.write(static_cast<std::uint8_t>(reg >> 8));
  touchWire.write(static_cast<std::uint8_t>(reg & 0xFF));
  touchWire.endTransmission(false);
  touchWire.requestFrom(static_cast<int>(display_board::kTouchAddr), 1);
  if (touchWire.available()) {
    return static_cast<std::uint8_t>(touchWire.read());
  }
  return 0;
}

void TouchService::readRegs(std::uint16_t reg, std::uint8_t* buf, std::uint8_t len) {
  touchWire.beginTransmission(display_board::kTouchAddr);
  touchWire.write(static_cast<std::uint8_t>(reg >> 8));
  touchWire.write(static_cast<std::uint8_t>(reg & 0xFF));
  touchWire.endTransmission(false);
  touchWire.requestFrom(static_cast<int>(display_board::kTouchAddr), static_cast<int>(len));
  for (std::uint8_t i = 0; i < len && touchWire.available(); ++i) {
    buf[i] = static_cast<std::uint8_t>(touchWire.read());
  }
}

bool TouchService::readState(std::int16_t& x, std::int16_t& y) {
  const std::uint8_t status = readReg8(kRegStatus);
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

  std::uint8_t point[8];
  readRegs(kRegPoint1, point, 8);
  writeReg8(kRegStatus, 0x00);  // ack — lets the GT911 prepare the next report

  // Raw GT911 coordinates, assumed pre-configured by the board vendor to
  // match the panel's own 800x480 pixel space (common for these prebuilt
  // Sunton modules) — see the "unverified" note in touch_service.h.
  lastX_ = static_cast<std::int16_t>(point[1] | (point[2] << 8));
  lastY_ = static_cast<std::int16_t>(point[3] | (point[4] << 8));
  pressed_ = true;
  x = lastX_;
  y = lastY_;
  return true;
}

#endif  // ESP32_8048S050C
