#pragma once

#include <Adafruit_MCP23X17.h>
#include <Adafruit_PWMServoDriver.h>

#include <Arduino.h>
#include <array>
#include <cstdint>
#include <functional>
#include <utility>

#include "devices/device_pins.h"
#include "devices/encoder_hardware_config.h"
#include "protocol/game_state.h"

struct EncoderState {
  int32_t value = 0;         // raw quadrature accumulator, signed, free-running, never reset
  bool buttonPressed = false;
  bool lastA = false;
  bool lastB = false;
};

struct EncoderLedState {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

enum class EasingFunction {
  kLinear,
  kEaseIn,
  kEaseOut,
  kEaseInOut,
  kBounce,
};

// TM1638 LED&KEY (display + 8 buttons) and the three MCP23017/PCA9685
// rotary encoders. Both halves share this class. See spec/puzzlebox_hw.md
// sections 4.4 and 4.5.
class InputPanelService {
 public:
  enum class Mode {
    kStatus,
    kPatternPuzzle,
    kTetrisPuzzle,
    kSpectralTuner,
    kCipherPuzzle,
    kSetup,
    kHighscoreEntry,
  };

  void begin();
  void poll();  // every loop: reads buttons/encoders, advances LED animations

  // --- TM1638 display ---
  void renderStatus(protocol::GameState state, std::uint32_t remainingSeconds, bool sidecarOnline);
  void renderPatternPuzzle(const char* phase, std::uint8_t ledMask, std::uint8_t progress,
                            std::uint8_t targetLength, bool blinkOn);
  void renderTetrisPuzzle(const char* phase, std::uint8_t level, std::uint8_t targetLevel,
                           std::uint8_t ledMask, bool blinkOn);
  // lockedMask bit i = encoder i locked; mirrored onto TM1638 LEDs 0-2.
  void renderSpectralTuner(std::uint8_t lockedMask, std::uint8_t encoderCount);
  // phase (STBY/XMIT/INPT/ERR /GOOD) + entered digit values so far (0-7 each).
  void renderCipherPuzzle(const char* phase, const std::uint8_t* enteredDigits,
                           std::uint8_t enteredCount, std::uint8_t totalCount);
  // Time-limit selection screen: MM.SS of the currently selected limit.
  void renderSetup(std::uint32_t selectedSeconds);
  // 3 letters (A-Z), live from the 3 encoders, + the score they're being set against.
  void renderHighscoreEntry(const char letters[3], std::uint32_t scoreSeconds);
  void setStatusLed(bool bright);  // brightness 7 (active) vs 1 (idle)

  // --- Debug override (used by AppController's /debug hardware test page) ---
  // While active, every render*()/setStatusLed() call above no-ops instead of
  // touching the TM1638, so runDisplayTest()'s pattern stays on screen
  // instead of being overwritten by the game's own per-tick rendering.
  void setDebugOverrideActive(bool active) {
    if (debugOverrideActive_ && !active) {
      // The test pattern wrote straight to the TM1638 without updating the
      // change-gating cache below, so force the next render*() call to
      // actually redraw instead of assuming nothing changed.
      lastRenderedTop_ = "";
    }
    debugOverrideActive_ = active;
  }
  void runDisplayTest();  // "12345678" on all digit positions + all 8 LEDs lit
  bool debugOverrideActive() const { return debugOverrideActive_; }

  // --- TM1638 buttons ---
  bool ledKeyPressed() const { return buttonMask_ != 0; }
  std::uint8_t ledKeyButtonMask() const { return buttonMask_; }
  // Consumes one queued press (lowest index first).
  bool takeLedKeyButtonPress(std::uint8_t& buttonIndex);

  // --- Encoders ---
  const std::array<EncoderState, encoder_hw::kEncoderCount>& encoders() const { return encoders_; }
  const EncoderState& encoder(std::uint8_t index) const { return encoders_[index]; }

  void setEncoderLed(std::uint8_t index, std::uint8_t r, std::uint8_t g, std::uint8_t b);
  void setEncoderLed(std::uint8_t index, const EncoderLedState& target);
  void fadeEncoderLed(std::uint8_t index, const EncoderLedState& target, std::uint32_t durationMs);
  void pulseEncoderLed(std::uint8_t index, const EncoderLedState& target, std::uint32_t durationMs);
  void blinkEncoderLed(std::uint8_t index, const EncoderLedState& target, int count,
                        std::uint32_t stepMs);
  void queueEncoderAnimation(std::uint8_t index, const EncoderLedState& target,
                              std::uint32_t durationMs, EasingFunction easing,
                              std::function<void()> onDone = nullptr);
  void clearEncoderAnimation(std::uint8_t index);

 private:
  struct Animation {
    bool active = false;
    EncoderLedState from;
    EncoderLedState target;
    std::uint32_t startMs = 0;
    std::uint32_t durationMs = 0;
    EasingFunction easing = EasingFunction::kLinear;
    std::function<void()> onDone;
  };

  // --- TM1638 bit-banged driver ---
  void tm1638Start();
  void tm1638Stop();
  void tm1638ShiftOut(std::uint8_t data);
  std::uint8_t tm1638ShiftIn();
  void tm1638SendCommand(std::uint8_t cmd);
  std::uint8_t tm1638ReadButtons();
  static std::pair<bool, bool> decodeTm1638ButtonPair(std::uint8_t scanByte);
  void tm1638WriteText(const char text[8], std::uint8_t dotMask);
  void tm1638WriteLeds(std::uint8_t ledMask);
  static std::uint8_t segmentsForChar(char c);

  // --- MCP23017 encoder inputs ---
  void pollEncoders();
  void pollButtons();

  // --- PCA9685 encoder LEDs ---
  void writeEncoderLed(std::uint8_t index, const EncoderLedState& state);
  void advanceAnimations();

  Adafruit_MCP23X17 mcp_;
  Adafruit_PWMServoDriver pca_{i2c_addr::kPca9685};

  Mode mode_ = Mode::kStatus;
  bool debugOverrideActive_ = false;

  std::uint8_t buttonMask_ = 0;          // live state
  std::uint8_t pendingButtonQueue_ = 0;  // bitmask of unread presses

  std::array<EncoderState, encoder_hw::kEncoderCount> encoders_{};
  std::array<Animation, encoder_hw::kEncoderCount> animationSlot_{};
  std::array<EncoderLedState, encoder_hw::kEncoderCount> currentLed_{};

  // Change-gating for the idempotent render*() calls.
  String lastRenderedTop_;
  String lastRenderedBottom_;
  std::uint8_t lastRenderedDots_ = 0xFF;
};
