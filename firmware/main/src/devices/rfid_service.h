#pragma once

#include <MFRC522.h>
#include <SPI.h>

#include <Arduino.h>

// MFRC522 RFID reader on VSPI, 3.3V only. See spec/puzzlebox_hw.md
// section 4.2. Presence is auto-detected in begin(): a missing or
// miswired reader degrades silently instead of hanging poll().
class RfidService {
 public:
  void begin();
  void poll();

  // Non-empty only on a NEW scan; consumes the event. Call exactly once per
  // loop.
  String takeEventTag();

  // Sticky: last UID seen, or "geen scan" if none yet.
  String lastSeenTag() const { return lastSeenTag_; }

  bool isReady() const { return ready_; }

 private:
  static String formatUid(const MFRC522::Uid& uid);

  // Pins 16 (SS), 17 (RST) hardcoded here to match the documented (known)
  // discrepancy with device_pins.h — see spec/puzzlebox_hw.md section 11.
  MFRC522 reader_{16, 17};

  bool ready_ = false;
  String lastSeenTag_ = "geen scan";
  String pendingEventTag_;
};
