#include "puzzles/living_interval_puzzle.h"

#include <Arduino.h>
#include <cmath>

#include "protocol/audio_cue.h"

namespace {
// There is no difficulty selector anywhere in the game (AppController's
// difficulty_ is hardcoded to kMedium) -- this puzzle tunes directly for
// that, per encoder, instead of layering per-encoder factors on top of
// unused Easy/Hard tier tables.
//
// Difficulty ramps encoder 1 (easiest) -> encoder 3 (hardest) via speed
// (period) and hold duration, at a FIXED tolerance for all three.
// Tolerance is deliberately *not* the difficulty lever: widening it to
// make an encoder "easier" also widens how long the drifting target sits
// within tolerance of the *untouched* baseline as it crosses zero every
// half-period -- wide enough (this was tried first, see
// spec/Specifications.md), and the puzzle solves itself without the
// encoder ever being touched. At tolerance=4/amplitude=25, that
// zero-crossing dwell stays safely under every hold duration below
// (checked at each encoder's *longest* period, the case most likely to
// self-solve), while the achievable dwell at the sine's peak (checked at
// each encoder's *shortest* period, the hardest case) stays above ~1.5x
// the hold duration even for encoder 3.
constexpr std::int32_t kLivingTolerance = 4;
constexpr float kLivingAmplitude = 25.0f;
constexpr float kEncoderPeriodMinMs[3] = {9000.0f, 7000.0f, 5600.0f};
constexpr float kEncoderPeriodMaxMs[3] = {13000.0f, 10000.0f, 8000.0f};
constexpr std::uint32_t kEncoderHoldDurationMs[3] = {900, 800, 700};

constexpr std::int32_t kVisibleRange = 20;
constexpr float kVoiceBaseHz[3] = {180.0f, 260.0f, 360.0f};
constexpr float kVoiceSpanHz = 220.0f;
constexpr float kHoldingBonusHz = 30.0f;
}  // namespace

void LivingIntervalPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  (void)difficulty;  // no difficulty selector exists yet -- see the tuning comment above

  solved_ = false;
  rewardDigit_ = static_cast<std::uint8_t>(random(8));
  lastPollMs_ = ctx.nowMs;

  for (std::uint8_t i = 0; i < kCount; ++i) {
    tolerance_[i] = kLivingTolerance;
    holdDurationMs_[i] = kEncoderHoldDurationMs[i];

    baseline_[i] = ctx.panel.encoder(i).value;
    amplitude_[i] = kLivingAmplitude;
    const float periodMin = kEncoderPeriodMinMs[i];
    const float periodMax = kEncoderPeriodMaxMs[i];
    periodMs_[i] = periodMin + (periodMax - periodMin) * (random(1000) / 1000.0f);
    phaseRad_[i] = (random(1000) / 1000.0f) * 2.0f * PI;
    holdTimerMs_[i] = 0;
    confirmed_[i] = false;
    lastButtonPressed_[i] = ctx.panel.encoder(i).buttonPressed;
    ctx.panel.setEncoderLed(i, 255, 0, 0);
  }
}

float LivingIntervalPuzzle::liveTarget(std::uint8_t index, std::uint32_t elapsedMs) const {
  const float angle = (2.0f * PI * static_cast<float>(elapsedMs) / periodMs_[index]) + phaseRad_[index];
  return amplitude_[index] * sinf(angle);
}

float LivingIntervalPuzzle::holdProgress(std::uint8_t index) const {
  if (confirmed_[index]) return 1.0f;
  if (holdDurationMs_[index] == 0) return 0.0f;
  const float p = static_cast<float>(holdTimerMs_[index]) / static_cast<float>(holdDurationMs_[index]);
  return p > 1.0f ? 1.0f : p;
}

void LivingIntervalPuzzle::poll(puzzles::PuzzleContext& ctx) {
  if (solved_) {
    return;
  }

  const std::uint32_t deltaMs = ctx.nowMs - lastPollMs_;
  lastPollMs_ = ctx.nowMs;

  InputPanelService& panel = ctx.panel;
  bool allHeld = true;

  for (std::uint8_t i = 0; i < kCount; ++i) {
    const float target = liveTarget(i, ctx.elapsedMs);
    const float rel = static_cast<float>(panel.encoder(i).value - baseline_[i]);
    const float distance = fabsf(target - rel);
    const bool inTolerance = distance <= static_cast<float>(tolerance_[i]);

    if (!confirmed_[i]) {
      if (inTolerance) {
        holdTimerMs_[i] += deltaMs;
        if (holdTimerMs_[i] >= holdDurationMs_[i]) {
          confirmed_[i] = true;
        }
      } else {
        holdTimerMs_[i] = 0;
      }
    }
    if (!confirmed_[i]) {
      allHeld = false;
    }

    const float proximity = 1.0f - min(distance, static_cast<float>(kVisibleRange)) / kVisibleRange;
    if (confirmed_[i]) {
      // Sticky visual confirmation -- stays lit even if the live position
      // has since drifted away, so the player gets clear feedback that
      // this encoder is done and can move on to the next one.
      panel.setEncoderLed(i, 0, 255, 0);
    } else {
      const std::uint8_t red = static_cast<std::uint8_t>(255 * (1.0f - proximity));
      const std::uint8_t green = static_cast<std::uint8_t>(255 * proximity);
      panel.setEncoderLed(i, red, green, 0);
    }

    float voiceHz = kVoiceBaseHz[i] + proximity * kVoiceSpanHz;
    if (inTolerance) {
      voiceHz += kHoldingBonusHz;
    }
    ctx.audio.holdToneVoice(i, voiceHz);
  }

  std::uint8_t confirmedMask = 0;
  for (std::uint8_t i = 0; i < kCount; ++i) {
    if (confirmed_[i]) confirmedMask |= static_cast<std::uint8_t>(1u << i);
  }
  panel.renderSpectralTuner(confirmedMask, kCount);

  if (!allHeld) {
    for (std::uint8_t i = 0; i < kCount; ++i) {
      lastButtonPressed_[i] = panel.encoder(i).buttonPressed;
    }
    return;
  }

  for (std::uint8_t i = 0; i < kCount; ++i) {
    const bool pressed = panel.encoder(i).buttonPressed;
    if (pressed && !lastButtonPressed_[i]) {
      solved_ = true;
      for (std::uint8_t v = 0; v < kCount; ++v) {
        ctx.audio.stopToneVoice(v);
      }
      ctx.audio.playCue(protocol::AudioCueId::Success);
    }
    lastButtonPressed_[i] = pressed;
  }
}
