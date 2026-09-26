#include "app/sidecar_controller.h"

#include <Wire.h>

#include "protocol/espnow_protocol.h"

namespace {
constexpr std::uint8_t kOledSda = 5;
constexpr std::uint8_t kOledScl = 6;
constexpr std::uint8_t kOnboardLed = 8;  // active LOW
}  // namespace

void SidecarController::begin() {
  Serial.begin(115200);

  eventLog_.begin("evtlog_side");

  pinMode(kOnboardLed, OUTPUT);
  digitalWrite(kOnboardLed, HIGH);  // active LOW: HIGH = off

  Wire.begin(kOledSda, kOledScl);
  display_.begin();
  display_.setFont(u8g2_font_4x6_tr);

  eventLog_.logf("sidecar", "C3 sidecar ready for ESP-NOW sync");
}

void SidecarController::onControllerHeartbeat() {
  if (!controllerLinked_) {
    eventLog_.logf("sidecar", "controller link established");
  }
  controllerLinked_ = true;
  lastHeartbeatMs_ = millis();
}

void SidecarController::sendHello() {
  const std::uint32_t now = millis();
  if (now - lastHelloMs_ < protocol::kHelloIntervalMs) {
    return;
  }
  lastHelloMs_ = now;
  // Actual ESP-NOW send (esp_now_send with a Hello PacketEnvelope) lives in
  // main.cpp, which owns the peer table and the receive callback.
}

std::uint32_t SidecarController::blinkIntervalMs() const {
  switch (scene_) {
    case Scene::kSearching: return 180;
    case Scene::kActive: return 250;
    case Scene::kTimeout: return 120;
    case Scene::kPaused: return 500;
    case Scene::kIdleBriefingMaintenance: return 700;
    case Scene::kSuccess: return 0;  // solid on
  }
  return 700;
}

void SidecarController::tickLed() {
  if (scene_ == Scene::kSuccess) {
    digitalWrite(kOnboardLed, LOW);  // solid on
    return;
  }

  const std::uint32_t now = millis();
  const std::uint32_t interval = blinkIntervalMs();
  if (now - lastLedToggleMs_ >= interval) {
    lastLedToggleMs_ = now;
    ledOn_ = !ledOn_;
    digitalWrite(kOnboardLed, ledOn_ ? LOW : HIGH);
  }
}

void SidecarController::redrawOled() {
  const std::uint32_t now = millis();
  if (now - lastOledDrawMs_ < 250) {
    return;
  }
  lastOledDrawMs_ = now;

  if (now - lastIpVerSwapMs_ > 4000) {
    lastIpVerSwapMs_ = now;
    showIpNotVersion_ = !showIpNotVersion_;
  }

  display_.clearBuffer();
  display_.drawStr(0, 6, controllerLinked_ ? "CTRL:OK" : "CTRL:--");
  display_.drawStr(0, 12, "SCN :---");
  display_.drawStr(0, 18, "TMR :--:--");
  display_.drawStr(0, 24, showIpNotVersion_ ? "IP  :192.168.4.210" : "VER :dev");
  display_.drawStr(0, 30, "OTA :READY");
  display_.drawStr(0, 36, "PULL:NONE");
  display_.sendBuffer();
}

void SidecarController::tick() {
  eventLog_.pollSerialCommand();  // "log"/"logs" over Serial dumps history

  if (!controllerLinked_) {
    sendHello();
  } else if (millis() - lastHeartbeatMs_ > protocol::kPeerLostTimeoutMs) {
    controllerLinked_ = false;
    scene_ = Scene::kSearching;
    eventLog_.logf("sidecar", "controller link lost (heartbeat timeout)");
  }

  tickLed();
  redrawOled();
}
