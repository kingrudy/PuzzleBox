#include "devices/input_panel_service.h"

#include <Wire.h>
#include <cmath>

namespace {

constexpr std::uint8_t kCmdDataAutoIncrement = 0x40;
constexpr std::uint8_t kCmdAddressBase = 0xC0;
constexpr std::uint8_t kCmdDisplayControlBase = 0x88;  // | brightness(0-7), display on
constexpr std::uint8_t kCmdReadKeys = 0x42;

constexpr std::uint8_t kIdleBrightness = 1;
constexpr std::uint8_t kActiveBrightness = 7;

float applyEasing(EasingFunction easing, float t) {
  switch (easing) {
    case EasingFunction::kEaseIn:
      return t * t;
    case EasingFunction::kEaseOut:
      return 1.0f - (1.0f - t) * (1.0f - t);
    case EasingFunction::kEaseInOut:
      return (t < 0.5f) ? 2.0f * t * t : 1.0f - std::pow(-2.0f * t + 2.0f, 2.0f) / 2.0f;
    case EasingFunction::kBounce: {
      constexpr float n1 = 7.5625f;
      constexpr float d1 = 2.75f;
      if (t < 1.0f / d1) return n1 * t * t;
      if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
      if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
      t -= 2.625f / d1;
      return n1 * t * t + 0.984375f;
    }
    case EasingFunction::kLinear:
    default:
      return t;
  }
}

std::uint8_t lerp8(std::uint8_t from, std::uint8_t to, float t) {
  const float value = from + (static_cast<float>(to) - static_cast<float>(from)) * t;
  if (value <= 0.0f) return 0;
  if (value >= 255.0f) return 255;
  return static_cast<std::uint8_t>(value + 0.5f);
}

// Standard quadrature Gray-code delta table, indexed by
// (lastState << 2) | newState, where state = (A << 1) | B.
constexpr int8_t kQuadratureDelta[16] = {
    0, -1, 1,  0,
    1, 0,  0,  -1,
    -1, 0, 0,  1,
    0,  1, -1, 0,
};

}  // namespace

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void InputPanelService::begin() {
  pinMode(pins::kTm1638Stb, OUTPUT);
  pinMode(pins::kTm1638Clk, OUTPUT);
  digitalWrite(pins::kTm1638Stb, HIGH);
  digitalWrite(pins::kTm1638Clk, HIGH);

  tm1638SendCommand(kCmdDataAutoIncrement);
  tm1638SendCommand(kCmdDisplayControlBase | kIdleBrightness);

  char blank[8] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' '};
  tm1638WriteText(blank, 0);
  tm1638WriteLeds(0);

  mcp_.begin_I2C(i2c_addr::kMcp23017);
  for (std::uint8_t i = 0; i < encoder_hw::kEncoderCount; ++i) {
    const auto& p = encoder_hw::kEncoderInputs[i];
    mcp_.pinMode(p.a, INPUT_PULLUP);
    mcp_.pinMode(p.b, INPUT_PULLUP);
    mcp_.pinMode(p.sw, encoder_hw::kEncoderSwitchActiveHigh[i] ? INPUT : INPUT_PULLUP);
    encoders_[i].lastA = mcp_.digitalRead(p.a) == HIGH;
    encoders_[i].lastB = mcp_.digitalRead(p.b) == HIGH;
  }

  pca_.begin();
  pca_.setOscillatorFrequency(encoder_hw::kPwmOscillatorHz);
  pca_.setPWMFreq(encoder_hw::kPwmFrequencyHz);

  // Blocking wiring self-test: encoder 1 red, encoder 2 green, encoder 3 blue.
  setEncoderLed(0, 255, 0, 0);
  setEncoderLed(1, 0, 255, 0);
  setEncoderLed(2, 0, 0, 255);
  delay(encoder_hw::kBootSelfTestMs);
  for (std::uint8_t i = 0; i < encoder_hw::kEncoderCount; ++i) {
    setEncoderLed(i, 0, 0, 0);
  }
}

void InputPanelService::poll() {
  pollEncoders();
  pollButtons();
  advanceAnimations();
}

// ---------------------------------------------------------------------------
// TM1638 bit-banged driver
// ---------------------------------------------------------------------------

void InputPanelService::tm1638Start() { digitalWrite(pins::kTm1638Stb, LOW); }
void InputPanelService::tm1638Stop() { digitalWrite(pins::kTm1638Stb, HIGH); }

void InputPanelService::tm1638ShiftOut(std::uint8_t data) {
  pinMode(pins::kTm1638Dio, OUTPUT);
  for (int i = 0; i < 8; ++i) {
    digitalWrite(pins::kTm1638Clk, LOW);
    digitalWrite(pins::kTm1638Dio, (data & 0x01) ? HIGH : LOW);
    data >>= 1;
    digitalWrite(pins::kTm1638Clk, HIGH);
  }
}

std::uint8_t InputPanelService::tm1638ShiftIn() {
  pinMode(pins::kTm1638Dio, INPUT_PULLUP);
  std::uint8_t data = 0;
  for (int i = 0; i < 8; ++i) {
    digitalWrite(pins::kTm1638Clk, LOW);
    delayMicroseconds(1);
    data |= (digitalRead(pins::kTm1638Dio) ? 0x01 : 0x00) << i;
    digitalWrite(pins::kTm1638Clk, HIGH);
  }
  pinMode(pins::kTm1638Dio, OUTPUT);
  return data;
}

void InputPanelService::tm1638SendCommand(std::uint8_t cmd) {
  tm1638Start();
  tm1638ShiftOut(cmd);
  tm1638Stop();
}

std::uint8_t InputPanelService::tm1638ReadButtons() {
  tm1638Start();
  tm1638ShiftOut(kCmdReadKeys);
  std::uint8_t scan[4];
  for (auto& b : scan) {
    b = tm1638ShiftIn();
  }
  tm1638Stop();

  std::uint8_t rawMask = 0;
  for (int i = 0; i < 4; ++i) {
    auto [first, second] = decodeTm1638ButtonPair(scan[i]);
    if (first) rawMask |= (1 << (2 * i));
    if (second) rawMask |= (1 << (2 * i + 1));
  }

  // KSi's "K1" leg scans to raw bit (2*i), its "K2" leg to raw bit (2*i+1) --
  // but on this board's actual silkscreen/wiring (confirmed by pressing each
  // button individually via the /debug page), K1 legs are S1-S4 and K2 legs
  // are S5-S8, not S1/S2 interleaved per KS line. Remap so bit n of the
  // returned mask always means physical button S(n+1) -- every consumer
  // (TetrisPuzzle, VibrationalCipherPuzzle, puzzlebox_hw.md's control
  // tables) already assumes that sequential numbering.
  static constexpr std::uint8_t kRawBitForButton[8] = {0, 2, 4, 6, 1, 3, 5, 7};
  std::uint8_t mask = 0;
  for (std::uint8_t button = 0; button < 8; ++button) {
    if (rawMask & (1 << kRawBitForButton[button])) {
      mask |= (1 << button);
    }
  }
  return mask;
}

std::pair<bool, bool> InputPanelService::decodeTm1638ButtonPair(std::uint8_t scanByte) {
  // Clone boards wire K2 onto either bit1/bit5 or bit0/bit4; accept both.
  const bool first = (scanByte & 0x01) || (scanByte & 0x02);
  const bool second = (scanByte & 0x10) || (scanByte & 0x20);
  return {first, second};
}

std::uint8_t InputPanelService::segmentsForChar(char c) {
  // Segment bit order: bit0=a bit1=b bit2=c bit3=d bit4=e bit5=f bit6=g bit7=dp.
  // Several letters are approximate — a 7-segment display can't render every
  // glyph distinctly.
  switch (toupper(c)) {
    case ' ': return 0x00;
    case '-': return 0x40;
    case '0': return 0x3F;
    case '1': return 0x06;
    case '2': return 0x5B;
    case '3': return 0x4F;
    case '4': return 0x66;
    case '5': return 0x6D;
    case '6': return 0x7D;
    case '7': return 0x07;
    case '8': return 0x7F;
    case '9': return 0x6F;
    case 'A': return 0x77;
    case 'B': return 0x7C;
    case 'C': return 0x39;
    case 'D': return 0x5E;
    case 'E': return 0x79;
    case 'G': return 0x3D;
    case 'H': return 0x76;
    case 'I': return 0x06;
    case 'L': return 0x38;
    case 'N': return 0x54;
    case 'O': return 0x3F;
    case 'P': return 0x73;
    case 'R': return 0x50;
    case 'S': return 0x6D;
    case 'T': return 0x78;
    case 'U': return 0x3E;
    case 'V': return 0x3E;
    case 'W': return 0x3E;
    case 'Y': return 0x6E;
    default: return 0x00;
  }
}

void InputPanelService::tm1638WriteText(const char text[8], std::uint8_t dotMask) {
  tm1638Start();
  tm1638ShiftOut(kCmdAddressBase);
  for (int i = 0; i < 8; ++i) {
    std::uint8_t segments = segmentsForChar(text[i]);
    if (dotMask & (1 << i)) segments |= 0x80;
    tm1638ShiftOut(segments);
    tm1638ShiftOut(0);  // the odd address per digit is that digit's single LED; setLeds() owns it
  }
  tm1638Stop();
}

void InputPanelService::tm1638WriteLeds(std::uint8_t ledMask) {
  for (int i = 0; i < 8; ++i) {
    tm1638Start();
    tm1638ShiftOut(kCmdAddressBase | static_cast<std::uint8_t>((2 * i + 1) & 0x0F));
    tm1638ShiftOut((ledMask & (1 << i)) ? 1 : 0);
    tm1638Stop();
  }
}

// ---------------------------------------------------------------------------
// Rendering
// ---------------------------------------------------------------------------

void InputPanelService::setStatusLed(bool bright) {
  if (debugOverrideActive_) return;
  tm1638SendCommand(kCmdDisplayControlBase | (bright ? kActiveBrightness : kIdleBrightness));
}

void InputPanelService::runDisplayTest() {
  debugOverrideActive_ = true;
  tm1638SendCommand(kCmdDisplayControlBase | kActiveBrightness);
  const char text[8] = {'1', '2', '3', '4', '5', '6', '7', '8'};
  tm1638WriteText(text, 0xFF);  // every digit + its decimal point
  tm1638WriteLeds(0xFF);        // all 8 discrete LEDs
}

void InputPanelService::renderStatus(protocol::GameState state, std::uint32_t remainingSeconds,
                                      bool sidecarOnline) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kStatus;

  const std::uint32_t mm = (remainingSeconds / 60) % 100;
  const std::uint32_t ss = remainingSeconds % 60;

  const char* stateCode = "BOOT";
  switch (state) {
    case protocol::GameState::Boot: stateCode = "BOOT"; break;
    case protocol::GameState::Idle: stateCode = "IDLE"; break;
    case protocol::GameState::Setup: stateCode = "SET "; break;
    case protocol::GameState::Countdown: stateCode = "CNT "; break;
    case protocol::GameState::Active: stateCode = "ACTV"; break;
    case protocol::GameState::Paused: stateCode = "PAUS"; break;
    case protocol::GameState::Timeout: stateCode = "TIME"; break;
    case protocol::GameState::Success: stateCode = "SUCC"; break;
    case protocol::GameState::HighscoreEntry: stateCode = "HISC"; break;
    case protocol::GameState::Maintenance: stateCode = "MAIN"; break;
  }

  char text[8];
  snprintf(text, 5, "%02u%02u", mm, ss);  // "MMSS", dot after digit 1 shown via dotMask
  memcpy(text + 4, stateCode, 4);

  const std::uint8_t dotMask = 0x02 | (sidecarOnline ? 0x80 : 0x00);  // dot on digit1 (MM.SS), digit7 = sidecar

  String key = String(text[0]) + text[1] + text[2] + text[3] + text[4] + text[5] + text[6] +
               text[7] + dotMask;
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, dotMask);
  tm1638WriteLeds(buttonMask_);  // mirror button state
}

void InputPanelService::renderPatternPuzzle(const char* phase, std::uint8_t ledMask,
                                             std::uint8_t progress, std::uint8_t targetLength,
                                             bool blinkOn) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kPatternPuzzle;

  char text[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
  for (int i = 0; i < 4 && phase[i] != '\0'; ++i) text[i] = phase[i];
  snprintf(text + 4, 5, "%u-%u", progress, targetLength);

  String key = String(phase) + "|" + progress + "/" + targetLength + "|" + ledMask + "|" +
               (blinkOn ? "1" : "0");
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, 0);
  tm1638WriteLeds(blinkOn ? ledMask : 0);
}

void InputPanelService::renderTetrisPuzzle(const char* phase, std::uint8_t level,
                                            std::uint8_t targetLevel, std::uint8_t ledMask,
                                            bool blinkOn) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kTetrisPuzzle;

  char text[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
  for (int i = 0; i < 4 && phase[i] != '\0'; ++i) text[i] = phase[i];
  snprintf(text + 4, 5, "%u-%u", level, targetLevel);

  String key = String(phase) + "|" + level + "/" + targetLevel + "|" + ledMask + "|" +
               (blinkOn ? "1" : "0");
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, 0);
  tm1638WriteLeds(blinkOn ? (ledMask & 0x3F) : 0);
}

void InputPanelService::renderSpectralTuner(std::uint8_t lockedMask, std::uint8_t encoderCount) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kSpectralTuner;

  std::uint8_t lockedCount = 0;
  for (std::uint8_t i = 0; i < encoderCount; ++i) {
    if (lockedMask & (1 << i)) ++lockedCount;
  }

  char text[9] = {'T', 'U', 'N', 'E', ' ', ' ', ' ', ' ', '\0'};
  snprintf(text + 4, 5, "%u-%u", lockedCount, encoderCount);

  String key = String("tune|") + lockedMask;
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, 0);
  tm1638WriteLeds(lockedMask);  // TM1638 LEDs 0-2 mirror encoder lock state
}

void InputPanelService::renderCipherPuzzle(const char* phase, const std::uint8_t* enteredDigits,
                                            std::uint8_t enteredCount, std::uint8_t totalCount) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kCipherPuzzle;

  char text[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
  for (int i = 0; i < 4 && phase[i] != '\0'; ++i) text[i] = phase[i];
  for (std::uint8_t i = 0; i < enteredCount && i < 4; ++i) {
    text[4 + i] = static_cast<char>('0' + (enteredDigits[i] % 10));
  }

  String key = String(phase) + "|" + enteredCount + "/" + totalCount;
  for (std::uint8_t i = 0; i < enteredCount && i < 4; ++i) {
    key += String("-") + enteredDigits[i];
  }
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, 0);
  tm1638WriteLeds(0);
}

void InputPanelService::renderSetup(std::uint32_t selectedSeconds) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kSetup;

  const std::uint32_t mm = (selectedSeconds / 60) % 100;
  const std::uint32_t ss = selectedSeconds % 60;

  char text[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
  snprintf(text, 5, "%02u%02u", mm, ss);  // snprintf's null terminator lands on text[4]...
  text[4] = 'S'; text[5] = 'E'; text[6] = 'T'; text[7] = ' ';  // ...overwritten here

  String key = String("setup|") + selectedSeconds;
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, 0x02);  // dot after MM
  tm1638WriteLeds(0);
}

void InputPanelService::renderHighscoreEntry(const char letters[3], std::uint32_t scoreSeconds) {
  if (debugOverrideActive_) return;
  mode_ = Mode::kHighscoreEntry;

  const std::uint32_t mm = (scoreSeconds / 60) % 100;
  const std::uint32_t ss = scoreSeconds % 60;

  char text[9] = {' ', ' ', ' ', ' ', ' ', ' ', ' ', ' ', '\0'};
  text[0] = letters[0];
  text[1] = letters[1];
  text[2] = letters[2];
  snprintf(text + 4, 5, "%02u%02u", mm, ss);

  String key = String("hisc|") + letters[0] + letters[1] + letters[2] + "|" + scoreSeconds;
  if (key == lastRenderedTop_) {
    return;
  }
  lastRenderedTop_ = key;

  tm1638WriteText(text, 0x20);  // dot after score MM
  tm1638WriteLeds(0);
}

// ---------------------------------------------------------------------------
// Buttons
// ---------------------------------------------------------------------------

void InputPanelService::pollButtons() {
  static std::uint32_t lastReadMs = 0;
  const std::uint32_t now = millis();
  if (now - lastReadMs < 20) {
    return;
  }
  lastReadMs = now;

  const std::uint8_t gameMask = tm1638ReadButtons();
  const std::uint8_t newlyPressed = gameMask & ~buttonMask_;
  pendingButtonQueue_ |= newlyPressed;
  buttonMask_ = gameMask;
}

bool InputPanelService::takeLedKeyButtonPress(std::uint8_t& buttonIndex) {
  if (pendingButtonQueue_ == 0) {
    return false;
  }
  for (std::uint8_t i = 0; i < 8; ++i) {
    if (pendingButtonQueue_ & (1 << i)) {
      pendingButtonQueue_ &= ~(1 << i);
      buttonIndex = i;
      return true;
    }
  }
  return false;
}

// ---------------------------------------------------------------------------
// Encoders
// ---------------------------------------------------------------------------

void InputPanelService::pollEncoders() {
  for (std::uint8_t i = 0; i < encoder_hw::kEncoderCount; ++i) {
    const auto& p = encoder_hw::kEncoderInputs[i];
    EncoderState& e = encoders_[i];

    const bool a = mcp_.digitalRead(p.a) == HIGH;
    const bool b = mcp_.digitalRead(p.b) == HIGH;
    const std::uint8_t lastState = (e.lastA ? 2 : 0) | (e.lastB ? 1 : 0);
    const std::uint8_t newState = (a ? 2 : 0) | (b ? 1 : 0);
    e.value += kQuadratureDelta[(lastState << 2) | newState];
    e.lastA = a;
    e.lastB = b;

    const bool swState = mcp_.digitalRead(p.sw) == HIGH;
    e.buttonPressed = encoder_hw::kEncoderSwitchActiveHigh[i] ? swState : !swState;
  }
}

void InputPanelService::setEncoderLed(std::uint8_t index, std::uint8_t r, std::uint8_t g,
                                       std::uint8_t b) {
  setEncoderLed(index, EncoderLedState{r, g, b});
}

void InputPanelService::setEncoderLed(std::uint8_t index, const EncoderLedState& target) {
  if (index >= encoder_hw::kEncoderCount) return;
  clearEncoderAnimation(index);
  currentLed_[index] = target;
  writeEncoderLed(index, target);
}

void InputPanelService::writeEncoderLed(std::uint8_t index, const EncoderLedState& state) {
  const auto& ch = encoder_hw::kEncoderLedChannels[index];

  auto scale = [&](std::uint8_t value, float multiplier) -> std::uint16_t {
    const float scaled = value * multiplier;
    const std::uint16_t duty =
        static_cast<std::uint16_t>((scaled / 255.0f) * (encoder_hw::kPwmResolution - 1));
    return ch.commonAnode ? (encoder_hw::kPwmResolution - 1 - duty) : duty;
  };

  pca_.setPWM(ch.r, 0, scale(state.r, encoder_hw::kBicolorRedMultiplier));
  pca_.setPWM(ch.g, 0, scale(state.g, encoder_hw::kBicolorGreenMultiplier));
  if (ch.b != encoder_hw::kNoChannel) {
    pca_.setPWM(ch.b, 0, scale(state.b, encoder_hw::kRgbBlueMultiplier));
  }
}

void InputPanelService::fadeEncoderLed(std::uint8_t index, const EncoderLedState& target,
                                        std::uint32_t durationMs) {
  queueEncoderAnimation(index, target, durationMs, EasingFunction::kLinear);
}

void InputPanelService::pulseEncoderLed(std::uint8_t index, const EncoderLedState& target,
                                         std::uint32_t durationMs) {
  const EncoderLedState origin = currentLed_[index];
  fadeEncoderLed(index, target, durationMs / 2);
  queueEncoderAnimation(index, origin, durationMs / 2, EasingFunction::kLinear);
}

void InputPanelService::blinkEncoderLed(std::uint8_t index, const EncoderLedState& target,
                                         int count, std::uint32_t stepMs) {
  const EncoderLedState origin = currentLed_[index];
  const int steps = min(count * 2, encoder_hw::kAnimationSlotsPerEncoder);
  for (int i = 0; i < steps; ++i) {
    queueEncoderAnimation(index, (i % 2 == 0) ? target : origin, stepMs, EasingFunction::kLinear);
  }
}

void InputPanelService::queueEncoderAnimation(std::uint8_t index, const EncoderLedState& target,
                                               std::uint32_t durationMs, EasingFunction easing,
                                               std::function<void()> onDone) {
  if (index >= encoder_hw::kEncoderCount) return;

  // Only one slot per encoder is modeled here; a newly queued animation
  // overwrites whatever was active, matching "a 5th queued animation
  // overwrites slot 4" for this simplified single-active-slot scheduler.
  Animation& slot = animationSlot_[index];
  slot.active = true;
  slot.from = currentLed_[index];
  slot.target = target;
  slot.startMs = millis();
  slot.durationMs = durationMs == 0 ? 1 : durationMs;
  slot.easing = easing;
  slot.onDone = std::move(onDone);
}

void InputPanelService::clearEncoderAnimation(std::uint8_t index) {
  if (index >= encoder_hw::kEncoderCount) return;
  animationSlot_[index].active = false;
}

void InputPanelService::advanceAnimations() {
  const std::uint32_t now = millis();
  for (std::uint8_t i = 0; i < encoder_hw::kEncoderCount; ++i) {
    Animation& slot = animationSlot_[i];
    if (!slot.active) continue;

    const float t = min(1.0f, static_cast<float>(now - slot.startMs) / slot.durationMs);
    const float eased = applyEasing(slot.easing, t);

    EncoderLedState current{
        lerp8(slot.from.r, slot.target.r, eased),
        lerp8(slot.from.g, slot.target.g, eased),
        lerp8(slot.from.b, slot.target.b, eased),
    };
    currentLed_[i] = current;
    writeEncoderLed(i, current);

    if (t >= 1.0f) {
      slot.active = false;
      if (slot.onDone) {
        auto cb = std::move(slot.onDone);
        slot.onDone = nullptr;
        cb();
      }
    }
  }
}
