#include "puzzles/pattern_puzzle.h"

#include <Arduino.h>

namespace {
// Sequence length tier: Short/Normal/Long -> 3/4/6 (docs/puzzles/puzzle_01.md).
constexpr std::uint8_t kLengthByDifficulty[3] = {3, 4, 6};
// Playback speed tier: Easy/Medium/Hard -> {on, off} ms.
constexpr std::uint16_t kOnMsByDifficulty[3] = {780, 520, 320};
constexpr std::uint16_t kOffMsByDifficulty[3] = {340, 220, 140};
constexpr std::uint32_t kErrorPenaltyMs = 15000;
}  // namespace

void PatternPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  const std::uint8_t tier = static_cast<std::uint8_t>(difficulty);
  const std::uint8_t length = kLengthByDifficulty[tier];

  std::uint8_t sequence[SequenceRecallEngine::kMaxLength];
  for (std::uint8_t i = 0; i < length; ++i) {
    sequence[i] = static_cast<std::uint8_t>(random(SequenceRecallEngine::kAlphabetSize));
  }
  rewardDigit_ = static_cast<std::uint8_t>(random(SequenceRecallEngine::kAlphabetSize));

  engine_.reset(sequence, length, kOnMsByDifficulty[tier], kOffMsByDifficulty[tier],
                kErrorPenaltyMs, "PLAY");

  (void)ctx;
}

void PatternPuzzle::poll(puzzles::PuzzleContext& ctx) {
  engine_.poll(ctx.panel, ctx.audio, ctx.nowMs);
}
