#include "devices/vibration_service.h"

#include <Arduino.h>

#include "devices/device_pins.h"

void VibrationService::begin() {
  pinMode(pins::kVibrationIn, OUTPUT);
  digitalWrite(pins::kVibrationIn, LOW);
}

void VibrationService::tick() {
  if (active_ && millis() >= pulseEndMs_) {
    stop();
  }
}

void VibrationService::pulse(std::uint32_t durationMs) {
  digitalWrite(pins::kVibrationIn, HIGH);
  active_ = true;
  pulseEndMs_ = millis() + durationMs;
}

void VibrationService::stop() {
  digitalWrite(pins::kVibrationIn, LOW);
  active_ = false;
}
