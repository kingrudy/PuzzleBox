#pragma once

#include "puzzles/puzzle.h"
#include "puzzles/sequence_recall.h"

// Stage 1 — Energiepatroon (Pattern). See docs/puzzles/puzzle_01.md.
class PatternPuzzle : public puzzles::Puzzle {
 public:
  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return engine_.isSolved(); }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::Pattern; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }
  std::uint32_t takePenaltyMs() override { return engine_.takePenaltyMs(); }

 private:
  SequenceRecallEngine engine_;
  std::uint8_t rewardDigit_ = 0;
};
