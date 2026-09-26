#pragma once

#include <cstdint>

// I2C expander / LED driver map for the three rotary encoders. Both chips
// sit on the shared I2C bus (device_pins.h: kI2cSda/kI2cScl). See
// spec/puzzlebox_hw.md section 4.5.

namespace encoder_hw {

inline constexpr std::uint8_t kEncoderCount = 3;

// --- MCP23017 @ 0x20 — encoder inputs, all pins pulled up except GPB0. ---
// GPA0-1: encoder 1 A/B   GPA2: encoder 1 SW (active LOW)
// GPA3-4: encoder 2 A/B   GPA5: encoder 2 SW (active LOW)
// GPA6-7: encoder 3 A/B   GPB0: encoder 3 SW (active HIGH, external 10k pull-down)
struct EncoderInputPins {
  std::uint8_t a;
  std::uint8_t b;
  std::uint8_t sw;
};

// MCP23017 pin numbering: GPA0-7 = 0-7, GPB0-7 = 8-15.
inline constexpr EncoderInputPins kEncoderInputs[kEncoderCount] = {
    {0, 1, 2},  // encoder 1
    {3, 4, 5},  // encoder 2
    {6, 7, 8},  // encoder 3 -> GPB0
};

// Encoder 3's button is wired inversely to the other two: external pull-down
// to GND, switches to 3.3V, so its active level is HIGH instead of LOW. If
// you rewire encoder 3 to match encoders 1 and 2, this table (and the
// pollEncoders() logic that reads it) must change together.
inline constexpr bool kEncoderSwitchActiveHigh[kEncoderCount] = {false, false, true};

// --- PCA9685 @ 0x40 — encoder LEDs. ---
inline constexpr std::uint16_t kPwmFrequencyHz = 1600;
inline constexpr std::uint32_t kPwmOscillatorHz = 27000000;
inline constexpr std::uint16_t kPwmResolution = 4096;  // 12-bit

// Encoders 1 and 2: bi-colour, common cathode (active high), no blue channel.
// Encoder 3: RGB, common anode (active low -> PWM inverted in software).
struct EncoderLedChannels {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;       // 0xFF = no blue channel
  bool commonAnode;
};

inline constexpr std::uint8_t kNoChannel = 0xFF;

inline constexpr EncoderLedChannels kEncoderLedChannels[kEncoderCount] = {
    {0, 1, kNoChannel, false},  // encoder 1
    {2, 3, kNoChannel, false},  // encoder 2
    {4, 5, 6, true},            // encoder 3
};
// Channels 7-15 are free.

inline int getColorChannelCount(std::uint8_t encoderIndex) {
  return kEncoderLedChannels[encoderIndex].b == kNoChannel ? 2 : 3;
}

// Brightness calibration derived from the series resistors. If you change a
// resistor, update the matching multiplier — the firmware has no way to
// detect it.
inline constexpr float kBicolorRedMultiplier = 1.00f;    // 330 ohm
inline constexpr float kBicolorGreenMultiplier = 0.45f;  // 150 ohm
inline constexpr float kRgbBlueMultiplier = 0.41f;       // 135 ohm, encoder 3 only

// Boot self-test duration (encoder 1 red, 2 green, 3 blue, then all off).
inline constexpr std::uint32_t kBootSelfTestMs = 1500;

// Animation slots per encoder — a 5th queued animation overwrites slot 4.
inline constexpr int kAnimationSlotsPerEncoder = 4;

}  // namespace encoder_hw
