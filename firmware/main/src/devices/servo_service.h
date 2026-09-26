#pragma once

#include <ESP32Servo.h>

// MG946R-class servo lock, powered from a separate 5V rail. See
// spec/puzzlebox_hw.md section 4.8.
//
// The servo stays attached and powered at its target angle — it is never
// detached, so it holds torque continuously.
class ServoService {
 public:
  void begin();

  void open();
  void close();
  bool isOpen() const { return isOpen_; }

 private:
  static constexpr int kClosedDegrees = 12;
  static constexpr int kOpenDegrees = 96;
  static constexpr int kMinPulseUs = 500;
  static constexpr int kMaxPulseUs = 2400;

  Servo servo_;
  bool isOpen_ = false;
};
