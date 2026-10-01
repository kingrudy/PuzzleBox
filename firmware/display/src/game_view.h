#pragma once

#include "display_board_profile.h"

#if defined(ESP32_8048S050C)

#include <ArduinoJson.h>
#include <Arduino_GFX_Library.h>
#include <HardwareSerial.h>
#include <lvgl.h>

#include <array>
#include <cstdint>

#include "display_speaker_service.h"
#include "protocol/game_state.h"
#include "touch_service.h"

// The main controller <-> display screen. Supersedes the earlier
// TunerVisualizer (which only rendered the SpectralTuner puzzle) — this
// class owns the single RGB panel instance and renders whichever of the 7
// puzzles (docs/puzzles/plan.md) is currently active, plus the Setup /
// Countdown / Success / HighscoreEntry / Timeout screens around them. Polls
// GET /api/game on the main controller (see AppController::handleGameStatus)
// and, on the Resonant Grid screen, posts touches back via POST /api/touch.
//
// Everything is rendered through LVGL (https://lvgl.io/), driven off an
// Arduino_GFX RGB-panel bus as the flush target — the panel only renders
// correctly this way (hardware-confirmed), not via direct Arduino_GFX draw
// calls. Each game state / active puzzle owns one persistent lv_obj_t
// "screen", built once in begin() and switched with lv_scr_load(); widgets
// on the current screen are pushed the latest state every poll rather than
// hand-tracking "did anything change" (LVGL only repaints what actually
// changed).
class GameView {
 public:
  // speaker outlives this object (owned by main.cpp) — GameView drives it
  // from the "audio" section of every /api/game poll (cues + up to 3
  // continuous proximity-tone voices), so sound stays in lockstep with
  // whatever the main controller's AudioCueBus is doing. touch outlives
  // this object too — GameView wires it into LVGL as a pointer input
  // device (see touchReadCb).
  void begin(DisplaySpeakerService& speaker, TouchService& touch);
  void poll();  // call every loop; drives lv_timer_handler() every call and
                // internally rate-limits its own HTTP polls

 private:
  void applyGameStatus(JsonDocument& doc);
  void render();
  void showScreen(lv_obj_t* screen);
  void handleDisconnected();
  // Drains whatever bytes are already sitting in the UART's hardware FIFO
  // (HardwareSerial::available()/read() never block, unlike the WS client
  // this replaced -- see the comment on mainUart_), accumulates them into
  // uartLineBuf_, and applies+renders each complete '\n'-terminated line.
  void pollMainUart();

  // --- LVGL glue ---
  static void flushCb(lv_display_t* disp, const lv_area_t* area, std::uint8_t* px_map);
  static void touchReadCb(lv_indev_t* indev, lv_indev_data_t* data);
  static void gridCellClickedCb(lv_event_t* e);

  // --- one-time UI construction (called from begin()) ---
  void buildUi();
  lv_obj_t* createScreen(const char* title, lv_obj_t** outTitleLabel = nullptr);
  lv_obj_t* addInstructionLabel(lv_obj_t* screen, const char* text, int yOffset);
  void buildProximityColumns(lv_obj_t* screen, std::array<lv_obj_t*, 3>& bars);
  void buildHighscoreRows(lv_obj_t* screen, std::array<lv_obj_t*, 5>& rows, int x, int y);
  void buildGridScreen();
  void buildTetrisScreen();
  void buildTiltMazeScreen();
  void rebuildMaze();
  void buildGenericPuzzleScreen();
  void buildHighscoreEntryScreen();
  void buildDebugScreen();

  // --- per-poll widget updates ---
  void updateSetupScreen();
  void updateCountdownScreen();
  void updateTunerScreen();
  void updateLivingScreen();
  void updateGridScreen();
  void updateTetrisScreen();
  void updateTiltMazeScreen();
  void updateGenericPuzzleScreen();
  void updateSuccessScreen();
  void updateHighscoreEntryScreen();
  void updateHighscoreRows(std::array<lv_obj_t*, 5>& rows);
  void updateDebugScreen();
  void updatePersistentTimer();

  void gridCellRect(std::uint8_t cell, int& x, int& y, int& w, int& h) const;
  void postGridTouch(std::uint8_t cell);

  Arduino_RGB_Display* gfx_ = nullptr;
  DisplaySpeakerService* speaker_ = nullptr;
  TouchService* touch_ = nullptr;
  std::uint32_t lastAppliedCueSeq_ = 0xFFFFFFFFu;

  lv_display_t* lvDisplay_ = nullptr;
  lv_indev_t* lvIndev_ = nullptr;
  void* lvDrawBuf_ = nullptr;
  lv_obj_t* currentScreen_ = nullptr;

  // --- parsed /api/game state ---
  protocol::GameState state_ = protocol::GameState::Boot;
  bool debugTestPatternActive_ = false;
  String puzzleId_;
  std::uint32_t remainingSeconds_ = 0;
  std::uint32_t selectedLimitSeconds_ = 0;
  std::uint32_t countdownSeconds_ = 0;
  std::uint32_t score_ = 0;

  struct EncoderField {
    float proximity = 0.0f;
    bool locked = false;
  };
  std::array<EncoderField, 3> tunerFields_{};

  struct LivingField {
    float holdProgress = 0.0f;
    bool holding = false;
  };
  std::array<LivingField, 3> livingFields_{};

  static constexpr std::uint8_t kMaxGridCells = 9;
  std::uint8_t gridSize_ = 2;
  std::uint8_t gridTotalCells_ = 4;
  std::uint8_t gridCursor_ = 0;
  std::uint16_t gridLockedMask_ = 0;

  std::uint8_t tetrisLinesCleared_ = 0;
  std::uint8_t tetrisTargetLines_ = 10;
  bool tetrisPaused_ = false;
  static constexpr std::uint8_t kTetrisWidth = 10;
  static constexpr std::uint8_t kTetrisHeight = 16;
  static constexpr std::uint8_t kTetrisCellPx = 22;
  std::array<std::array<std::uint8_t, kTetrisWidth>, kTetrisHeight> tetrisBoard_{};

  // Tilt maze (see TiltMazePuzzle on the main controller). Walls/holes/exit
  // are rebuilt only when mazeId_ changes; the ball just moves.
  static constexpr std::uint8_t kMazeMaxCells = 60;
  static constexpr std::uint8_t kMazeMaxHoles = 3;
  std::uint32_t mazeId_ = 0;
  std::uint32_t mazeBuiltId_ = 0;
  std::uint8_t mazeCols_ = 0;
  std::uint8_t mazeRows_ = 0;
  char mazeWalls_[kMazeMaxCells + 1] = {0};
  std::array<std::uint8_t, kMazeMaxHoles> mazeHoles_{};
  std::uint8_t mazeHoleCount_ = 0;
  std::uint8_t mazeExit_ = 0;
  std::int32_t mazeBallX_ = 0;  // cell units x100
  std::int32_t mazeBallY_ = 0;
  std::uint8_t mazeFalls_ = 0;
  bool mazeSensorOnline_ = true;

  static constexpr std::uint8_t kMaxHighscores = 10;
  static constexpr std::uint8_t kHighscoreShown = 5;
  struct HighscoreRow {
    char initials[4] = "---";
    std::uint32_t score = 0;
  };
  std::array<HighscoreRow, kMaxHighscores> highscores_{};
  std::uint8_t highscoreCount_ = 0;
  char hsLetters_[4] = "AAA";
  std::uint32_t hsScore_ = 0;

  // --- screens (built once in buildUi(), switched via showScreen()) ---
  lv_obj_t* defaultScreen_ = nullptr;
  lv_obj_t* disconnectedScreen_ = nullptr;
  lv_obj_t* setupScreen_ = nullptr;
  lv_obj_t* countdownScreen_ = nullptr;
  lv_obj_t* tunerScreen_ = nullptr;
  lv_obj_t* livingScreen_ = nullptr;
  lv_obj_t* gridScreen_ = nullptr;
  lv_obj_t* tetrisScreen_ = nullptr;
  lv_obj_t* tiltMazeScreen_ = nullptr;
  lv_obj_t* genericPuzzleScreen_ = nullptr;
  lv_obj_t* successScreen_ = nullptr;
  lv_obj_t* highscoreEntryScreen_ = nullptr;
  lv_obj_t* timeoutScreen_ = nullptr;
  lv_obj_t* debugScreen_ = nullptr;

  // --- dynamic widgets, updated every poll ---
  lv_obj_t* setupTimerLabel_ = nullptr;
  std::array<lv_obj_t*, kHighscoreShown> setupHighscoreRows_{};

  lv_obj_t* countdownLabel_ = nullptr;

  std::array<lv_obj_t*, 3> tunerBars_{};
  std::array<lv_obj_t*, 3> livingBars_{};

  std::array<lv_obj_t*, kMaxGridCells> gridCells_{};
  lv_obj_t* gridCountLabel_ = nullptr;

  lv_obj_t* tetrisCanvas_ = nullptr;
  lv_color_t* tetrisCanvasBuf_ = nullptr;
  lv_obj_t* tetrisLinesLabel_ = nullptr;
  lv_obj_t* tetrisPausedLabel_ = nullptr;

  lv_obj_t* mazeArea_ = nullptr;     // walls/holes/exit live here, cleared on rebuild
  lv_obj_t* mazeBall_ = nullptr;
  lv_obj_t* mazeStatusLabel_ = nullptr;
  int mazeCellPx_ = 0;

  lv_obj_t* genericTitleLabel_ = nullptr;

  lv_obj_t* successTimeLabel_ = nullptr;

  lv_obj_t* hsLettersLabel_ = nullptr;
  lv_obj_t* hsScoreLabel_ = nullptr;
  std::array<lv_obj_t*, kHighscoreShown> hsHighscoreRows_{};

  lv_obj_t* debugTouchLabel_ = nullptr;
  lv_obj_t* debugTouchRawLabel_ = nullptr;
  lv_obj_t* debugTouchDot_ = nullptr;

  // Persistent room-clock overlay -- lives on lv_layer_top(), not any one
  // screen, so it stays visible across every puzzle screen.
  lv_obj_t* persistentTimerPanel_ = nullptr;
  lv_obj_t* persistentTimerLabel_ = nullptr;

  // --- redraw bookkeeping ---
  bool connected_ = false;
  // Highest AppController::statusSeq_ applied so far; applyGameStatus()
  // drops anything with seq < this (out-of-order protection, see the
  // comment on statusSeq_ in app_controller.h).
  std::uint32_t lastAppliedStatusSeq_ = 0;

  // The display's only link to the main controller (it has no WiFi at all):
  // status lines in, touch lines out (see AppController::displayUart_ and
  // pollDisplayUart). Two earlier transports were dropped: an HTTP poll
  // (blocking, stalled LVGL/touch/audio for 160-250ms per request) and a
  // WebSocketsClient (blocking 5s TCP connect). HardwareSerial::available()/
  // read() only look at an already-filled hardware FIFO and never block, so
  // pollMainUart() runs straight on the main/LVGL loop. GPIO12 (RX) /
  // GPIO13 (TX) were the TF/SD-card SPI SCLK/MISO -- see
  // display_board_profile.h -- never used by this firmware; the far end is
  // the main controller's GPIO33/32.
  HardwareSerial mainUart_{1};
  static constexpr std::uint8_t kMainUartRxPin = 12;
  static constexpr std::uint8_t kMainUartTxPin = 13;
  String uartLineBuf_;
  std::uint32_t uartBytes_ = 0;
  std::uint32_t uartLinesOk_ = 0;
  std::uint32_t uartLinesBad_ = 0;
  std::uint32_t lastUartDiagMs_ = 0;
  std::uint32_t worstPollUs_ = 0;
  std::uint32_t lastUartOkMs_ = 0;
  // Main controller heartbeats every 250ms even when nothing changed.
  static constexpr std::uint32_t kUartFreshMs = 1000;
};

#endif  // ESP32_8048S050C
