#pragma once

#include <Arduino.h>
#include <RTClib.h>

// DS3231 (preferred) or DS1307 on I2C 0x68. Read-only: this service never
// sets the RTC. See spec/puzzlebox_hw.md section 4.3.
class RtcService {
 public:
  void begin();
  void poll();  // refreshes at most once per second

  bool isReady() const { return ready_; }
  String formattedNow() const { return formattedNow_; }

 private:
  enum class Kind { kNone, kDs3231, kDs1307 };

  RTC_DS3231 ds3231_;
  RTC_DS1307 ds1307_;
  Kind kind_ = Kind::kNone;

  bool ready_ = false;
  String formattedNow_ = "niet beschikbaar";
  unsigned long lastRefreshMs_ = 0;
};
