#pragma once

#include <array>
#include <cstdint>

#include "puzzles/puzzle.h"
#include "puzzles/sequence_recall.h"

// Stage 7 — Eindsequentie (Finale). See docs/puzzles/puzzle_07.md.
//
// Unlike every other puzzle, Finale isn't part of the shuffled 6-puzzle
// order — it depends on having a reward digit from each of the other 6
// stages, so it always runs last. AppController calls setCollectedDigits()
// before begin() rather than shuffling this into the random-order list.
class FinalePuzzle : public puzzles::Puzzle {
 public:
  static constexpr std::uint8_t kDigitCount = 6;

  void setCollectedDigits(const std::array<std::uint8_t, kDigitCount>& digits) {
    digits_ = digits;
  }

  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return engine_.isSolved(); }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::Finale; }
  std::uint8_t rewardDigit() const override { return 0; }  // nothing consumes this — Finale is last
  std::uint32_t takePenaltyMs() override { return engine_.takePenaltyMs(); }

 private:
  SequenceRecallEngine engine_;
  std::array<std::uint8_t, kDigitCount> digits_{};
};
