#include "puzzles/living_interval_puzzle.h"

#include <Arduino.h>
#include <cmath>

#include "protocol/audio_cue.h"

namespace {
constexpr float kPeriodMinMsByDifficulty[3] = {12000.0f, 7000.0f, 3000.0f};
constexpr float kPeriodMaxMsByDifficulty[3] = {18000.0f, 11000.0f, 6000.0f};
constexpr float kAmplitudeByDifficulty[3] = {15.0f, 25.0f, 35.0f};
constexpr std::int32_t kToleranceByDifficulty[3] = {4, 2, 1};
constexpr std::uint32_t kHoldDurationMsByDifficulty[3] = {1000, 1500, 2000};

constexpr std::int32_t kVisibleRange = 20;
constexpr float kVoiceBaseHz[3] = {180.0f, 260.0f, 360.0f};
constexpr float kVoiceSpanHz = 220.0f;
constexpr float kHoldingBonusHz = 30.0f;
}  // namespace

void LivingIntervalPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  const std::uint8_t tier = static_cast<std::uint8_t>(difficulty);
  tolerance_ = kToleranceByDifficulty[tier];
  holdDurationMs_ = kHoldDurationMsByDifficulty[tier];

  solved_ = false;
  rewardDigit_ = static_cast<std::uint8_t>(random(8));
  lastPollMs_ = ctx.nowMs;

  for (std::uint8_t i = 0; i < kCount; ++i) {
    baseline_[i] = ctx.panel.encoder(i).value;
    amplitude_[i] = kAmplitudeByDifficulty[tier];
    const float periodMin = kPeriodMinMsByDifficulty[tier];
    const float periodMax = kPeriodMaxMsByDifficulty[tier];
    periodMs_[i] = periodMin + (periodMax - periodMin) * (random(1000) / 1000.0f);
    phaseRad_[i] = (random(1000) / 1000.0f) * 2.0f * PI;
    holdTimerMs_[i] = 0;
    lastButtonPressed_[i] = ctx.panel.encoder(i).buttonPressed;
    ctx.panel.setEncoderLed(i, 255, 0, 0);
  }
}

float LivingIntervalPuzzle::liveTarget(std::uint8_t index, std::uint32_t elapsedMs) const {
  const float angle = (2.0f * PI * static_cast<float>(elapsedMs) / periodMs_[index]) + phaseRad_[index];
  return amplitude_[index] * sinf(angle);
}

float LivingIntervalPuzzle::holdProgress(std::uint8_t index) const {
  if (holdDurationMs_ == 0) return 0.0f;
  const float p = static_cast<float>(holdTimerMs_[index]) / static_cast<float>(holdDurationMs_);
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
    const bool inTolerance = distance <= static_cast<float>(tolerance_);

    if (inTolerance) {
      holdTimerMs_[i] += deltaMs;
    } else {
      holdTimerMs_[i] = 0;
    }
    if (holdProgress(i) < 1.0f) {
      allHeld = false;
    }

    const float proximity = 1.0f - min(distance, static_cast<float>(kVisibleRange)) / kVisibleRange;
    const std::uint8_t red = static_cast<std::uint8_t>(255 * (1.0f - proximity));
    const std::uint8_t green = static_cast<std::uint8_t>(255 * proximity);
    panel.setEncoderLed(i, red, green, 0);

    float voiceHz = kVoiceBaseHz[i] + proximity * kVoiceSpanHz;
    if (inTolerance) {
      voiceHz += kHoldingBonusHz;
    }
    ctx.audio.holdToneVoice(i, voiceHz);
  }

  panel.renderSpectralTuner(allHeld ? 0x07 : 0, kCount);

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
