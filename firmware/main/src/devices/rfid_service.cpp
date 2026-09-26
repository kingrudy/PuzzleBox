#include "devices/rfid_service.h"

#include "devices/device_pins.h"

void RfidService::begin() {
  SPI.begin(pins::kRfidSck, pins::kRfidMiso, pins::kRfidMosi, pins::kRfidSs);
  reader_.PCD_Init();

  const std::uint8_t version = reader_.PCD_ReadRegister(MFRC522::VersionReg);
  ready_ = version != 0x00 && version != 0xFF;
}

void RfidService::poll() {
  if (!ready_) {
    return;
  }
  if (!reader_.PICC_IsNewCardPresent() || !reader_.PICC_ReadCardSerial()) {
    return;
  }

  lastSeenTag_ = formatUid(reader_.uid);
  pendingEventTag_ = lastSeenTag_;

  reader_.PICC_HaltA();
  reader_.PCD_StopCrypto1();
}

String RfidService::takeEventTag() {
  String tag = pendingEventTag_;
  pendingEventTag_ = "";
  return tag;
}

String RfidService::formatUid(const MFRC522::Uid& uid) {
  String out;
  out.reserve(uid.size * 2);
  for (std::uint8_t i = 0; i < uid.size; ++i) {
    char buf[3];
    snprintf(buf, sizeof(buf), "%02X", uid.uidByte[i]);
    out += buf;
  }
  return out;
}
