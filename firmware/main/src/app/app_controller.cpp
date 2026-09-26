#include "app/app_controller.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_system.h>

#include <cstring>

#include "config/network_config.h"
#include "devices/device_pins.h"
#include "devices/encoder_hardware_config.h"
#include "web/debug_page.h"

namespace {
constexpr std::uint32_t kSuccessDisplayMs = 3000;

// Free-heap / loop-time telemetry — the firmware previously had none, which
// made "is it overloaded?" unanswerable from the serial log alone. Printed
// every 5s so a slow heap drain (fragmentation from repeated JSON building
// in handleGameStatus()) or a ballooning loop time shows up directly instead
// of being guessed at.
constexpr std::uint32_t kDiagIntervalMs = 5000;
std::uint32_t lastDiagMs = 0;
std::uint32_t maxLoopUs = 0;
}  // namespace

void AppController::begin() {
  Serial.begin(115200);

  eventLog_.begin("evtlog_main");

  // Wire.begin() is also called inside RtcService::begin() — harmless, but
  // it guarantees the I2C bus is initialised before any device driver runs.
  Wire.begin(pins::kI2cSda, pins::kI2cScl);

  colorSensorService_.begin();
  matrixService_.begin();
  rfidService_.begin();
  rtcService_.begin();
  hiddenTriggerService_.begin();
  servoService_.begin();
  vibrationService_.begin();

  inputPanelService_.begin();
  eventLog_.logf("input_panel", "MCP23017 ready, 3 encoders configured");

  highscoreService_.begin();
  eventLog_.logf("highscore", "%u entries loaded from NVS", highscoreService_.count());

  WiFi.mode(WIFI_MODE_APSTA);
  WiFi.softAP(config::kApSsid, config::kApPassword, config::kAccessPointChannel);
  eventLog_.logf("web", "AP ready on %s", config::kMainControllerIp);

  webServer_.on("/api/game", HTTP_GET, [this](AsyncWebServerRequest *request) { handleGameStatus(request); });
  webServer_.on("/api/touch", HTTP_POST, [this](AsyncWebServerRequest *request) { handleTouchPost(request); });
  webServer_.on("/api/logs", HTTP_GET, [this](AsyncWebServerRequest *request) { handleLogsGet(request); });
  webServer_.on("/debug", HTTP_GET, [this](AsyncWebServerRequest *request) { handleDebugPageGet(request); });
  webServer_.on("/api/debug", HTTP_GET, [this](AsyncWebServerRequest *request) { handleDebugStatusGet(request); });
  webServer_.on("/api/debug/test", HTTP_POST, [this](AsyncWebServerRequest *request) { handleDebugTestPost(request); });
  webServer_.begin();
  eventLog_.logf("web", "GET /api/game, /api/logs, /debug, POST /api/touch, /api/debug/test ready");

  randomSeed(esp_random());

  state_ = protocol::GameState::Setup;
  puzzleSelectionMode_ = false;
  singlePuzzleTestMode_ = false;
  setupEncoderBaseline_ = inputPanelService_.encoder(0).value;
  updateOutputsForState();

  eventLog_.logf("main", "Chronolab main controller ready");
}

void AppController::tick() {
  const std::uint32_t loopStartUs = micros();

  eventLog_.pollSerialCommand();  // "log"/"logs" over Serial dumps history

  colorSensorService_.poll();
  rfidService_.poll();
  rtcService_.poll();
  hiddenTriggerService_.poll();
  vibrationService_.tick();
  inputPanelService_.poll();
  // AsyncWebServer handles requests asynchronously, no handleClient() call needed

  syncDiagnostics();

  const std::uint32_t loopUs = micros() - loopStartUs;
  if (loopUs > maxLoopUs) {
    maxLoopUs = loopUs;
  }
  if (millis() - lastDiagMs >= kDiagIntervalMs) {
    lastDiagMs = millis();
    Serial.printf("[diag] freeHeap=%u minFreeHeap=%u maxAllocHeap=%u worstLoopUs=%u state=%d\n",
                  ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), maxLoopUs,
                  static_cast<int>(state_));
    maxLoopUs = 0;
  }

  switch (state_) {
    case protocol::GameState::Setup:
      if (puzzleSelectionMode_) {
        tickPuzzleSelection();
      } else {
        tickSetup();
      }
      break;
    case protocol::GameState::Countdown:
      tickCountdown();
      break;
    case protocol::GameState::Active:
      tickActive();
      break;
    case protocol::GameState::Success:
      tickSuccess();
      break;
    case protocol::GameState::HighscoreEntry:
      tickHighscoreEntry();
      break;
    case protocol::GameState::Timeout:
      tickTimeout();
      break;
    default:
      inputPanelService_.renderStatus(state_, remainingSeconds_, sidecarOnline_);
      break;
  }
}

// ---------------------------------------------------------------------------
// Setup — time-limit selection
// ---------------------------------------------------------------------------

void AppController::tickSetup() {
  const std::int32_t steps = inputPanelService_.encoder(0).value - setupEncoderBaseline_;
  if (steps != 0) {
    std::int32_t newLimit = static_cast<std::int32_t>(selectedLimitSeconds_) +
                             steps * static_cast<std::int32_t>(kLimitStepSeconds);
    newLimit = constrain(newLimit, static_cast<std::int32_t>(kMinLimitSeconds),
                          static_cast<std::int32_t>(kMaxLimitSeconds));
    selectedLimitSeconds_ = static_cast<std::uint32_t>(newLimit);
    setupEncoderBaseline_ = inputPanelService_.encoder(0).value;
    audioCueBus_.playTone(320.0f + selectedLimitSeconds_ * 0.1f, 60);
  }

  std::uint8_t btn;
  if (inputPanelService_.takeLedKeyButtonPress(btn)) {
    if (btn == 0) {
      // S1 pressed — enter puzzle selection mode
      puzzleSelectionMode_ = true;
      selectedPuzzleIndex_ = 0;
      puzzleSelectionEncoderBaseline_ = inputPanelService_.encoder(0).value;
      audioCueBus_.playCue(protocol::AudioCueId::Success);
      eventLog_.logf("main", "entering puzzle selection mode");
      return;
    }
    // Any other button — start normal game
    state_ = protocol::GameState::Countdown;
    countdownStartMs_ = millis();
    lastCountdownValue_ = 0xFFFFFFFFu;
    singlePuzzleTestMode_ = false;
    audioCueBus_.playCue(protocol::AudioCueId::RunStart);
    updateOutputsForState();
    return;
  }

  inputPanelService_.renderSetup(selectedLimitSeconds_);
}

// ---------------------------------------------------------------------------
// Puzzle Selection — test mode (S1 at startup)
// ---------------------------------------------------------------------------

void AppController::tickPuzzleSelection() {
  const std::int32_t steps = inputPanelService_.encoder(0).value - puzzleSelectionEncoderBaseline_;
  if (steps != 0) {
    std::int32_t newIndex = static_cast<std::int32_t>(selectedPuzzleIndex_) + steps;
    newIndex = constrain(newIndex, 0, static_cast<std::int32_t>(protocol::kPuzzleCount - 1));
    selectedPuzzleIndex_ = static_cast<std::uint8_t>(newIndex);
    puzzleSelectionEncoderBaseline_ = inputPanelService_.encoder(0).value;
    audioCueBus_.playTone(400.0f + selectedPuzzleIndex_ * 50.0f, 60);
  }

  std::uint8_t btn;
  if (inputPanelService_.takeLedKeyButtonPress(btn)) {
    // Any button press starts the selected puzzle
    const protocol::PuzzleId puzzleIds[] = {
        protocol::PuzzleId::Pattern,
        protocol::PuzzleId::ResonantGrid,
        protocol::PuzzleId::VibrationalCipher,
        protocol::PuzzleId::SpectralTuner,
        protocol::PuzzleId::LivingInterval,
        protocol::PuzzleId::Tetris,
        protocol::PuzzleId::Finale,
    };
    const protocol::PuzzleId selectedId = puzzleIds[selectedPuzzleIndex_];

    state_ = protocol::GameState::Active;
    remainingSeconds_ = selectedLimitSeconds_;
    lastClockTickMs_ = millis();
    singlePuzzleTestMode_ = true;
    beginSinglePuzzle(selectedId);
    updateOutputsForState();
    audioCueBus_.playCue(protocol::AudioCueId::RunStart);
    eventLog_.logf("main", "starting single puzzle test: %s", protocol::puzzleName(selectedId));
    return;
  }

  // Render puzzle selection UI on TM1638: "P" + puzzle number (1-7)
  inputPanelService_.renderStatus(protocol::GameState::Setup, selectedPuzzleIndex_ + 1, false);
}

// ---------------------------------------------------------------------------
// Countdown
// ---------------------------------------------------------------------------

void AppController::tickCountdown() {
  const std::uint32_t elapsedSeconds = (millis() - countdownStartMs_) / 1000;

  if (elapsedSeconds >= kCountdownSeconds) {
    state_ = protocol::GameState::Active;
    remainingSeconds_ = selectedLimitSeconds_;
    lastClockTickMs_ = millis();
    shufflePuzzleOrder();
    currentPuzzleIndex_ = 0;
    beginPuzzle(0);
    updateOutputsForState();
    return;
  }

  const std::uint32_t remaining = kCountdownSeconds - elapsedSeconds;
  if (remaining != lastCountdownValue_) {
    lastCountdownValue_ = remaining;
    audioCueBus_.playCue(protocol::AudioCueId::Countdown);
  }
  inputPanelService_.renderStatus(state_, remaining, sidecarOnline_);
}

// ---------------------------------------------------------------------------
// Active — 6 shuffled puzzles, then the Finale
// ---------------------------------------------------------------------------

void AppController::shufflePuzzleOrder() {
  shuffledPuzzles_ = {&patternPuzzle_,       &resonantGridPuzzle_, &vibrationalCipherPuzzle_,
                      &spectralTunerPuzzle_, &livingIntervalPuzzle_, &tetrisPuzzle_};
  for (std::uint8_t i = kShuffledCount; i > 1; --i) {
    const std::uint8_t j = static_cast<std::uint8_t>(random(i));
    std::swap(shuffledPuzzles_[i - 1], shuffledPuzzles_[j]);
  }
  Serial.print("[game] puzzle order:");
  for (auto* p : shuffledPuzzles_) {
    Serial.printf(" %s", protocol::puzzleName(p->id()));
  }
  Serial.println(" Finale");
  // Full order already went to Serial above; the persisted event just needs
  // to mark "a new run's order was shuffled" (the per-puzzle log lines below
  // record the actual sequence as it's played).
  eventLog_.logf("game", "puzzle order shuffled, starts with %s",
                  protocol::puzzleName(shuffledPuzzles_[0]->id()));
}

void AppController::beginPuzzle(std::uint8_t index) {
  currentPuzzle_ = shuffledPuzzles_[index];
  currentPuzzleStartMs_ = millis();
  puzzles::PuzzleContext ctx{inputPanelService_, vibrationService_, audioCueBus_,
                              puzzles::TouchEvent{}, millis(), 0};
  currentPuzzle_->begin(ctx, difficulty_);
  eventLog_.logf("game", "puzzle %u/%u: %s", index + 1, kShuffledCount,
                  protocol::puzzleName(currentPuzzle_->id()));
}

void AppController::beginSinglePuzzle(protocol::PuzzleId puzzleId) {
  puzzles::Puzzle* puzzle = nullptr;
  switch (puzzleId) {
    case protocol::PuzzleId::Pattern:
      puzzle = &patternPuzzle_;
      break;
    case protocol::PuzzleId::ResonantGrid:
      puzzle = &resonantGridPuzzle_;
      break;
    case protocol::PuzzleId::VibrationalCipher:
      puzzle = &vibrationalCipherPuzzle_;
      break;
    case protocol::PuzzleId::SpectralTuner:
      puzzle = &spectralTunerPuzzle_;
      break;
    case protocol::PuzzleId::LivingInterval:
      puzzle = &livingIntervalPuzzle_;
      break;
    case protocol::PuzzleId::Tetris:
      puzzle = &tetrisPuzzle_;
      break;
    case protocol::PuzzleId::Finale:
      // For Finale test, populate with dummy digits
      std::array<std::uint8_t, FinalePuzzle::kDigitCount> testDigits{};
      for (std::uint8_t i = 0; i < FinalePuzzle::kDigitCount; ++i) {
        testDigits[i] = i;
      }
      finalePuzzle_.setCollectedDigits(testDigits);
      puzzle = &finalePuzzle_;
      break;
  }

  currentPuzzle_ = puzzle;
  currentPuzzleIndex_ = 0;
  currentPuzzleStartMs_ = millis();
  puzzles::PuzzleContext ctx{inputPanelService_, vibrationService_, audioCueBus_,
                              puzzles::TouchEvent{}, millis(), 0};
  currentPuzzle_->begin(ctx, difficulty_);
  eventLog_.logf("game", "single puzzle test: %s", protocol::puzzleName(puzzleId));
}

void AppController::advanceToNextPuzzle() {
  if (singlePuzzleTestMode_) {
    // In test mode, solving the puzzle returns to setup
    state_ = protocol::GameState::Setup;
    puzzleSelectionMode_ = false;
    singlePuzzleTestMode_ = false;
    setupEncoderBaseline_ = inputPanelService_.encoder(0).value;
    audioCueBus_.playCue(protocol::AudioCueId::Success);
    updateOutputsForState();
    eventLog_.logf("main", "single puzzle test complete");
    return;
  }

  if (currentPuzzleIndex_ < kShuffledCount) {
    collectedDigits_[currentPuzzleIndex_] = currentPuzzle_->rewardDigit();
  }
  ++currentPuzzleIndex_;

  if (currentPuzzleIndex_ < kShuffledCount) {
    beginPuzzle(currentPuzzleIndex_);
  } else if (currentPuzzleIndex_ == kShuffledCount) {
    finalePuzzle_.setCollectedDigits(collectedDigits_);
    currentPuzzle_ = &finalePuzzle_;
    currentPuzzleStartMs_ = millis();
    puzzles::PuzzleContext ctx{inputPanelService_, vibrationService_, audioCueBus_,
                                puzzles::TouchEvent{}, millis(), 0};
    finalePuzzle_.begin(ctx, difficulty_);
    eventLog_.logf("game", "Finale");
  } else {
    finishRun();
  }
}

void AppController::tickActive() {
  const std::uint32_t now = millis();

  if (now - lastClockTickMs_ >= 1000) {
    const std::uint32_t elapsedSeconds = (now - lastClockTickMs_) / 1000;
    lastClockTickMs_ += elapsedSeconds * 1000;
    remainingSeconds_ = elapsedSeconds >= remainingSeconds_ ? 0 : remainingSeconds_ - elapsedSeconds;
  }

  if (remainingSeconds_ == 0) {
    state_ = protocol::GameState::Timeout;
    audioCueBus_.playCue(protocol::AudioCueId::Error);
    updateOutputsForState();
    return;
  }

  if (currentPuzzle_ == nullptr) {
    return;
  }

  puzzles::PuzzleContext ctx{inputPanelService_,
                              vibrationService_,
                              audioCueBus_,
                              puzzles::TouchEvent{touchPending_, touchCellIndex_},
                              now,
                              now - currentPuzzleStartMs_};
  currentPuzzle_->poll(ctx);
  touchPending_ = false;  // touch events are one-shot, delivered to whichever puzzle is current

  const std::uint32_t penaltyMs = currentPuzzle_->takePenaltyMs();
  if (penaltyMs > 0) {
    const std::uint32_t penaltySeconds = (penaltyMs + 999) / 1000;
    remainingSeconds_ = penaltySeconds >= remainingSeconds_ ? 0 : remainingSeconds_ - penaltySeconds;
    if (remainingSeconds_ == 0) {
      state_ = protocol::GameState::Timeout;
      updateOutputsForState();
      return;
    }
  }

  if (currentPuzzle_->isSolved()) {
    advanceToNextPuzzle();
  }
}

// ---------------------------------------------------------------------------
// Success / Highscore / Timeout
// ---------------------------------------------------------------------------

void AppController::finishRun() {
  state_ = protocol::GameState::Success;
  lastScoreSeconds_ = remainingSeconds_;
  successEnteredMs_ = millis();
  audioCueBus_.playCue(protocol::AudioCueId::Endgame);
  updateOutputsForState();

  for (std::uint8_t i = 0; i < 3; ++i) {
    highscoreEncoderBaseline_[i] = inputPanelService_.encoder(i).value;
  }
  highscoreLetters_[0] = highscoreLetters_[1] = highscoreLetters_[2] = 'A';

  eventLog_.logf("game", "run complete, score=%u s", lastScoreSeconds_);
}

void AppController::tickSuccess() {
  inputPanelService_.renderStatus(state_, lastScoreSeconds_, sidecarOnline_);

  if (millis() - successEnteredMs_ < kSuccessDisplayMs) {
    return;
  }

  if (highscoreService_.qualifies(lastScoreSeconds_)) {
    state_ = protocol::GameState::HighscoreEntry;
  } else {
    state_ = protocol::GameState::Setup;
    puzzleSelectionMode_ = false;
    singlePuzzleTestMode_ = false;
    setupEncoderBaseline_ = inputPanelService_.encoder(0).value;
  }
}

void AppController::tickHighscoreEntry() {
  for (std::uint8_t i = 0; i < 3; ++i) {
    const std::int32_t delta = inputPanelService_.encoder(i).value - highscoreEncoderBaseline_[i];
    if (delta != 0) {
      std::int32_t letterIndex = (highscoreLetters_[i] - 'A' + delta) % 26;
      if (letterIndex < 0) {
        letterIndex += 26;
      }
      highscoreLetters_[i] = static_cast<char>('A' + letterIndex);
      highscoreEncoderBaseline_[i] = inputPanelService_.encoder(i).value;
      audioCueBus_.playTone(400.0f + letterIndex * 12.0f, 60);
    }
  }

  std::uint8_t btn;
  if (inputPanelService_.takeLedKeyButtonPress(btn)) {
    highscoreService_.submit(highscoreLetters_, lastScoreSeconds_);
    puzzleSelectionMode_ = false;
    singlePuzzleTestMode_ = false;
    audioCueBus_.playCue(protocol::AudioCueId::Success);
    state_ = protocol::GameState::Setup;
    setupEncoderBaseline_ = inputPanelService_.encoder(0).value;
    return;
  }

  inputPanelService_.renderHighscoreEntry(highscoreLetters_, lastScoreSeconds_);
}

void AppController::tickTimeout() {
  inputPanelService_.renderStatus(state_, 0, sidecarOnline_);

  std::uint8_t btn;
  if (inputPanelService_.takeLedKeyButtonPress(btn)) {
    state_ = protocol::GameState::Setup;
    puzzleSelectionMode_ = false;
    singlePuzzleTestMode_ = false;
    setupEncoderBaseline_ = inputPanelService_.encoder(0).value;
    updateOutputsForState();
  }
}

// ---------------------------------------------------------------------------
// Outputs / diagnostics / web API
// ---------------------------------------------------------------------------

void AppController::updateOutputsForState() {
  matrixService_.renderGameState(state_);
  inputPanelService_.setStatusLed(state_ == protocol::GameState::Countdown ||
                                   state_ == protocol::GameState::Active);

  if (state_ == protocol::GameState::Success) {
    servoService_.open();
  } else if (state_ != protocol::GameState::Maintenance) {
    servoService_.close();
  }
}

void AppController::syncDiagnostics() {
  // takeEventTag() consumes the RFID event; call it exactly once per loop.
  const String scannedTag = rfidService_.takeEventTag();
  if (scannedTag.length() > 0) {
    eventLog_.logf("rfid", "scanned %s", scannedTag.c_str());
  }
}

puzzles::Puzzle* AppController::currentPuzzleForStatus() const {
  if (state_ != protocol::GameState::Active) {
    return nullptr;
  }
  return currentPuzzle_;
}

void AppController::handleTouchPost(AsyncWebServerRequest *request) {
  if (!request->hasParam("plain", true)) {
    request->send(400, "application/json", "{\"error\":\"missing_body\"}");
    return;
  }

  String body = request->getParam("plain", true)->value();
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    request->send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  const int cell = doc["cell"] | -1;
  if (cell < 0 || cell > 255) {
    request->send(400, "application/json", "{\"error\":\"invalid_cell\"}");
    return;
  }

  touchCellIndex_ = static_cast<std::uint8_t>(cell);
  touchPending_ = true;
  request->send(200, "application/json", "{\"ok\":true}");
}

void AppController::handleLogsGet(AsyncWebServerRequest *request) {
  JsonDocument doc;
  JsonArray events = doc["events"].to<JsonArray>();
  eventLog_.forEachOldestFirst([&events](const diagnostics::EventLog::Entry& e) {
    JsonObject o = events.add<JsonObject>();
    o["seq"] = e.seq;
    o["timestampMs"] = e.timestampMs;
    o["tag"] = e.tag;
    o["message"] = e.message;
  });

  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

void AppController::handleGameStatus(AsyncWebServerRequest *request) {
  JsonDocument doc;
  doc["state"] = static_cast<int>(state_);
  doc["remainingSeconds"] = remainingSeconds_;
  doc["selectedLimitSeconds"] = selectedLimitSeconds_;
  std::uint32_t countdownRemaining = 0;
  if (state_ == protocol::GameState::Countdown) {
    const std::uint32_t elapsed = static_cast<std::uint32_t>(millis() - countdownStartMs_) / 1000;
    countdownRemaining = elapsed >= kCountdownSeconds ? 0 : kCountdownSeconds - elapsed;
  }
  doc["countdownSeconds"] = countdownRemaining;
  doc["puzzleIndex"] = currentPuzzleIndex_;
  doc["puzzleCount"] = kShuffledCount + 1;  // +1 for Finale

  puzzles::Puzzle* active = currentPuzzleForStatus();
  doc["puzzleId"] = active != nullptr ? protocol::puzzleName(active->id()) : "";

  JsonObject audio = doc["audio"].to<JsonObject>();
  audio["cueId"] = static_cast<int>(audioCueBus_.lastCue());
  audio["cueSeq"] = audioCueBus_.cueSeq();
  JsonArray voices = audio["voices"].to<JsonArray>();
  for (std::uint8_t v = 0; v < AudioCueBus::kVoiceCount; ++v) {
    voices.add(audioCueBus_.voiceHz(v));
  }

  if (active == &spectralTunerPuzzle_) {
    JsonObject tuner = doc["tuner"].to<JsonObject>();
    tuner["solved"] = spectralTunerPuzzle_.isSolved();
    JsonArray encoders = tuner["encoders"].to<JsonArray>();
    for (std::uint8_t i = 0; i < spectralTunerPuzzle_.encoderCount(); ++i) {
      JsonObject e = encoders.add<JsonObject>();
      e["proximity"] = spectralTunerPuzzle_.proximity(i);
      e["locked"] = spectralTunerPuzzle_.locked(i);
    }
  } else if (active == &livingIntervalPuzzle_) {
    JsonObject living = doc["living"].to<JsonObject>();
    JsonArray encoders = living["encoders"].to<JsonArray>();
    for (std::uint8_t i = 0; i < 3; ++i) {
      JsonObject e = encoders.add<JsonObject>();
      e["holdProgress"] = livingIntervalPuzzle_.holdProgress(i);
      e["holding"] = livingIntervalPuzzle_.holding(i);
    }
  } else if (active == &resonantGridPuzzle_) {
    JsonObject grid = doc["grid"].to<JsonObject>();
    grid["gridSize"] = resonantGridPuzzle_.gridSize();
    grid["totalCells"] = resonantGridPuzzle_.totalCells();
    grid["cursor"] = resonantGridPuzzle_.cursor();
    grid["lockedMask"] = resonantGridPuzzle_.lockedMask();
  } else if (active == &tetrisPuzzle_) {
    JsonObject tetris = doc["tetris"].to<JsonObject>();
    tetris["level"] = tetrisPuzzle_.level();
    tetris["targetLevel"] = tetrisPuzzle_.targetLevel();
    tetris["stability"] = tetrisPuzzle_.stability01();
    tetris["paused"] = tetrisPuzzle_.paused();
    JsonArray rows = tetris["board"].to<JsonArray>();
    for (std::uint8_t r = 0; r < TetrisPuzzle::kHeight; ++r) {
      JsonArray row = rows.add<JsonArray>();
      for (std::uint8_t c = 0; c < TetrisPuzzle::kWidth; ++c) {
        std::uint8_t v = tetrisPuzzle_.cellAt(r, c);
        if (v == 0 && tetrisPuzzle_.activeCellAt(r, c)) {
          v = 8;  // 8 = "active falling piece", distinct from locked colours 1-7
        }
        row.add(v);
      }
    }
  }

  JsonArray highscores = doc["highscores"].to<JsonArray>();
  for (std::uint8_t i = 0; i < highscoreService_.count(); ++i) {
    const auto& entry = highscoreService_.entries()[i];
    JsonObject e = highscores.add<JsonObject>();
    e["initials"] = entry.initials;
    e["score"] = entry.scoreSeconds;
  }

  if (state_ == protocol::GameState::HighscoreEntry) {
    JsonObject entry = doc["highscoreEntry"].to<JsonObject>();
    char letters[4] = {highscoreLetters_[0], highscoreLetters_[1], highscoreLetters_[2], '\0'};
    entry["letters"] = letters;
    entry["score"] = lastScoreSeconds_;
  }

  if (state_ == protocol::GameState::Success) {
    doc["score"] = lastScoreSeconds_;
  }

  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

// ---------------------------------------------------------------------------
// Hardware debug page — GET /debug, GET /api/debug, POST /api/debug/test.
// Independent of game state: lets a builder verify each component from
// spec/components.md individually, live, without playing through the game.
// ---------------------------------------------------------------------------

void AppController::handleDebugPageGet(AsyncWebServerRequest *request) {
  request->send(200, "text/html", kDebugPageHtml);
}

void AppController::handleDebugStatusGet(AsyncWebServerRequest *request) {
  JsonDocument doc;

  JsonObject rfid = doc["rfid"].to<JsonObject>();
  rfid["ready"] = rfidService_.isReady();
  rfid["lastTag"] = rfidService_.lastSeenTag();

  JsonObject rtc = doc["rtc"].to<JsonObject>();
  rtc["ready"] = rtcService_.isReady();
  rtc["now"] = rtcService_.formattedNow();

  JsonObject colorSensor = doc["colorSensor"].to<JsonObject>();
  colorSensor["hasSignal"] = colorSensorService_.hasSignal();
  colorSensor["label"] = colorSensorService_.detectedColorLabel();
  colorSensor["r"] = colorSensorService_.redFrequencyHz();
  colorSensor["g"] = colorSensorService_.greenFrequencyHz();
  colorSensor["b"] = colorSensorService_.blueFrequencyHz();

  JsonObject hidden = doc["hidden"].to<JsonObject>();
  hidden["sensor1"] = hiddenTriggerService_.sensor1Active();
  hidden["sensor2"] = hiddenTriggerService_.sensor2Active();

  JsonObject servo = doc["servo"].to<JsonObject>();
  servo["isOpen"] = servoService_.isOpen();

  JsonObject vibration = doc["vibration"].to<JsonObject>();
  vibration["active"] = vibrationService_.isActive();

  JsonArray encoders = doc["encoders"].to<JsonArray>();
  for (std::uint8_t i = 0; i < encoder_hw::kEncoderCount; ++i) {
    JsonObject e = encoders.add<JsonObject>();
    e["value"] = inputPanelService_.encoder(i).value;
    e["buttonPressed"] = inputPanelService_.encoder(i).buttonPressed;
  }

  JsonObject tm1638 = doc["tm1638"].to<JsonObject>();
  tm1638["buttonMask"] = inputPanelService_.ledKeyButtonMask();

  JsonObject audio = doc["audio"].to<JsonObject>();
  audio["cueSeq"] = audioCueBus_.cueSeq();
  audio["voice0Hz"] = audioCueBus_.voiceHz(0);

  String body;
  serializeJson(doc, body);
  request->send(200, "application/json", body);
}

void AppController::handleDebugTestPost(AsyncWebServerRequest *request) {
  if (!request->hasParam("plain", true)) {
    request->send(400, "application/json", "{\"error\":\"missing_body\"}");
    return;
  }

  String body = request->getParam("plain", true)->value();
  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    request->send(400, "application/json", "{\"error\":\"invalid_json\"}");
    return;
  }

  const char* component = doc["component"] | "";
  const char* action = doc["action"] | "";

  if (strcmp(component, "servo") == 0) {
    if (strcmp(action, "open") == 0) {
      servoService_.open();
    } else if (strcmp(action, "close") == 0) {
      servoService_.close();
    } else {
      request->send(400, "application/json", "{\"error\":\"unknown_action\"}");
      return;
    }
  } else if (strcmp(component, "vibration") == 0 && strcmp(action, "pulse") == 0) {
    vibrationService_.pulse(300);
  } else if (strcmp(component, "matrix") == 0 && strcmp(action, "test") == 0) {
    matrixService_.runTest();
  } else if (strcmp(component, "encoderLed") == 0 && strcmp(action, "set") == 0) {
    const int index = doc["index"] | -1;
    if (index < 0 || index >= static_cast<int>(encoder_hw::kEncoderCount)) {
      request->send(400, "application/json", "{\"error\":\"invalid_index\"}");
      return;
    }
    const std::uint8_t r = doc["r"] | 0;
    const std::uint8_t g = doc["g"] | 0;
    const std::uint8_t b = doc["b"] | 0;
    inputPanelService_.setEncoderLed(static_cast<std::uint8_t>(index), r, g, b);
  } else if (strcmp(component, "audio") == 0) {
    if (strcmp(action, "tone") == 0) {
      audioCueBus_.playTone(440.0f, 500);
    } else if (strcmp(action, "cue") == 0) {
      audioCueBus_.playCue(protocol::AudioCueId::TestMelody);
    } else {
      request->send(400, "application/json", "{\"error\":\"unknown_action\"}");
      return;
    }
  } else {
    request->send(400, "application/json", "{\"error\":\"unknown_component\"}");
    return;
  }

  eventLog_.logf("debug", "test %s.%s", component, action);
  request->send(200, "application/json", "{\"ok\":true}");
}
