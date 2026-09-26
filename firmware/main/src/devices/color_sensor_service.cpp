#include "devices/color_sensor_service.h"

#include <Arduino.h>

#include "devices/device_pins.h"

void ColorSensorService::begin() {
  pinMode(pins::kColorSensorS2, OUTPUT);
  pinMode(pins::kColorSensorS3, OUTPUT);
  pinMode(pins::kColorSensorOut, INPUT);  // pure input, correct for input-only pin
  digitalWrite(pins::kColorSensorS2, HIGH);  // must stay HIGH at boot (strap pin)
}

std::uint32_t ColorSensorService::measureFrequency(bool s2High, bool s3High) {
  digitalWrite(pins::kColorSensorS2, s2High ? HIGH : LOW);
  digitalWrite(pins::kColorSensorS3, s3High ? HIGH : LOW);
  delayMicroseconds(kFilterSettleUs);

  std::uint32_t total = 0;
  constexpr int kSamplesPerFilter = 2;
  for (int i = 0; i < kSamplesPerFilter; ++i) {
    const unsigned long periodUs =
        pulseIn(pins::kColorSensorOut, HIGH, kPulseTimeoutUs) +
        pulseIn(pins::kColorSensorOut, LOW, kPulseTimeoutUs);
    if (periodUs > 0) {
      total += 1000000UL / periodUs;
    }
  }
  return total / kSamplesPerFilter;
}

void ColorSensorService::poll() {
  const unsigned long now = millis();
  const std::uint32_t interval =
      consecutiveNoSignal_ >= kNoSignalStreakForBackoff ? kBackoffIntervalMs : kSampleIntervalMs;
  if (now - lastSampleMs_ < interval) {
    return;
  }
  lastSampleMs_ = now;

  redHz_ = measureFrequency(/*s2=*/false, /*s3=*/false);
  greenHz_ = measureFrequency(/*s2=*/true, /*s3=*/true);
  blueHz_ = measureFrequency(/*s2=*/false, /*s3=*/true);

  color_ = classify(redHz_, greenHz_, blueHz_);
  if (color_ == Color::kNoSignal) {
    if (consecutiveNoSignal_ < 255) ++consecutiveNoSignal_;
  } else {
    consecutiveNoSignal_ = 0;
  }
}

ColorSensorService::Color ColorSensorService::classify(std::uint32_t r, std::uint32_t g,
                                                         std::uint32_t b) const {
  const std::uint32_t maxVal = max(r, max(g, b));
  const std::uint32_t minVal = min(r, min(g, b));

  if (maxVal < kSignalFloorHz) {
    return Color::kNoSignal;
  }
  if (maxVal <= minVal + maxVal / 5) {
    return Color::kWhite;
  }
  if (r > (13 * b) / 10 && g > (12 * b) / 10 &&
      (r > g ? r - g : g - r) <= max(r, g) / 3) {
    return Color::kYellow;
  }
  if (r > (12 * g) / 10 && r > (12 * b) / 10) {
    return Color::kRed;
  }
  if (g > (12 * r) / 10 && g > (12 * b) / 10) {
    return Color::kGreen;
  }
  if (b > (12 * r) / 10 && b > (12 * g) / 10) {
    return Color::kBlue;
  }
  return Color::kUnknown;
}

const char* ColorSensorService::detectedColorLabel() const {
  switch (color_) {
    case Color::kWhite:
      return "wit";
    case Color::kYellow:
      return "geel";
    case Color::kRed:
      return "rood";
    case Color::kGreen:
      return "groen";
    case Color::kBlue:
      return "blauw";
    case Color::kNoSignal:
      return "geen signaal";
    case Color::kUnknown:
    default:
      return "onbekend";
  }
}
