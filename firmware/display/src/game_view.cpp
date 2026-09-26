#include "game_view.h"

#if defined(ESP32_8048S050C)

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "config/network_config.h"

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
constexpr std::uint32_t kPollIntervalMs = 200;

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
// Status polling
// ---------------------------------------------------------------------------

bool GameView::fetchStatus() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(800);
  http.begin(String("http://") + config::kMainControllerIp + "/api/game");
  const int code = http.GET();
  if (code != 200) {
    http.end();
    return false;
  }
  const String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body) != DeserializationError::Ok) {
    return false;
  }

  state_ = static_cast<protocol::GameState>(doc["state"] | 0);
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

  tetrisLevel_ = doc["tetris"]["level"] | 1;
  tetrisTargetLevel_ = doc["tetris"]["targetLevel"] | 3;
  tetrisStability_ = doc["tetris"]["stability"] | 1.0f;
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

  return true;
}

void GameView::poll() {
  lv_timer_handler();  // LVGL needs frequent ticks for input/animation, independent of the network poll below

  const std::uint32_t now = millis();
  if (now - lastPollMs_ < kPollIntervalMs) {
    return;
  }
  lastPollMs_ = now;

  if (fetchStatus()) {
    connected_ = true;
    render();
  } else if (connected_) {
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

void GameView::render() {
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
  constexpr int kMeterW = 240;
  constexpr int kMeterH = 40;

  tetrisStabilityBar_ = lv_bar_create(tetrisScreen_);
  lv_obj_set_pos(tetrisStabilityBar_, kMeterX, kMeterY);
  lv_obj_set_size(tetrisStabilityBar_, kMeterW, kMeterH);
  lv_bar_set_range(tetrisStabilityBar_, 0, 1000);
  lv_obj_set_style_radius(tetrisStabilityBar_, 0, LV_PART_MAIN);
  lv_obj_set_style_radius(tetrisStabilityBar_, 0, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(tetrisStabilityBar_, bgColor(), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(tetrisStabilityBar_, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_border_width(tetrisStabilityBar_, 2, LV_PART_MAIN);
  lv_obj_set_style_border_color(tetrisStabilityBar_, lv_color_make(90, 90, 110), LV_PART_MAIN);

  lv_obj_t* stabilityLabel = lv_label_create(tetrisScreen_);
  lv_obj_set_style_text_color(stabilityLabel, lv_color_make(200, 200, 220), 0);
  lv_obj_set_style_text_font(stabilityLabel, &lv_font_montserrat_20, 0);
  lv_label_set_text(stabilityLabel, "STABILITEIT");
  lv_obj_set_pos(stabilityLabel, kMeterX, kMeterY + kMeterH + 16);

  tetrisLevelLabel_ = lv_label_create(tetrisScreen_);
  lv_obj_set_style_text_color(tetrisLevelLabel_, lv_color_make(220, 220, 230), 0);
  lv_obj_set_style_text_font(tetrisLevelLabel_, &lv_font_montserrat_28, 0);
  lv_obj_set_pos(tetrisLevelLabel_, kMeterX, kMeterY + 90);

  tetrisPausedLabel_ = lv_label_create(tetrisScreen_);
  lv_obj_set_style_text_color(tetrisPausedLabel_, lv_color_make(255, 200, 0), 0);
  lv_obj_set_style_text_font(tetrisPausedLabel_, &lv_font_montserrat_28, 0);
  lv_label_set_text(tetrisPausedLabel_, "GEPAUZEERD");
  lv_obj_set_pos(tetrisPausedLabel_, kMeterX, kMeterY + 160);
  lv_obj_add_flag(tetrisPausedLabel_, LV_OBJ_FLAG_HIDDEN);
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
  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(500);
  http.begin(String("http://") + config::kMainControllerIp + "/api/touch");
  http.addHeader("Content-Type", "application/json");
  char body[32];
  snprintf(body, sizeof(body), "{\"cell\":%u}", cell);
  http.POST(body);
  http.end();
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

  lv_bar_set_value(tetrisStabilityBar_, static_cast<std::int32_t>(tetrisStability_ * 1000.0f), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(tetrisStabilityBar_, lerpColor(220, 0, 0, 0, 220, 0, tetrisStability_), LV_PART_INDICATOR);

  lv_label_set_text_fmt(tetrisLevelLabel_, "NIVEAU %u/%u", tetrisLevel_, tetrisTargetLevel_);

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
