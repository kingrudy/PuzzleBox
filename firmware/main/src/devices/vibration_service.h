#pragma once

#include <cstdint>

// OPEN-SMART vibration driver board, active HIGH, non-blocking one-shot
// pulses. See spec/puzzlebox_hw.md section 4.9.
//
// If you stop calling tick(), the motor never stops — pulse end is polled,
// not interrupt-driven.
class VibrationService {
 public:
  void begin();
  void tick();  // call every loop; this is what ends the pulse

  void pulse(std::uint32_t durationMs);  // a new pulse restarts the timer
  void stop();

  bool isActive() const { return active_; }

 private:
  bool active_ = false;
  std::uint32_t pulseEndMs_ = 0;
};
