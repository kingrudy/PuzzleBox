#pragma once

// Two hidden puzzle triggers (reed switch / hall sensor / plain contact),
// both active LOW, no debouncing. See spec/puzzlebox_hw.md section 4.6.
// Sensor 1 (GPIO35) is input-only and needs an external 10k pull-up to
// 3.3V — without it, it floats and reports random triggers.
class HiddenTriggerService {
 public:
  void begin();
  void poll();

  bool sensor1Active() const { return sensor1Active_; }
  bool sensor2Active() const { return sensor2Active_; }

 private:
  bool sensor1Active_ = false;
  bool sensor2Active_ = false;
};
