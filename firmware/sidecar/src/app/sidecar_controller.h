#pragma once

#include <U8g2lib.h>

#include <Arduino.h>

#include "diagnostics/event_log.h"
#include "protocol/game_state.h"

// ABRobot ESP32-C3 with onboard 0.42" OLED. Output-only status beacon: no
// inputs, cannot influence the game. See spec/puzzlebox_hw.md section 5.
class SidecarController {
 public:
  void begin();
  void tick();  // call every loop

  // Called by the ESP-NOW receive callback (see sidecar/src/main.cpp).
  void onControllerHeartbeat();

  // Exposed so sidecar/src/main.cpp can log events (e.g. esp_now_init
  // failure) that happen outside this class but after begin().
  diagnostics::EventLog& eventLog() { return eventLog_; }

 private:
  enum class Scene {
    kSearching,  // no controller found yet
    kIdleBriefingMaintenance,
    kActive,
    kPaused,
    kTimeout,
    kSuccess,
  };

  void sendHello();
  void tickLed();
  void redrawOled();
  std::uint32_t blinkIntervalMs() const;

  U8G2_SSD1306_72X40_ER_F_HW_I2C display_{U8G2_R0, /*reset=*/U8X8_PIN_NONE};
  diagnostics::EventLog eventLog_;

  Scene scene_ = Scene::kSearching;
  bool controllerLinked_ = false;
  std::uint32_t lastHeartbeatMs_ = 0;
  std::uint32_t lastHelloMs_ = 0;

  bool ledOn_ = false;
  std::uint32_t lastLedToggleMs_ = 0;

  std::uint32_t lastOledDrawMs_ = 0;
  bool showIpNotVersion_ = true;
  std::uint32_t lastIpVerSwapMs_ = 0;
};
