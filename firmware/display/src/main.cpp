#include <Arduino.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "config/network_config.h"
#include "diagnostics/event_log.h"
#include "display_board_profile.h"
#include "display_speaker_service.h"
#include "game_view.h"
#include "touch_service.h"

#if defined(ESP32_8048S050C)
// Required crash fix — do not remove. The RGB panel driver needs more stack
// than the default loop task provides.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif

namespace {

diagnostics::EventLog eventLog;

#if defined(ESP32_8048S050C)
GameView gameView;
DisplaySpeakerService speakerService;
TouchService touchService;
#endif

constexpr std::uint8_t kBacklightLedcChannel = 0;
constexpr std::uint32_t kBacklightLedcFreqHz = 5000;
constexpr std::uint8_t kBacklightLedcResolutionBits = 8;

std::uint32_t lastPollMs = 0;
constexpr std::uint32_t kOnlinePollIntervalMs = 750;
constexpr std::uint32_t kOfflineRetryIntervalMs = 1500;
bool wifiWasConnected = false;

void initBacklight() {
  ledcSetup(kBacklightLedcChannel, kBacklightLedcFreqHz, kBacklightLedcResolutionBits);
  ledcAttachPin(display_board::kBacklightPin, kBacklightLedcChannel);
  const std::uint32_t maxDuty = (1u << kBacklightLedcResolutionBits) - 1;
  ledcWrite(kBacklightLedcChannel, static_cast<std::uint32_t>(display_board::kBacklightDuty * maxDuty));
}

void printBootBanner() {
  Serial.println("[display] Chronolab display node ready");
  Serial.printf("board=%s | %s | %ux%u | %s\n", display_board::kBoardName,
                 display_board::kHasTouch ? "RGB panel + GT911 touch" : "GC9A01 SPI",
                 display_board::kWidth, display_board::kHeight,
                 display_board::kRound ? "round" : "rect");

#if defined(ESP32_8048S050C)
  Serial.printf("pins=BCKL %u | RGB HSYNC%u VSYNC%u DE%u PCLK%u\n", display_board::kBacklightPin,
                 display_board::kRgbHsync, display_board::kRgbVsync, display_board::kRgbDe,
                 display_board::kRgbPclk);
#elif defined(ESP32_2424S012N)
  Serial.printf("pins=BCKL %u | SPI MOSI%u MISO- SCLK%u CS%u DC%u RST-\n",
                 display_board::kBacklightPin, display_board::kSpiMosi, display_board::kSpiSclk,
                 display_board::kSpiCs, display_board::kSpiDc);
#endif

  // Full pinout already went to Serial above; the persisted event just
  // records that this node booted and which board it is.
  eventLog.logf("display", "%s ready, %ux%u", display_board::kBoardName, display_board::kWidth,
                display_board::kHeight);
}

void pollMainController() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(800);
  http.begin(String("http://") + config::kMainControllerIp + "/api/status");
  const int code = http.GET();
  if (code > 0) {
    Serial.printf("[display] /api/status -> %d (%d bytes)\n", code, http.getSize());
  }
  http.end();
}

}  // namespace

void setup() {
  Serial.begin(115200);

  eventLog.begin("evtlog_disp");

  printBootBanner();

  initBacklight();

#if defined(ESP32_8048S050C)
  speakerService.begin(eventLog);
  touchService.begin(eventLog);
  gameView.begin(speakerService, touchService);
#endif

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // default modem-sleep adds latency/jitter on this poll-driven link
  WiFi.begin(config::kApSsid, config::kApPassword);
}

void loop() {
  eventLog.pollSerialCommand();  // "log"/"logs" over Serial dumps history

  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiWasConnected) {
    wifiWasConnected = connected;
    eventLog.logf("display", connected ? "Wi-Fi connected" : "Wi-Fi lost");
  }

#if defined(ESP32_2424S012N)
  // The round display has no game-specific view yet (no touch, no audio —
  // see spec/components.md) — it stays a plain connectivity heartbeat.
  const std::uint32_t now = millis();
  const std::uint32_t interval = connected ? kOnlinePollIntervalMs : kOfflineRetryIntervalMs;
  if (now - lastPollMs >= interval) {
    lastPollMs = now;
    pollMainController();
  }
#endif

#if defined(ESP32_8048S050C)
  // gameView.poll() drives lv_timer_handler() every call, which reads
  // touchService through the LVGL indev wired up in gameView.begin() — no
  // separate touch-polling here.
  gameView.poll();
  speakerService.poll();
#endif
}
