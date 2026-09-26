#pragma once

#include <cstdint>

// Main controller GPIO map. ESP32 DevKit V1. See spec/puzzlebox_hw.md
// section 2 for wiring notes, strap-pin warnings, and power rules.
//
// Pins go here — and then actually get used. MatrixService and RfidService
// are documented exceptions that still hardcode their pins in member
// initialisers; do not add a third.

namespace pins {

// TCS3200/TCS230 colour sensor. GPIO0/GPIO2 are ESP32 strap pins — see the
// warning in ColorSensorService before wiring this up.
inline constexpr std::uint8_t kColorSensorS2 = 0;
inline constexpr std::uint8_t kColorSensorS3 = 2;
inline constexpr std::uint8_t kColorSensorOut = 34;  // input-only, no pull-up needed (pure input)

// TM1638 LED&KEY, 3-wire bit-banged.
inline constexpr std::uint8_t kTm1638Stb = 4;
inline constexpr std::uint8_t kTm1638Clk = 5;   // strap pin, must be HIGH at boot
inline constexpr std::uint8_t kTm1638Dio = 26;  // bidirectional

// WS2812 LED matrices, single data line, 128 pixels total.
inline constexpr std::uint8_t kMatrixData = 13;

// Hidden trigger sensors, both active LOW.
inline constexpr std::uint8_t kHiddenSensor2 = 14;  // INPUT_PULLUP
inline constexpr std::uint8_t kHiddenSensor1 = 35;  // input-only, needs external 10k pull-up

// Vibration motor driver board, active HIGH.
inline constexpr std::uint8_t kVibrationIn = 15;

// RC522 RFID reader on VSPI.
inline constexpr std::uint8_t kRfidSs = 16;
inline constexpr std::uint8_t kRfidRst = 17;
inline constexpr std::uint8_t kRfidSck = 18;
inline constexpr std::uint8_t kRfidMiso = 19;
inline constexpr std::uint8_t kRfidMosi = 23;

// I2C bus: RTC (0x68) + MCP23017 (0x20) + PCA9685 (0x40).
inline constexpr std::uint8_t kI2cSda = 21;
inline constexpr std::uint8_t kI2cScl = 22;

// Servo lock, LEDC PWM 50 Hz.
inline constexpr std::uint8_t kServoSignal = 25;

// Declared but unused: the three rotary encoders moved to the MCP23017.
// These pins are physically free. Nothing reads these constants.
inline constexpr std::uint8_t kEncoderSwitch = 27;
inline constexpr std::uint8_t kEncoderA = 32;
inline constexpr std::uint8_t kEncoderB = 33;

}  // namespace pins

namespace i2c_addr {

inline constexpr std::uint8_t kRtc = 0x68;
inline constexpr std::uint8_t kMcp23017 = 0x20;
inline constexpr std::uint8_t kPca9685 = 0x40;

}  // namespace i2c_addr
