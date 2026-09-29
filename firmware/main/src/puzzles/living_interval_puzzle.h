#pragma once

#include <array>
#include <cstdint>

#include "devices/encoder_hardware_config.h"
#include "puzzles/puzzle.h"

// Stage 5 — Living Interval. See docs/puzzles/puzzle_05.md.
//
// Reuses the same 3 encoders as SpectralTuner (stage 4), but the target now
// drifts continuously as a function of elapsed time since begin() — no RTC
// hardware needed, just PuzzleContext::elapsedMs — and each encoder must be
// held within tolerance for a sustained duration, not just touched once.
class LivingIntervalPuzzle : public puzzles::Puzzle {
 public:
  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return solved_; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::LivingInterval; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }

  // For the display status endpoint: 0..1 hold progress per encoder. Once an
  // encoder has been confirmed (see confirmed_ below), this stays 1.0 even
  // if the live position has since drifted out of tolerance.
  float holdProgress(std::uint8_t index) const;
  bool holding(std::uint8_t index) const { return holdTimerMs_[index] > 0; }

 private:
  static constexpr std::uint8_t kCount = encoder_hw::kEncoderCount;

  float liveTarget(std::uint8_t index, std::uint32_t elapsedMs) const;

  std::array<std::int32_t, kCount> baseline_{};
  std::array<float, kCount> amplitude_{};
  std::array<float, kCount> periodMs_{};
  std::array<float, kCount> phaseRad_{};
  std::array<std::uint32_t, kCount> holdTimerMs_{};
  std::array<bool, kCount> lastButtonPressed_{};
  // Sticky per-encoder completion: once an encoder finishes its hold, it
  // stays confirmed even after drifting back out of tolerance, so the
  // three encoders can be solved one at a time instead of needing all
  // three simultaneously in-tolerance (was found to be near-impossible
  // solo at Medium tolerance/hold settings -- see spec/Specifications.md).
  std::array<bool, kCount> confirmed_{};

  // Per-encoder, not per-run: encoder 1 is the easiest, encoder 3 the
  // hardest (see kEncoderToleranceFactor etc. in the .cpp), so difficulty
  // ramps up across the three instead of all three being identical.
  std::array<std::int32_t, kCount> tolerance_{};
  std::array<std::uint32_t, kCount> holdDurationMs_{};
  std::uint32_t lastPollMs_ = 0;
  bool solved_ = false;
  std::uint8_t rewardDigit_ = 0;
};
