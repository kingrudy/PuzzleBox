#pragma once

#include <cstdint>

// TCS3200/TCS230 colour sensor. S0/S1/OE are hard-strapped in hardware (20%
// output scaling, always enabled) — see spec/puzzlebox_hw.md section 4.7
// for the full strapping table and the GPIO0/GPIO2 boot-strap warning.
//
// Not yet bound to a puzzle; reported in diagnostics only.
class ColorSensorService {
 public:
  enum class Color {
    kWhite,
    kYellow,
    kRed,
    kGreen,
    kBlue,
    kUnknown,
    kNoSignal,
  };

  void begin();
  void poll();  // every loop, self-rate-limited to 250ms

  bool hasSignal() const { return color_ != Color::kNoSignal; }
  Color detectedColor() const { return color_; }
  const char* detectedColorLabel() const;

  std::uint32_t redFrequencyHz() const { return redHz_; }
  std::uint32_t greenFrequencyHz() const { return greenHz_; }
  std::uint32_t blueFrequencyHz() const { return blueHz_; }

 private:
  static constexpr std::uint32_t kSampleIntervalMs = 250;
  static constexpr std::uint32_t kPulseTimeoutUs = 8000;
  static constexpr std::uint32_t kFilterSettleUs = 300;
  static constexpr std::uint32_t kSignalFloorHz = 40;
  // While nothing is plugged in, every measureFrequency() call times out on
  // all 12 pulseIn()s (worst case ~96ms) at kSampleIntervalMs, i.e. every
  // 250ms -- a large, avoidable chunk of the main loop for absent hardware.
  // After a few consecutive NoSignal reads, back off to a much slower
  // recheck; any real reading resets the streak and restores full speed.
  // Hardware-confirmed: even backed off, this ~96-102ms stall was still
  // frequent enough (every 3s) to be the dominant remaining source of
  // Setup-screen encoder lag once the display's WS push latency was fixed
  // -- 15s keeps hot-plug detection working but nearly eliminates the
  // chance of a stall landing during a few-seconds interaction.
  static constexpr std::uint8_t kNoSignalStreakForBackoff = 4;
  static constexpr std::uint32_t kBackoffIntervalMs = 15000;

  std::uint32_t measureFrequency(bool s2High, bool s3High);
  Color classify(std::uint32_t r, std::uint32_t g, std::uint32_t b) const;

  std::uint32_t redHz_ = 0;
  std::uint32_t greenHz_ = 0;
  std::uint32_t blueHz_ = 0;
  Color color_ = Color::kNoSignal;
  unsigned long lastSampleMs_ = 0;
  std::uint8_t consecutiveNoSignal_ = 0;
};
