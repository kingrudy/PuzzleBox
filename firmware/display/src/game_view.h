#pragma once

#include "display_board_profile.h"

#if defined(ESP32_8048S050C)

#include <Arduino_GFX_Library.h>
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
  bool fetchStatus();
  void render();
  void showScreen(lv_obj_t* screen);
  void handleDisconnected();

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
  void buildGenericPuzzleScreen();
  void buildHighscoreEntryScreen();

  // --- per-poll widget updates ---
  void updateSetupScreen();
  void updateCountdownScreen();
  void updateTunerScreen();
  void updateLivingScreen();
  void updateGridScreen();
  void updateTetrisScreen();
  void updateGenericPuzzleScreen();
  void updateSuccessScreen();
  void updateHighscoreEntryScreen();
  void updateHighscoreRows(std::array<lv_obj_t*, 5>& rows);

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

  std::uint8_t tetrisLevel_ = 1;
  std::uint8_t tetrisTargetLevel_ = 3;
  float tetrisStability_ = 1.0f;
  bool tetrisPaused_ = false;
  static constexpr std::uint8_t kTetrisWidth = 10;
  static constexpr std::uint8_t kTetrisHeight = 16;
  static constexpr std::uint8_t kTetrisCellPx = 22;
  std::array<std::array<std::uint8_t, kTetrisWidth>, kTetrisHeight> tetrisBoard_{};

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
  lv_obj_t* genericPuzzleScreen_ = nullptr;
  lv_obj_t* successScreen_ = nullptr;
  lv_obj_t* highscoreEntryScreen_ = nullptr;
  lv_obj_t* timeoutScreen_ = nullptr;

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
  lv_obj_t* tetrisStabilityBar_ = nullptr;
  lv_obj_t* tetrisLevelLabel_ = nullptr;
  lv_obj_t* tetrisPausedLabel_ = nullptr;

  lv_obj_t* genericTitleLabel_ = nullptr;

  lv_obj_t* successTimeLabel_ = nullptr;

  lv_obj_t* hsLettersLabel_ = nullptr;
  lv_obj_t* hsScoreLabel_ = nullptr;
  std::array<lv_obj_t*, kHighscoreShown> hsHighscoreRows_{};

  // --- redraw bookkeeping ---
  bool connected_ = false;
  std::uint32_t lastPollMs_ = 0;
};

#endif  // ESP32_8048S050C
