#include "puzzles/spectral_tuner_puzzle.h"

#include <Arduino.h>

#include "protocol/audio_cue.h"

namespace {
// Beyond this distance from the target, the LED stays fully "cold" blue —
// closing the last part of that gap is where the colour ramp lives.
constexpr std::int32_t kVisibleRange = 20;

// Per-encoder base pitch, so all three voices are distinguishable even at
// identical proximity — see docs/puzzles/puzzle_04.md's sound design.
constexpr float kVoiceBaseHz[3] = {180.0f, 260.0f, 360.0f};
constexpr float kVoiceSpanHz = 220.0f;  // far -> near sweep range, added to base
}  // namespace

void SpectralTunerPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  switch (difficulty) {
    case puzzles::Difficulty::kEasy: tolerance_ = 4; break;
    case puzzles::Difficulty::kMedium: tolerance_ = 2; break;
    case puzzles::Difficulty::kHard: tolerance_ = 1; break;
  }

  solved_ = false;
  rewardDigit_ = static_cast<std::uint8_t>(random(8));
  const LedColor cold = colorForProximity(0.0f);
  for (std::uint8_t i = 0; i < kCount; ++i) {
    baseline_[i] = ctx.panel.encoder(i).value;
    targetOffset_[i] = randomTargetOffset();
    locked_[i] = false;
    lastButtonPressed_[i] = ctx.panel.encoder(i).buttonPressed;
    ctx.panel.setEncoderLed(i, cold.r, cold.g, cold.b);
  }
}

void SpectralTunerPuzzle::poll(puzzles::PuzzleContext& ctx) {
  if (solved_) {
    return;
  }

  InputPanelService& panel = ctx.panel;

  for (std::uint8_t i = 0; i < kCount; ++i) {
    const std::int32_t rel = relativePosition(panel, i);
    const std::int32_t diff = targetOffset_[i] - rel;
    const std::int32_t distance = diff < 0 ? -diff : diff;
    const bool isLocked = distance <= tolerance_;

    float proximity = 1.0f - static_cast<float>(min(distance, kVisibleRange)) / kVisibleRange;
    if (isLocked) {
      proximity = 1.0f;
    }
    proximity_[i] = proximity;
    const LedColor color = colorForProximity(proximity);
    panel.setEncoderLed(i, color.r, color.g, color.b);
    ctx.audio.holdToneVoice(i, kVoiceBaseHz[i] + proximity * kVoiceSpanHz);

    if (locked_[i] && !isLocked) {
      // Just fell out of lock: re-desync the other fields that were still
      // holding, so the endgame can't be solved one dial at a time.
      jitterOtherLockedTargets(i);
    }
    locked_[i] = isLocked;
  }

  panel.renderSpectralTuner(lockedMask(), kCount);

  if (!allLocked()) {
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

bool SpectralTunerPuzzle::allLocked() const {
  for (bool locked : locked_) {
    if (!locked) return false;
  }
  return true;
}

std::uint8_t SpectralTunerPuzzle::lockedMask() const {
  std::uint8_t mask = 0;
  for (std::uint8_t i = 0; i < kCount; ++i) {
    if (locked_[i]) mask |= (1 << i);
  }
  return mask;
}

std::int32_t SpectralTunerPuzzle::relativePosition(const InputPanelService& panel,
                                                     std::uint8_t index) const {
  return panel.encoder(index).value - baseline_[index];
}

void SpectralTunerPuzzle::jitterOtherLockedTargets(std::uint8_t exceptIndex) {
  for (std::uint8_t i = 0; i < kCount; ++i) {
    if (i == exceptIndex || !locked_[i]) {
      continue;
    }
    const std::int32_t magnitude = random(2, 5);
    targetOffset_[i] += (random(0, 2) == 0) ? magnitude : -magnitude;
    locked_[i] = false;
  }
}

std::int32_t SpectralTunerPuzzle::randomTargetOffset() {
  const std::int32_t magnitude = random(6, 25);
  return (random(0, 2) == 0) ? magnitude : -magnitude;
}

SpectralTunerPuzzle::LedColor SpectralTunerPuzzle::colorForProximity(float proximity01) {
  proximity01 = proximity01 < 0.0f ? 0.0f : (proximity01 > 1.0f ? 1.0f : proximity01);

  // Red -> amber -> green, using only R/G. Encoders 1 and 2 have no blue
  // channel at all (InputPanelService::setEncoderLed silently drops it), so
  // a blue-based "cold" colour is invisible on 2 of the 3 encoders — it was
  // rendering as a flat dim green no matter how far off target they were.
  // Every encoder can do R/G, so the ramp is now visible everywhere.
  constexpr LedColor kCold{255, 0, 0};          // far: red
  constexpr LedColor kWarm{255, 170, 0};        // approaching: amber
  constexpr LedColor kLockedColor{0, 255, 0};   // locked: green

  auto lerp = [](std::uint8_t a, std::uint8_t b, float t) -> std::uint8_t {
    return static_cast<std::uint8_t>(a + (static_cast<float>(b) - a) * t);
  };

  if (proximity01 < 0.6f) {
    const float t = proximity01 / 0.6f;
    return {lerp(kCold.r, kWarm.r, t), lerp(kCold.g, kWarm.g, t), 0};
  }
  const float t = (proximity01 - 0.6f) / 0.4f;
  return {lerp(kWarm.r, kLockedColor.r, t), lerp(kWarm.g, kLockedColor.g, t), 0};
}
