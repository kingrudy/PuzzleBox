#pragma once

#include "esp32_fix.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include <array>
#include <cstdint>

#include "devices/audio_cue_bus.h"
#include "devices/color_sensor_service.h"
#include "devices/highscore_service.h"
#include "devices/hidden_trigger_service.h"
#include "devices/input_panel_service.h"
#include "devices/matrix_service.h"
#include "devices/rfid_service.h"
#include "devices/rtc_service.h"
#include "devices/servo_service.h"
#include "devices/vibration_service.h"
#include "diagnostics/event_log.h"
#include "protocol/game_state.h"
#include "puzzles/finale_puzzle.h"
#include "puzzles/living_interval_puzzle.h"
#include "puzzles/pattern_puzzle.h"
#include "puzzles/puzzle.h"
#include "puzzles/resonant_grid_puzzle.h"
#include "puzzles/spectral_tuner_puzzle.h"
#include "puzzles/tetris_puzzle.h"
#include "puzzles/vibrational_cipher_puzzle.h"

// Owns every device service, the full 7-puzzle game (docs/puzzles/plan.md),
// and the operator web API. Flow: Setup (pick a time limit, encoder 1 +
// TM1638 button to start) -> Countdown -> Active (6 puzzles in a shuffled
// order, then the fixed Finale recap) -> Success (score = remaining
// seconds) -> HighscoreEntry (if it qualifies) -> back to Setup. See
// docs/puzzles/plan.md section 4 for why PuzzleId no longer includes
// Rfid/Rtc/Hidden/FinalLock — every puzzle here uses only hardware marked
// `Connected` in spec/components.md.
class AppController {
 public:
  void begin();
  void tick();  // call every loop

 private:
  void updateOutputsForState();
  void syncDiagnostics();
  void handleGameStatus(AsyncWebServerRequest *request);
  void handleTouchPost(AsyncWebServerRequest *request);
  void handleLogsGet(AsyncWebServerRequest *request);
  void handleDebugPageGet(AsyncWebServerRequest *request);
  void handleDebugStatusGet(AsyncWebServerRequest *request);
  void handleDebugTestPost(AsyncWebServerRequest *request);
  void applyPendingDebugTest();

  void tickSetup();
  void tickPuzzleSelection();
  void tickCountdown();
  void tickActive();
  void tickSuccess();
  void tickHighscoreEntry();
  void tickTimeout();

  void shufflePuzzleOrder();
  void beginPuzzle(std::uint8_t index);
  void beginSinglePuzzle(protocol::PuzzleId puzzleId);
  void advanceToNextPuzzle();
  void finishRun();

  puzzles::Puzzle* currentPuzzleForStatus() const;

  protocol::GameState state_ = protocol::GameState::Boot;
  std::uint32_t remainingSeconds_ = 0;  // room clock; meaningful only from Countdown's end onward
  bool sidecarOnline_ = false;

  // --- device services ---
  // Matrix/RFID/RTC/hidden-sensor/colour-sensor/servo are the box's original
  // hardware skeleton (spec/components.md) — none of the 7 puzzles below
  // depend on them (see docs/puzzles/plan.md section 1), but they're kept
  // alive here since they already degrade gracefully when their hardware is
  // absent, exactly as designed.
  MatrixService matrixService_;
  RfidService rfidService_;
  RtcService rtcService_;
  HiddenTriggerService hiddenTriggerService_;
  ColorSensorService colorSensorService_;
  ServoService servoService_;

  InputPanelService inputPanelService_;
  VibrationService vibrationService_;
  AudioCueBus audioCueBus_;
  HighscoreService highscoreService_;
  diagnostics::EventLog eventLog_;

  // --- the 7 puzzles ---
  PatternPuzzle patternPuzzle_;
  ResonantGridPuzzle resonantGridPuzzle_;
  VibrationalCipherPuzzle vibrationalCipherPuzzle_;
  SpectralTunerPuzzle spectralTunerPuzzle_;
  LivingIntervalPuzzle livingIntervalPuzzle_;
  TetrisPuzzle tetrisPuzzle_;
  FinalePuzzle finalePuzzle_;

  static constexpr std::uint8_t kShuffledCount = 6;  // every puzzle except Finale
  std::array<puzzles::Puzzle*, kShuffledCount> shuffledPuzzles_{};
  std::uint8_t currentPuzzleIndex_ = 0;  // 0..5 = shuffled puzzles, 6 = Finale
  puzzles::Puzzle* currentPuzzle_ = nullptr;
  std::uint32_t currentPuzzleStartMs_ = 0;
  std::array<std::uint8_t, FinalePuzzle::kDigitCount> collectedDigits_{};
  puzzles::Difficulty difficulty_ = puzzles::Difficulty::kMedium;

  // --- Setup: time-limit selection ---
  std::uint32_t selectedLimitSeconds_ = 900;  // default 15:00
  static constexpr std::uint32_t kMinLimitSeconds = 300;   // 5:00
  static constexpr std::uint32_t kMaxLimitSeconds = 1800;  // 30:00
  static constexpr std::uint32_t kLimitStepSeconds = 60;
  std::int32_t setupEncoderBaseline_ = 0;

  // --- Puzzle selection (test mode, S1 at startup) ---
  bool puzzleSelectionMode_ = false;
  std::uint8_t selectedPuzzleIndex_ = 0;  // 0-6 for the 7 puzzles
  std::int32_t puzzleSelectionEncoderBaseline_ = 0;
  bool singlePuzzleTestMode_ = false;  // true when testing a single puzzle

  // --- Countdown ---
  static constexpr std::uint32_t kCountdownSeconds = 5;
  std::uint32_t countdownStartMs_ = 0;
  std::uint32_t lastCountdownValue_ = 0xFFFFFFFFu;

  // --- room clock ---
  std::uint32_t lastClockTickMs_ = 0;

  // --- Success / score / highscore ---
  std::uint32_t lastScoreSeconds_ = 0;
  std::uint32_t successEnteredMs_ = 0;
  char highscoreLetters_[3] = {'A', 'A', 'A'};
  std::array<std::int32_t, 3> highscoreEncoderBaseline_{};

  // --- touch bridge (relayed from the display over POST /api/touch) ---
  bool touchPending_ = false;
  std::uint8_t touchCellIndex_ = 0;

  // --- debug-page test bridge (POST /api/debug/test) ---
  // The handler only validates and stores the request; applyPendingDebugTest()
  // does the actual hardware call from AppController::tick() on the main loop
  // task, since AsyncWebServer callbacks run on their own task and several of
  // these calls (encoder LEDs) hit the shared I2C bus that poll() also uses.
  struct PendingDebugTest {
    bool pending = false;
    char component[16] = {0};
    char action[8] = {0};
    std::uint8_t index = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
  };
  PendingDebugTest pendingDebugTest_;

  AsyncWebServer webServer_{80};
};
