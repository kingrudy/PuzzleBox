#include "game_view.h"

#if defined(ESP32_8048S050C)

#include <ArduinoJson.h>
#include <esp_heap_caps.h>

namespace {

// --- RGB panel bring-up tuning constants (see game_view.h / former
// tuner_visualizer.cpp for the full rationale — unchanged from that file).
// This is low-level panel/bus bring-up, not "the screen" — LVGL sits on
// top of it as the flush target (GameView::flushCb) and owns everything
// actually drawn. ---
constexpr std::uint16_t kHsyncPolarity = 0;
constexpr std::uint16_t kHsyncFrontPorch = 8;
constexpr std::uint16_t kHsyncPulseWidth = 4;
constexpr std::uint16_t kHsyncBackPorch = 8;
constexpr std::uint16_t kVsyncPolarity = 0;
constexpr std::uint16_t kVsyncFrontPorch = 8;
constexpr std::uint16_t kVsyncPulseWidth = 4;
constexpr std::uint16_t kVsyncBackPorch = 8;
constexpr std::uint16_t kPclkActiveNeg = 0;

constexpr std::size_t kBounceBufferSizePx = display_board::kWidth * 10;

// LVGL partial-render draw buffer, sized the same way as the GFX-library's
// own bundled LVGL example for this display class (examples/LVGL/
// LVGL_Arduino_v9): a handful of scanlines' worth, not the whole frame.
constexpr std::uint32_t kLvDrawBufRows = 40;

constexpr int kBackgroundR = 8, kBackgroundG = 8, kBackgroundB = 20;

std::uint16_t rgb565(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
  return static_cast<std::uint16_t>(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

lv_color_t bgColor() { return lv_color_make(kBackgroundR, kBackgroundG, kBackgroundB); }

lv_color_t lerpColor(std::uint8_t r0, std::uint8_t g0, std::uint8_t b0, std::uint8_t r1,
                      std::uint8_t g1, std::uint8_t b1, float t) {
  auto lerp = [&](std::uint8_t a, std::uint8_t b) -> std::uint8_t {
    return static_cast<std::uint8_t>(a + (static_cast<float>(b) - a) * t);
  };
  return lv_color_make(lerp(r0, r1), lerp(g0, g1), lerp(b0, b1));
}

// Same red -> amber -> green ramp used by SpectralTunerPuzzle::colorForProximity
// on the main controller, so the big screen amplifies the physical encoder
// LEDs instead of contradicting them.
lv_color_t colorForProximity(float proximity01) {
  proximity01 = proximity01 < 0.0f ? 0.0f : (proximity01 > 1.0f ? 1.0f : proximity01);
  if (proximity01 < 0.6f) {
    return lerpColor(255, 0, 0, 255, 170, 0, proximity01 / 0.6f);
  }
  return lerpColor(255, 170, 0, 0, 255, 0, (proximity01 - 0.6f) / 0.4f);
}

lv_color_t colorForPieceType(std::uint8_t type) {
  switch (type) {
    case 1: return lv_color_make(0, 220, 220);    // I
    case 2: return lv_color_make(220, 220, 0);    // O
    case 3: return lv_color_make(180, 0, 220);    // T
    case 4: return lv_color_make(0, 220, 0);      // S
    case 5: return lv_color_make(220, 0, 0);      // Z
    case 6: return lv_color_make(0, 0, 220);      // J
    case 7: return lv_color_make(220, 120, 0);    // L
    case 8: return lv_color_make(140, 140, 160);  // active falling piece (unknown type at this point)
    default: return lv_color_make(4, 4, 10);
  }
}

constexpr int kColumnWidth = 220;
constexpr int kColumnGap = 30;
constexpr int kColumnStartX = (800 - (3 * kColumnWidth + 2 * kColumnGap)) / 2;

std::uint32_t millisCb() { return millis(); }

}  // namespace

void GameView::begin(DisplaySpeakerService& speaker, TouchService& touch) {
  speaker_ = &speaker;
  touch_ = &touch;

  auto* bus = new Arduino_ESP32RGBPanel(
      display_board::kRgbDe, display_board::kRgbVsync, display_board::kRgbHsync,
      display_board::kRgbPclk,
      display_board::kRgbR[0], display_board::kRgbR[1], display_board::kRgbR[2],
      display_board::kRgbR[3], display_board::kRgbR[4],
      display_board::kRgbG[0], display_board::kRgbG[1], display_board::kRgbG[2],
      display_board::kRgbG[3], display_board::kRgbG[4], display_board::kRgbG[5],
      display_board::kRgbB[0], display_board::kRgbB[1], display_board::kRgbB[2],
      display_board::kRgbB[3], display_board::kRgbB[4],
      kHsyncPolarity, kHsyncFrontPorch, kHsyncPulseWidth, kHsyncBackPorch, kVsyncPolarity,
      kVsyncFrontPorch, kVsyncPulseWidth, kVsyncBackPorch, kPclkActiveNeg,
      static_cast<std::int32_t>(display_board::kRgbPclkHz), /*useBigEndian=*/false,
      /*de_idle_high=*/0, /*pclk_idle_high=*/0, kBounceBufferSizePx);

  gfx_ = new Arduino_RGB_Display(display_board::kWidth, display_board::kHeight, bus,
                                  /*rotation=*/0, /*auto_flush=*/true);
  gfx_->begin();
  // Blank the panel before LVGL's first flush lands, so there's no garbage
  // frame in the gap between panel bring-up and lv_timer_handler()'s first
  // call.
  gfx_->fillScreen(rgb565(kBackgroundR, kBackgroundG, kBackgroundB));

  lv_init();
  lv_tick_set_cb(&millisCb);

  const std::size_t drawBufPixels = static_cast<std::size_t>(display_board::kWidth) * kLvDrawBufRows;
  const std::size_t drawBufBytes = drawBufPixels * sizeof(lv_color_t);
  lvDrawBuf_ = heap_caps_malloc(drawBufBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (lvDrawBuf_ == nullptr) {
    lvDrawBuf_ = heap_caps_malloc(drawBufBytes, MALLOC_CAP_8BIT);
  }

  lvDisplay_ = lv_display_create(display_board::kWidth, display_board::kHeight);
  lv_display_set_user_data(lvDisplay_, this);
  lv_display_set_flush_cb(lvDisplay_, &GameView::flushCb);
  lv_display_set_buffers(lvDisplay_, lvDrawBuf_, nullptr, static_cast<std::uint32_t>(drawBufBytes),
                          LV_DISPLAY_RENDER_MODE_PARTIAL);

  lvIndev_ = lv_indev_create();
  lv_indev_set_type(lvIndev_, LV_INDEV_TYPE_POINTER);
  lv_indev_set_user_data(lvIndev_, this);
  lv_indev_set_read_cb(lvIndev_, &GameView::touchReadCb);

  buildUi();
  currentScreen_ = defaultScreen_;
  lv_scr_load(defaultScreen_);

  // Supplementary push channel -- see the comment on mainUart_ in
  // game_view.h.
  Serial0.begin(115200);
  // Default RX ring buffer is 256 bytes, smaller than one status line --
  // hardware-confirmed: most lines arrived torn (bytes dropped while the
  // main loop was busy rendering). Must be set before begin().
  mainUart_.setRxBufferSize(8192);
  mainUart_.begin(921600, SERIAL_8N1, kMainUartRxPin, kMainUartTxPin);
  uartLineBuf_.reserve(768);  // comfortably larger than one game-status line
}

// ---------------------------------------------------------------------------
// LVGL glue — flush/touch callbacks can't be member functions (LVGL calls
// them as bare C function pointers), so these are static trampolines that
// recover the owning GameView via LVGL's per-object user_data slot.
// ---------------------------------------------------------------------------

void GameView::flushCb(lv_display_t* disp, const lv_area_t* area, std::uint8_t* px_map) {
  auto* self = static_cast<GameView*>(lv_display_get_user_data(disp));
  const std::uint32_t w = lv_area_get_width(area);
  const std::uint32_t h = lv_area_get_height(area);
  self->gfx_->draw16bitRGBBitmap(area->x1, area->y1, reinterpret_cast<std::uint16_t*>(px_map), w, h);
  lv_display_flush_ready(disp);
}

void GameView::touchReadCb(lv_indev_t* indev, lv_indev_data_t* data) {
  auto* self = static_cast<GameView*>(lv_indev_get_user_data(indev));
  std::int16_t x = 0;
  std::int16_t y = 0;
  if (self->touch_ != nullptr && self->touch_->readState(x, y)) {
    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = x;
    data->point.y = y;
  } else {
    data->state = LV_INDEV_STATE_RELEASED;
  }
}

void GameView::gridCellClickedCb(lv_event_t* e) {
  auto* self = static_cast<GameView*>(lv_event_get_user_data(e));
  auto* btn = static_cast<lv_obj_t*>(lv_event_get_target(e));
  const auto cell = static_cast<std::uint8_t>(reinterpret_cast<std::uintptr_t>(lv_obj_get_user_data(btn)));
  self->postGridTouch(cell);
}

// ---------------------------------------------------------------------------
// Status intake (UART only -- see pollMainUart)
// ---------------------------------------------------------------------------

void GameView::applyGameStatus(JsonDocument& doc) {
  const std::uint32_t seq = doc["seq"] | 0;
  if (seq != 0 && lastAppliedStatusSeq_ != 0 && seq < lastAppliedStatusSeq_) {
    return;  // arrived after a newer snapshot was already applied -- drop it (see lastAppliedStatusSeq_)
  }
  if (seq != 0) {
    lastAppliedStatusSeq_ = seq;
  }

  state_ = static_cast<protocol::GameState>(doc["state"] | 0);
  debugTestPatternActive_ = doc["debugTestPattern"] | false;
  puzzleId_ = doc["puzzleId"] | "";
  remainingSeconds_ = doc["remainingSeconds"] | 0;
  selectedLimitSeconds_ = doc["selectedLimitSeconds"] | 0;
  countdownSeconds_ = doc["countdownSeconds"] | 0;
  score_ = doc["score"] | 0;

  JsonArray tuner = doc["tuner"]["encoders"];
  std::uint8_t i = 0;
  for (JsonObject e : tuner) {
    if (i >= 3) break;
    tunerFields_[i].proximity = e["proximity"] | 0.0f;
    tunerFields_[i].locked = e["locked"] | false;
    ++i;
  }

  JsonArray living = doc["living"]["encoders"];
  i = 0;
  for (JsonObject e : living) {
    if (i >= 3) break;
    livingFields_[i].holdProgress = e["holdProgress"] | 0.0f;
    livingFields_[i].holding = e["holding"] | false;
    ++i;
  }

  gridSize_ = doc["grid"]["gridSize"] | 2;
  gridTotalCells_ = doc["grid"]["totalCells"] | 4;
  gridCursor_ = doc["grid"]["cursor"] | 0;
  gridLockedMask_ = doc["grid"]["lockedMask"] | 0;

  tetrisLinesCleared_ = doc["tetris"]["linesCleared"] | 0;
  tetrisTargetLines_ = doc["tetris"]["targetLines"] | 10;
  tetrisPaused_ = doc["tetris"]["paused"] | false;
  JsonArray boardRows = doc["tetris"]["board"];
  std::uint8_t r = 0;
  for (JsonArray row : boardRows) {
    if (r >= kTetrisHeight) break;
    std::uint8_t c = 0;
    for (JsonVariant cell : row) {
      if (c >= kTetrisWidth) break;
      tetrisBoard_[r][c] = cell.as<std::uint8_t>();
      ++c;
    }
    ++r;
  }

  JsonObject maze = doc["maze"];
  if (!maze.isNull()) {
    mazeId_ = maze["id"] | 0;
    mazeCols_ = std::min<std::uint8_t>(maze["cols"] | 0, 10);
    mazeRows_ = std::min<std::uint8_t>(maze["rows"] | 0, 6);
    strncpy(mazeWalls_, maze["walls"] | "", kMazeMaxCells);
    mazeWalls_[kMazeMaxCells] = '\0';
    mazeHoleCount_ = 0;
    for (JsonVariant h : maze["holes"].as<JsonArray>()) {
      if (mazeHoleCount_ >= kMazeMaxHoles) break;
      mazeHoles_[mazeHoleCount_++] = h.as<std::uint8_t>();
    }
    mazeExit_ = maze["exit"] | 0;
    mazeBallX_ = maze["bx"] | 0;
    mazeBallY_ = maze["by"] | 0;
    mazeFalls_ = maze["falls"] | 0;
    mazeSensorOnline_ = maze["sensor"] | true;
  }

  JsonArray hs = doc["highscores"];
  highscoreCount_ = 0;
  for (JsonObject e : hs) {
    if (highscoreCount_ >= kMaxHighscores) break;
    const char* initials = e["initials"] | "---";
    strncpy(highscores_[highscoreCount_].initials, initials, 3);
    highscores_[highscoreCount_].initials[3] = '\0';
    highscores_[highscoreCount_].score = e["score"] | 0;
    ++highscoreCount_;
  }

  const char* letters = doc["highscoreEntry"]["letters"] | "AAA";
  strncpy(hsLetters_, letters, 3);
  hsLetters_[3] = '\0';
  hsScore_ = doc["highscoreEntry"]["score"] | 0;

  if (speaker_ != nullptr) {
    const std::uint32_t cueSeq = doc["audio"]["cueSeq"] | 0;
    if (cueSeq != lastAppliedCueSeq_) {
      lastAppliedCueSeq_ = cueSeq;
      const auto cueId = static_cast<protocol::AudioCueId>(doc["audio"]["cueId"] | 0);
      speaker_->playCue(cueId);
    }
    JsonArray voices = doc["audio"]["voices"];
    std::uint8_t v = 0;
    for (JsonVariant hz : voices) {
      if (v >= DisplaySpeakerService::kVoiceCount) break;
      speaker_->setVoiceHz(v, hz.as<float>());
      ++v;
    }
  }
}

void GameView::poll() {
  struct PollTimer {
    std::uint32_t& worst;
    std::uint32_t startUs = micros();
    ~PollTimer() {
      const std::uint32_t d = micros() - startUs;
      if (d > worst) worst = d;
    }
  } pollTimer{worstPollUs_};
  lv_timer_handler();  // LVGL needs frequent ticks for input/animation
  pollMainUart();       // never blocks -- just drains whatever's already in the UART FIFO, if anything

  // The touch-dot readout needs to track a fast, brief tap -- refresh it
  // every call (i.e. every loop() iteration), or a quick tap can start and
  // end between two updates and never show.
  if (debugTestPatternActive_ && currentScreen_ == debugScreen_) {
    updateDebugScreen();
  }

  // The main controller sends at least a heartbeat every 250ms, so a quiet
  // wire for kUartFreshMs means the link is down. The display has no WiFi at
  // all any more (the blocking HTTP poll it replaced stalled LVGL, touch and
  // audio for 160-250ms per request).
  if (connected_ && millis() - lastUartOkMs_ >= kUartFreshMs) {
    connected_ = false;
    handleDisconnected();
  }
}

void GameView::handleDisconnected() {
  showScreen(disconnectedScreen_);
  if (speaker_ != nullptr) {
    for (std::uint8_t v = 0; v < DisplaySpeakerService::kVoiceCount; ++v) {
      speaker_->setVoiceHz(v, 0.0f);
    }
  }
}

void GameView::showScreen(lv_obj_t* screen) {
  if (currentScreen_ != screen) {
    currentScreen_ = screen;
    lv_scr_load(screen);
  }
}

void GameView::pollMainUart() {
  // available()/read() only ever look at bytes the UART hardware already
  // has buffered -- neither can block, so this is safe to call straight
  // from the main/LVGL loop (see the comment on mainUart_ in game_view.h).
  static constexpr std::size_t kMaxUartLineLength = 2048;
  const std::uint32_t nowMs = millis();
  if (nowMs - lastUartDiagMs_ >= 5000) {
    lastUartDiagMs_ = nowMs;
    Serial0.printf("[uart] bytes=%u linesOk=%u linesBad=%u worstPollUs=%u\n", uartBytes_, uartLinesOk_,
                   uartLinesBad_, worstPollUs_);
    worstPollUs_ = 0;
  }

  bool appliedAny = false;
  while (mainUart_.available() > 0) {
    const char c = static_cast<char>(mainUart_.read());
    ++uartBytes_;
    if (c == '\r') {
      continue;  // println() on the main controller sends "\r\n"
    }
    if (c != '\n') {
      uartLineBuf_ += c;
      if (uartLineBuf_.length() > kMaxUartLineLength) {
        uartLineBuf_ = "";  // corrupted/overlong line -- resync on the next '\n'
      }
      continue;
    }

    if (uartLineBuf_.length() > 0) {
      JsonDocument doc;
      if (deserializeJson(doc, uartLineBuf_) == DeserializationError::Ok) {
        ++uartLinesOk_;
        applyGameStatus(doc);
        connected_ = true;
        appliedAny = true;
        lastUartOkMs_ = millis();
      } else {
        ++uartLinesBad_;
      }
      uartLineBuf_ = "";
    }
  }

  // Render once per drain, not per line -- if several lines backed up while
  // the loop was busy, only the newest matters (applyGameStatus() already
  // keeps the highest seq), and each render() is a full widget refresh.
  if (appliedAny) {
    render();  // don't wait for the next HTTP-poll tick -- that's the whole point of this channel
  }
}

void GameView::render() {
  if (debugTestPatternActive_) {
    showScreen(debugScreen_);
    updateDebugScreen();
    return;
  }

  switch (state_) {
    case protocol::GameState::Setup:
      showScreen(setupScreen_);
      updateSetupScreen();
      break;
    case protocol::GameState::Countdown:
      showScreen(countdownScreen_);
      updateCountdownScreen();
      break;
    case protocol::GameState::Active:
      if (puzzleId_ == "SpectralTuner") {
        showScreen(tunerScreen_);
        updateTunerScreen();
      } else if (puzzleId_ == "LivingInterval") {
        showScreen(livingScreen_);
        updateLivingScreen();
      } else if (puzzleId_ == "ResonantGrid") {
        showScreen(gridScreen_);
        updateGridScreen();
      } else if (puzzleId_ == "Tetris") {
        showScreen(tetrisScreen_);
        updateTetrisScreen();
      } else if (puzzleId_ == "TiltMaze") {
        showScreen(tiltMazeScreen_);
        updateTiltMazeScreen();
      } else {
        showScreen(genericPuzzleScreen_);
        updateGenericPuzzleScreen();
      }
      break;
    case protocol::GameState::Success:
      showScreen(successScreen_);
      updateSuccessScreen();
      break;
    case protocol::GameState::HighscoreEntry:
      showScreen(highscoreEntryScreen_);
      updateHighscoreEntryScreen();
      break;
    case protocol::GameState::Timeout:
      showScreen(timeoutScreen_);
      break;
    default:
      showScreen(defaultScreen_);
      break;
  }

  updatePersistentTimer();
}

// ---------------------------------------------------------------------------
// UI construction (once, from begin())
// ---------------------------------------------------------------------------

lv_obj_t* GameView::createScreen(const char* title, lv_obj_t** outTitleLabel) {
  lv_obj_t* screen = lv_obj_create(nullptr);
  lv_obj_remove_style_all(screen);
  lv_obj_set_size(screen, display_board::kWidth, display_board::kHeight);
  lv_obj_set_style_bg_color(screen, bgColor(), 0);
  lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
  lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t* titleLabel = lv_label_create(screen);
  lv_obj_set_style_text_color(titleLabel, lv_color_make(200, 200, 220), 0);
  lv_obj_set_style_text_font(titleLabel, &lv_font_montserrat_28, 0);
  lv_label_set_text(titleLabel, title);
  lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, 12);

  if (outTitleLabel != nullptr) {
    *outTitleLabel = titleLabel;
  }
  return screen;
}

lv_obj_t* GameView::addInstructionLabel(lv_obj_t* screen, const char* text, int yOffset) {
  lv_obj_t* label = lv_label_create(screen);
  lv_obj_set_style_text_color(label, lv_color_make(160, 160, 180), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
  lv_label_set_text(label, text);
  lv_obj_align(label, LV_ALIGN_TOP_MID, 0, yOffset);
  return label;
}

void GameView::buildProximityColumns(lv_obj_t* screen, std::array<lv_obj_t*, 3>& bars) {
  constexpr int kTop = 100;
  constexpr int kBottom = 420;

  for (std::uint8_t i = 0; i < 3; ++i) {
    const int x = kColumnStartX + i * (kColumnWidth + kColumnGap);

    lv_obj_t* bar = lv_bar_create(screen);
    lv_obj_set_pos(bar, x, kTop);
    lv_obj_set_size(bar, kColumnWidth, kBottom - kTop);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 0, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, bgColor(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(bar, 2, LV_PART_MAIN);
    lv_obj_set_style_border_color(bar, lv_color_make(60, 60, 70), LV_PART_MAIN);
    bars[i] = bar;

    lv_obj_t* title = lv_label_create(screen);
    lv_obj_set_style_text_color(title, lv_color_make(220, 220, 230), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_label_set_text_fmt(title, "VELD %u", i + 1);
    lv_obj_align_to(title, bar, LV_ALIGN_OUT_TOP_MID, 0, -6);
  }
}

void GameView::buildHighscoreRows(lv_obj_t* screen, std::array<lv_obj_t*, kHighscoreShown>& rows, int x, int y) {
  lv_obj_t* header = lv_label_create(screen);
  lv_obj_set_style_text_color(header, lv_color_make(140, 140, 170), 0);
  lv_obj_set_style_text_font(header, &lv_font_montserrat_20, 0);
  lv_label_set_text(header, "TOPSCORES");
  lv_obj_set_pos(header, x, y);

  for (std::uint8_t i = 0; i < kHighscoreShown; ++i) {
    lv_obj_t* row = lv_label_create(screen);
    lv_obj_set_style_text_font(row, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(row, x, y + 26 + i * 24);
    rows[i] = row;
  }
}

void GameView::buildGridScreen() {
  gridScreen_ = createScreen("RESONANTIERASTER");
  addInstructionLabel(gridScreen_, "RAAK HET SCHERM AAN", 50);

  for (std::uint8_t cell = 0; cell < kMaxGridCells; ++cell) {
    lv_obj_t* btn = lv_button_create(gridScreen_);
    lv_obj_set_style_radius(btn, 4, 0);
    lv_obj_set_style_border_width(btn, 2, 0);
    lv_obj_set_style_border_color(btn, lv_color_make(90, 90, 110), 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_user_data(btn, reinterpret_cast<void*>(static_cast<std::uintptr_t>(cell)));
    lv_obj_add_event_cb(btn, &GameView::gridCellClickedCb, LV_EVENT_CLICKED, this);
    gridCells_[cell] = btn;
  }

  gridCountLabel_ = lv_label_create(gridScreen_);
  lv_obj_set_style_text_color(gridCountLabel_, lv_color_make(160, 160, 180), 0);
  lv_obj_set_style_text_font(gridCountLabel_, &lv_font_montserrat_20, 0);
  lv_obj_align(gridCountLabel_, LV_ALIGN_BOTTOM_MID, 0, -30);
}

void GameView::buildTetrisScreen() {
  tetrisScreen_ = createScreen("REACTOROVERBELASTING");

  constexpr int kBoardX = 60;
  constexpr int kBoardY = 70;
  const int boardW = kTetrisWidth * kTetrisCellPx;
  const int boardH = kTetrisHeight * kTetrisCellPx;

  lv_obj_t* boardFrame = lv_obj_create(tetrisScreen_);
  lv_obj_remove_style_all(boardFrame);
  lv_obj_set_pos(boardFrame, kBoardX - 2, kBoardY - 2);
  lv_obj_set_size(boardFrame, boardW + 4, boardH + 4);
  lv_obj_set_style_border_width(boardFrame, 2, 0);
  lv_obj_set_style_border_color(boardFrame, lv_color_make(90, 90, 110), 0);
  lv_obj_clear_flag(boardFrame, LV_OBJ_FLAG_SCROLLABLE);

  tetrisCanvas_ = lv_canvas_create(tetrisScreen_);
  const std::size_t bufBytes = static_cast<std::size_t>(boardW) * boardH * sizeof(lv_color_t);
  tetrisCanvasBuf_ = static_cast<lv_color_t*>(heap_caps_malloc(bufBytes, MALLOC_CAP_SPIRAM));
  lv_canvas_set_buffer(tetrisCanvas_, tetrisCanvasBuf_, boardW, boardH, LV_COLOR_FORMAT_RGB565);
  lv_obj_set_pos(tetrisCanvas_, kBoardX, kBoardY);

  const int kMeterX = kBoardX + boardW + 60;
  const int kMeterY = kBoardY;

  tetrisLinesLabel_ = lv_label_create(tetrisScreen_);
  lv_obj_set_style_text_color(tetrisLinesLabel_, lv_color_make(220, 220, 230), 0);
  lv_obj_set_style_text_font(tetrisLinesLabel_, &lv_font_montserrat_28, 0);
  lv_obj_set_pos(tetrisLinesLabel_, kMeterX, kMeterY);

  tetrisPausedLabel_ = lv_label_create(tetrisScreen_);
  lv_obj_set_style_text_color(tetrisPausedLabel_, lv_color_make(255, 200, 0), 0);
  lv_obj_set_style_text_font(tetrisPausedLabel_, &lv_font_montserrat_28, 0);
  lv_label_set_text(tetrisPausedLabel_, "GEPAUZEERD");
  lv_obj_set_pos(tetrisPausedLabel_, kMeterX, kMeterY + 160);
  lv_obj_add_flag(tetrisPausedLabel_, LV_OBJ_FLAG_HIDDEN);
}

void GameView::buildTiltMazeScreen() {
  tiltMazeScreen_ = createScreen("ZWAARTEKRACHTLABYRINT");

  mazeStatusLabel_ = lv_label_create(tiltMazeScreen_);
  lv_obj_set_style_text_font(mazeStatusLabel_, &lv_font_montserrat_20, 0);
  lv_obj_align(mazeStatusLabel_, LV_ALIGN_TOP_MID, 0, 50);

  mazeArea_ = lv_obj_create(tiltMazeScreen_);
  lv_obj_remove_style_all(mazeArea_);
  lv_obj_clear_flag(mazeArea_, LV_OBJ_FLAG_SCROLLABLE);

  // The ball lives on the screen (not mazeArea_) so lv_obj_clean() on a
  // rebuild doesn't delete it.
  mazeBall_ = lv_obj_create(tiltMazeScreen_);
  lv_obj_remove_style_all(mazeBall_);
  lv_obj_set_style_radius(mazeBall_, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(mazeBall_, lv_color_make(255, 210, 60), 0);
  lv_obj_set_style_bg_opa(mazeBall_, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(mazeBall_, 2, 0);
  lv_obj_set_style_border_color(mazeBall_, lv_color_make(255, 255, 220), 0);
  lv_obj_clear_flag(mazeBall_, LV_OBJ_FLAG_SCROLLABLE);
}

void GameView::rebuildMaze() {
  mazeBuiltId_ = mazeId_;
  lv_obj_clean(mazeArea_);
  if (mazeCols_ == 0 || mazeRows_ == 0) {
    return;
  }

  // Fit the maze into the area below the title/status line.
  constexpr int kAreaX = 40, kAreaY = 85, kAreaW = 720, kAreaH = 380;
  mazeCellPx_ = std::min(kAreaW / mazeCols_, kAreaH / mazeRows_);
  const int w = mazeCellPx_ * mazeCols_;
  const int h = mazeCellPx_ * mazeRows_;
  lv_obj_set_pos(mazeArea_, kAreaX + (kAreaW - w) / 2, kAreaY + (kAreaH - h) / 2);
  lv_obj_set_size(mazeArea_, w + 1, h + 1);
  lv_obj_set_style_bg_color(mazeArea_, lv_color_make(18, 18, 34), 0);
  lv_obj_set_style_bg_opa(mazeArea_, LV_OPA_COVER, 0);

  auto addCircle = [&](std::uint8_t cell, int diameter, lv_color_t color) {
    lv_obj_t* o = lv_obj_create(mazeArea_);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, diameter, diameter);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(o, color, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    const int cx = (cell % mazeCols_) * mazeCellPx_ + mazeCellPx_ / 2;
    const int cy = (cell / mazeCols_) * mazeCellPx_ + mazeCellPx_ / 2;
    lv_obj_set_pos(o, cx - diameter / 2, cy - diameter / 2);
    return o;
  };
  // Sizes mirror TiltMazePuzzle's kHoleRadius/kExitRadius (0.30 cells).
  const int markerPx = mazeCellPx_ * 6 / 10;
  lv_obj_t* exitMarker = addCircle(mazeExit_, markerPx, lv_color_make(0, 200, 120));
  lv_obj_set_style_shadow_width(exitMarker, mazeCellPx_ / 3, 0);
  lv_obj_set_style_shadow_color(exitMarker, lv_color_make(0, 255, 150), 0);
  for (std::uint8_t i = 0; i < mazeHoleCount_; ++i) {
    lv_obj_t* hole = addCircle(mazeHoles_[i], markerPx, lv_color_make(0, 0, 0));
    lv_obj_set_style_border_width(hole, 2, 0);
    lv_obj_set_style_border_color(hole, lv_color_make(200, 40, 40), 0);
  }

  constexpr int kWallPx = 6;
  const lv_color_t wallColor = lv_color_make(110, 140, 220);
  auto addWall = [&](int x, int y, int ww, int hh) {
    lv_obj_t* o = lv_obj_create(mazeArea_);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, ww, hh);
    lv_obj_set_style_bg_color(o, wallColor, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  };
  const int half = kWallPx / 2;
  addWall(0, 0, w + 1, kWallPx);              // outer border
  addWall(0, h + 1 - kWallPx, w + 1, kWallPx);
  addWall(0, 0, kWallPx, h + 1);
  addWall(w + 1 - kWallPx, 0, kWallPx, h + 1);
  const std::uint8_t cellCount = mazeCols_ * mazeRows_;
  for (std::uint8_t i = 0; i < cellCount && mazeWalls_[i] != '\0'; ++i) {
    const int bits = mazeWalls_[i] - '0';
    const int col = i % mazeCols_;
    const int row = i / mazeCols_;
    const int x = col * mazeCellPx_;
    const int y = row * mazeCellPx_;
    if ((bits & 0x01) && col < mazeCols_ - 1) addWall(x + mazeCellPx_ - half, y - half, kWallPx, mazeCellPx_ + kWallPx);
    if ((bits & 0x02) && row < mazeRows_ - 1) addWall(x - half, y + mazeCellPx_ - half, mazeCellPx_ + kWallPx, kWallPx);
  }

  // Ball diameter mirrors TiltMazePuzzle's kBallRadius (0.22 cells).
  const int ballPx = mazeCellPx_ * 44 / 100;
  lv_obj_set_size(mazeBall_, ballPx, ballPx);
  lv_obj_move_foreground(mazeBall_);
}

void GameView::updateTiltMazeScreen() {
  if (mazeId_ != mazeBuiltId_) {
    rebuildMaze();
  }
  if (mazeCellPx_ > 0) {
    const int ballPx = lv_obj_get_width(mazeBall_);
    const int x = lv_obj_get_x(mazeArea_) + mazeBallX_ * mazeCellPx_ / 100 - ballPx / 2;
    const int y = lv_obj_get_y(mazeArea_) + mazeBallY_ * mazeCellPx_ / 100 - ballPx / 2;
    lv_obj_set_pos(mazeBall_, x, y);
  }

  if (!mazeSensorOnline_) {
    lv_obj_set_style_text_color(mazeStatusLabel_, lv_color_make(255, 80, 80), 0);
    lv_label_set_text(mazeStatusLabel_, "KANTELSENSOR OFFLINE");
  } else {
    lv_obj_set_style_text_color(mazeStatusLabel_, lv_color_make(160, 160, 180), 0);
    lv_label_set_text_fmt(mazeStatusLabel_, "KANTEL DE DOOS NAAR DE GROENE KERN  -  GEVALLEN: %u", mazeFalls_);
  }
}

void GameView::buildGenericPuzzleScreen() {
  genericPuzzleScreen_ = createScreen("", &genericTitleLabel_);
  addInstructionLabel(genericPuzzleScreen_, "KIJK NAAR HET PANEEL", 220);
}

void GameView::buildHighscoreEntryScreen() {
  highscoreEntryScreen_ = createScreen("NIEUW RECORD");
  addInstructionLabel(highscoreEntryScreen_, "ENCODERS 1-3 OM LETTERS TE KIEZEN", 50);

  hsLettersLabel_ = lv_label_create(highscoreEntryScreen_);
  lv_obj_set_style_text_color(hsLettersLabel_, lv_color_make(255, 220, 0), 0);
  lv_obj_set_style_text_font(hsLettersLabel_, &lv_font_montserrat_48, 0);
  lv_obj_align(hsLettersLabel_, LV_ALIGN_TOP_MID, 0, 120);

  hsScoreLabel_ = lv_label_create(highscoreEntryScreen_);
  lv_obj_set_style_text_color(hsScoreLabel_, lv_color_make(0, 220, 160), 0);
  lv_obj_set_style_text_font(hsScoreLabel_, &lv_font_montserrat_28, 0);
  lv_obj_align(hsScoreLabel_, LV_ALIGN_TOP_MID, 0, 220);

  buildHighscoreRows(highscoreEntryScreen_, hsHighscoreRows_, 150, 300);
}

void GameView::buildUi() {
  defaultScreen_ = createScreen("CHRONOLAB-X13");

  disconnectedScreen_ = lv_obj_create(nullptr);
  lv_obj_remove_style_all(disconnectedScreen_);
  lv_obj_set_size(disconnectedScreen_, display_board::kWidth, display_board::kHeight);
  lv_obj_set_style_bg_color(disconnectedScreen_, lv_color_make(10, 0, 0), 0);
  lv_obj_set_style_bg_opa(disconnectedScreen_, LV_OPA_COVER, 0);
  lv_obj_clear_flag(disconnectedScreen_, LV_OBJ_FLAG_SCROLLABLE);
  {
    lv_obj_t* label = lv_label_create(disconnectedScreen_);
    lv_obj_set_style_text_color(label, lv_color_make(255, 80, 80), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, 0);
    lv_label_set_text(label, "GEEN VERBINDING MET REACTOR");
    lv_obj_center(label);
  }

  setupScreen_ = createScreen("STEL TIJDSLIMIET IN");
  addInstructionLabel(setupScreen_, "ENCODER 1 DRAAIEN, KNOP OM TE STARTEN", 50);
  setupTimerLabel_ = lv_label_create(setupScreen_);
  lv_obj_set_style_text_color(setupTimerLabel_, lv_color_make(0, 220, 160), 0);
  lv_obj_set_style_text_font(setupTimerLabel_, &lv_font_montserrat_48, 0);
  lv_obj_align(setupTimerLabel_, LV_ALIGN_TOP_MID, 0, 130);
  buildHighscoreRows(setupScreen_, setupHighscoreRows_, 150, 260);

  countdownScreen_ = createScreen("REACTOR STABILISEERT");
  countdownLabel_ = lv_label_create(countdownScreen_);
  lv_obj_set_style_text_color(countdownLabel_, lv_color_make(255, 200, 0), 0);
  lv_obj_set_style_text_font(countdownLabel_, &lv_font_montserrat_48, 0);
  lv_obj_align(countdownLabel_, LV_ALIGN_CENTER, 0, 20);

  tunerScreen_ = createScreen("RESONANTIEKAMER");
  buildProximityColumns(tunerScreen_, tunerBars_);

  livingScreen_ = createScreen("LEVEND INTERVAL");
  buildProximityColumns(livingScreen_, livingBars_);

  buildGridScreen();
  buildTetrisScreen();
  buildTiltMazeScreen();
  buildGenericPuzzleScreen();

  successScreen_ = createScreen("REACTOR GESTABILISEERD");
  successTimeLabel_ = lv_label_create(successScreen_);
  lv_obj_set_style_text_color(successTimeLabel_, lv_color_make(0, 255, 100), 0);
  lv_obj_set_style_text_font(successTimeLabel_, &lv_font_montserrat_48, 0);
  lv_obj_align(successTimeLabel_, LV_ALIGN_TOP_MID, 0, 130);
  {
    lv_obj_t* label = lv_label_create(successScreen_);
    lv_obj_set_style_text_color(label, lv_color_make(160, 160, 180), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, "EINDSCORE");
    lv_obj_align(label, LV_ALIGN_TOP_MID, 0, 230);
  }

  buildHighscoreEntryScreen();
  buildDebugScreen();

  timeoutScreen_ = createScreen("TIJD VERSTREKEN");
  {
    lv_obj_t* line1 = lv_label_create(timeoutScreen_);
    lv_obj_set_style_text_color(line1, lv_color_make(255, 80, 80), 0);
    lv_obj_set_style_text_font(line1, &lv_font_montserrat_28, 0);
    lv_label_set_text(line1, "DE REACTOR IS AFGESLOTEN");
    lv_obj_align(line1, LV_ALIGN_TOP_MID, 0, 190);

    lv_obj_t* line2 = lv_label_create(timeoutScreen_);
    lv_obj_set_style_text_color(line2, lv_color_make(160, 160, 180), 0);
    lv_obj_set_style_text_font(line2, &lv_font_montserrat_20, 0);
    lv_label_set_text(line2, "DRUK OP EEN KNOP OM OPNIEUW TE BEGINNEN");
    lv_obj_align(line2, LV_ALIGN_TOP_MID, 0, 250);
  }

  // Persistent room-clock overlay, built on the top layer (not any one
  // screen) so it survives lv_scr_load() and stays visible across every
  // puzzle screen -- previously the clock was only ever shown on the
  // Setup/Countdown/Success screens, disappearing the moment a puzzle
  // started. Shown only while state_ == Active (see updatePersistentTimer());
  // Setup/Countdown/Success/HighscoreEntry each already have their own
  // dedicated time display.
  persistentTimerPanel_ = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(persistentTimerPanel_);
  lv_obj_set_style_bg_color(persistentTimerPanel_, lv_color_make(0, 0, 0), 0);
  lv_obj_set_style_bg_opa(persistentTimerPanel_, LV_OPA_70, 0);
  lv_obj_set_style_radius(persistentTimerPanel_, 6, 0);
  lv_obj_set_size(persistentTimerPanel_, 110, 44);
  lv_obj_align(persistentTimerPanel_, LV_ALIGN_TOP_RIGHT, -12, 12);
  lv_obj_clear_flag(persistentTimerPanel_, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(persistentTimerPanel_, LV_OBJ_FLAG_HIDDEN);

  persistentTimerLabel_ = lv_label_create(persistentTimerPanel_);
  lv_obj_set_style_text_color(persistentTimerLabel_, lv_color_make(255, 220, 80), 0);
  lv_obj_set_style_text_font(persistentTimerLabel_, &lv_font_montserrat_28, 0);
  lv_obj_center(persistentTimerLabel_);
}

void GameView::updatePersistentTimer() {
  if (state_ != protocol::GameState::Active) {
    lv_obj_add_flag(persistentTimerPanel_, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(persistentTimerPanel_, LV_OBJ_FLAG_HIDDEN);
  const std::uint32_t mm = (remainingSeconds_ / 60) % 100;
  const std::uint32_t ss = remainingSeconds_ % 60;
  lv_label_set_text_fmt(persistentTimerLabel_, "%02u:%02u", static_cast<unsigned>(mm),
                         static_cast<unsigned>(ss));
}

void GameView::buildDebugScreen() {
  debugScreen_ = createScreen("DEBUG TESTPATROON");

  // Four full-width colour bands -- confirms each RGB channel and the
  // backlight actually render correctly on real hardware, not just "the
  // panel lights up".
  struct Band { const char* label; lv_color_t color; };
  const Band bands[4] = {
      {"ROOD", lv_color_make(255, 0, 0)},
      {"GROEN", lv_color_make(0, 255, 0)},
      {"BLAUW", lv_color_make(0, 0, 255)},
      {"WIT", lv_color_make(255, 255, 255)},
  };
  constexpr int kBandY = 60;
  constexpr int kBandH = 70;
  for (std::uint8_t i = 0; i < 4; ++i) {
    lv_obj_t* band = lv_obj_create(debugScreen_);
    lv_obj_remove_style_all(band);
    lv_obj_set_size(band, display_board::kWidth - 40, kBandH);
    lv_obj_set_style_bg_color(band, bands[i].color, 0);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, 0);
    lv_obj_align(band, LV_ALIGN_TOP_MID, 0, kBandY + i * (kBandH + 8));

    lv_obj_t* label = lv_label_create(band);
    lv_obj_set_style_text_color(label, lv_color_make(0, 0, 0), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_label_set_text(label, bands[i].label);
    lv_obj_center(label);
  }

  // Absolutely-positioned dot that follows the raw touch coordinate exactly
  // -- lets you visually confirm the reported point lands where you actually
  // touch, not just that *some* number changes. A rotated/mirrored/scaled
  // touch mapping would show the dot appearing away from your finger, which
  // is the leading suspect for a puzzle (e.g. Resonant Grid) whose on-screen
  // buttons never register a click even though touch itself "works".
  constexpr int kDotSize = 28;
  debugTouchDot_ = lv_obj_create(debugScreen_);
  lv_obj_remove_style_all(debugTouchDot_);
  lv_obj_set_size(debugTouchDot_, kDotSize, kDotSize);
  lv_obj_set_style_radius(debugTouchDot_, kDotSize / 2, 0);
  lv_obj_set_style_bg_color(debugTouchDot_, lv_color_make(255, 0, 255), 0);
  lv_obj_set_style_bg_opa(debugTouchDot_, LV_OPA_COVER, 0);
  lv_obj_add_flag(debugTouchDot_, LV_OBJ_FLAG_HIDDEN);

  debugTouchLabel_ = lv_label_create(debugScreen_);
  lv_obj_set_style_text_color(debugTouchLabel_, lv_color_make(220, 220, 220), 0);
  lv_obj_set_style_text_font(debugTouchLabel_, &lv_font_montserrat_28, 0);
  lv_label_set_text(debugTouchLabel_, "Raak het scherm aan...");
  lv_obj_align(debugTouchLabel_, LV_ALIGN_BOTTOM_MID, 0, -85);

  // Raw, undecoded bytes off the I2C bus -- reveals whether the hardware
  // itself is producing sane, stable data or noise, independent of whatever
  // interpretation (byte order, swapped axes, scale) TouchService applies.
  debugTouchRawLabel_ = lv_label_create(debugScreen_);
  lv_obj_set_style_text_color(debugTouchRawLabel_, lv_color_make(150, 200, 255), 0);
  lv_obj_set_style_text_font(debugTouchRawLabel_, &lv_font_montserrat_20, 0);
  lv_label_set_text(debugTouchRawLabel_, "status=.. point=.. .. .. .. .. .. .. ..");
  lv_obj_align(debugTouchRawLabel_, LV_ALIGN_BOTTOM_MID, 0, -55);

  // GT911's own configured resolution, read once at boot -- if one of these
  // is far below 800/480, that axis is squashed at the source, not a bug
  // in how this code interprets the point registers.
  if (touch_ != nullptr) {
    lv_obj_t* configLabel = lv_label_create(debugScreen_);
    lv_obj_set_style_text_color(configLabel, lv_color_make(255, 200, 100), 0);
    lv_obj_set_style_text_font(configLabel, &lv_font_montserrat_20, 0);
    lv_label_set_text_fmt(configLabel, "GT911 config: X_MAX=%u Y_MAX=%u", touch_->configXMax(),
                           touch_->configYMax());
    lv_obj_align(configLabel, LV_ALIGN_BOTTOM_MID, 0, -115);
  }

  lv_obj_t* hint = lv_label_create(debugScreen_);
  lv_obj_set_style_text_color(hint, lv_color_make(140, 140, 150), 0);
  lv_obj_set_style_text_font(hint, &lv_font_montserrat_20, 0);
  lv_label_set_text(hint, "Uitzetten via /debug op de main controller");
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -20);
}

void GameView::updateDebugScreen() {
  lv_point_t point{0, 0};
  const bool pressed = lvIndev_ != nullptr && lv_indev_get_state(lvIndev_) == LV_INDEV_STATE_PRESSED;
  if (pressed) {
    lv_indev_get_point(lvIndev_, &point);
  }
  lv_label_set_text_fmt(debugTouchLabel_, pressed ? "Touch: x=%d y=%d" : "Raak het scherm aan...",
                         static_cast<int>(point.x), static_cast<int>(point.y));

  if (pressed) {
    lv_obj_clear_flag(debugTouchDot_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(debugTouchDot_, point.x - 14, point.y - 14);
  } else {
    lv_obj_add_flag(debugTouchDot_, LV_OBJ_FLAG_HIDDEN);
  }

  if (touch_ != nullptr) {
    const std::uint8_t* p = touch_->lastRawPoint();
    lv_label_set_text_fmt(debugTouchRawLabel_,
                           "status=%02X point=%02X %02X %02X %02X %02X %02X %02X %02X",
                           touch_->lastStatusByte(), p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  }
}

// ---------------------------------------------------------------------------
// Per-poll widget updates
// ---------------------------------------------------------------------------

void GameView::updateHighscoreRows(std::array<lv_obj_t*, kHighscoreShown>& rows) {
  const std::uint8_t shown = highscoreCount_ < kHighscoreShown ? highscoreCount_ : kHighscoreShown;
  for (std::uint8_t i = 0; i < kHighscoreShown; ++i) {
    if (i >= shown) {
      lv_obj_add_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    lv_obj_clear_flag(rows[i], LV_OBJ_FLAG_HIDDEN);
    const auto& row = highscores_[i];
    const std::uint32_t mm = (row.score / 60) % 100;
    const std::uint32_t ss = row.score % 60;
    lv_label_set_text_fmt(rows[i], "%u. %s   %02u:%02u", i + 1, row.initials, mm, ss);
    lv_obj_set_style_text_color(rows[i], i == 0 ? lv_color_make(255, 220, 0) : lv_color_make(200, 200, 220), 0);
  }
}

void GameView::updateSetupScreen() {
  const std::uint32_t mm = (selectedLimitSeconds_ / 60) % 100;
  const std::uint32_t ss = selectedLimitSeconds_ % 60;
  lv_label_set_text_fmt(setupTimerLabel_, "%02u:%02u", mm, ss);
  updateHighscoreRows(setupHighscoreRows_);
}

void GameView::updateCountdownScreen() {
  lv_label_set_text_fmt(countdownLabel_, "%u", static_cast<unsigned>(countdownSeconds_));
}

void GameView::updateTunerScreen() {
  for (std::uint8_t i = 0; i < 3; ++i) {
    const EncoderField& f = tunerFields_[i];
    lv_bar_set_value(tunerBars_[i], static_cast<std::int32_t>(f.proximity * 1000.0f), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tunerBars_[i], colorForProximity(f.proximity), LV_PART_INDICATOR);
    lv_obj_set_style_border_color(tunerBars_[i], f.locked ? lv_color_make(255, 255, 255) : lv_color_make(60, 60, 70),
                                   LV_PART_MAIN);
  }
}

void GameView::updateLivingScreen() {
  for (std::uint8_t i = 0; i < 3; ++i) {
    const LivingField& f = livingFields_[i];
    lv_bar_set_value(livingBars_[i], static_cast<std::int32_t>(f.holdProgress * 1000.0f), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(livingBars_[i], f.holding ? lv_color_make(0, 220, 255) : lv_color_make(120, 60, 0),
                               LV_PART_INDICATOR);
    lv_obj_set_style_border_color(
        livingBars_[i], f.holdProgress >= 1.0f ? lv_color_make(255, 255, 255) : lv_color_make(60, 60, 70),
        LV_PART_MAIN);
  }
}

void GameView::gridCellRect(std::uint8_t cell, int& x, int& y, int& w, int& h) const {
  constexpr int kAreaSize = 360;
  constexpr int kAreaX = (800 - kAreaSize) / 2;
  constexpr int kAreaY = 90;
  const int cellSize = kAreaSize / gridSize_;
  const int row = cell / gridSize_;
  const int col = cell % gridSize_;
  x = kAreaX + col * cellSize;
  y = kAreaY + row * cellSize;
  w = cellSize - 6;
  h = cellSize - 6;
}

void GameView::updateGridScreen() {
  for (std::uint8_t cell = 0; cell < kMaxGridCells; ++cell) {
    if (cell >= gridTotalCells_) {
      lv_obj_add_flag(gridCells_[cell], LV_OBJ_FLAG_HIDDEN);
      continue;
    }
    int x, y, w, h;
    gridCellRect(cell, x, y, w, h);
    lv_obj_t* btn = gridCells_[cell];
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    const bool locked = (gridLockedMask_ & (1u << cell)) != 0;
    lv_obj_set_style_bg_color(btn, locked ? lv_color_make(0, 220, 100) : lv_color_make(40, 40, 60), 0);
  }
  lv_label_set_text_fmt(gridCountLabel_, "%u / %u VELDEN", gridCursor_, gridTotalCells_);
}

void GameView::postGridTouch(std::uint8_t cell) {
  // One newline-framed JSON line back over the same wire (display TX ->
  // main controller RX); see AppController::pollDisplayUart.
  mainUart_.printf("{\"touch\":%u}\n", cell);
}

void GameView::updateTetrisScreen() {
  if (tetrisCanvasBuf_ != nullptr) {
    lv_layer_t layer;
    lv_canvas_init_layer(tetrisCanvas_, &layer);

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_opa = LV_OPA_COVER;

    for (std::uint8_t r = 0; r < kTetrisHeight; ++r) {
      for (std::uint8_t c = 0; c < kTetrisWidth; ++c) {
        dsc.bg_color = colorForPieceType(tetrisBoard_[r][c]);
        lv_area_t area;
        area.x1 = c * kTetrisCellPx;
        area.y1 = r * kTetrisCellPx;
        area.x2 = area.x1 + kTetrisCellPx - 2;
        area.y2 = area.y1 + kTetrisCellPx - 2;
        lv_draw_rect(&layer, &dsc, &area);
      }
    }

    lv_canvas_finish_layer(tetrisCanvas_, &layer);
  }

  lv_label_set_text_fmt(tetrisLinesLabel_, "REGELS %u/%u", tetrisLinesCleared_, tetrisTargetLines_);

  if (tetrisPaused_) {
    lv_obj_clear_flag(tetrisPausedLabel_, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(tetrisPausedLabel_, LV_OBJ_FLAG_HIDDEN);
  }
}

void GameView::updateGenericPuzzleScreen() {
  lv_label_set_text(genericTitleLabel_, puzzleId_ == "VibrationalCipher" ? "TRILLINGSCIJFER" : "ENERGIEPATROON");
}

void GameView::updateSuccessScreen() {
  const std::uint32_t mm = (score_ / 60) % 100;
  const std::uint32_t ss = score_ % 60;
  lv_label_set_text_fmt(successTimeLabel_, "%02u:%02u", mm, ss);
}

void GameView::updateHighscoreEntryScreen() {
  lv_label_set_text_fmt(hsLettersLabel_, "%c %c %c", hsLetters_[0], hsLetters_[1], hsLetters_[2]);
  const std::uint32_t mm = (hsScore_ / 60) % 100;
  const std::uint32_t ss = hsScore_ % 60;
  lv_label_set_text_fmt(hsScoreLabel_, "%02u:%02u", mm, ss);
  updateHighscoreRows(hsHighscoreRows_);
}

#endif  // ESP32_8048S050C
