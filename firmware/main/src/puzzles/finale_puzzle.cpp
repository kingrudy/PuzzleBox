#include "puzzles/finale_puzzle.h"

namespace {
// The most generous timing in the game — this is the payoff, not a skill
// gate. Matches stage 1's Easy tier exactly (docs/puzzles/puzzle_07.md).
constexpr std::uint16_t kOnMs = 780;
constexpr std::uint16_t kOffMs = 340;
constexpr std::uint32_t kErrorPenaltyMs = 5000;
}  // namespace

void FinalePuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  engine_.reset(digits_.data(), kDigitCount, kOnMs, kOffMs, kErrorPenaltyMs, "PLAY");
  (void)ctx;
  (void)difficulty;
}

void FinalePuzzle::poll(puzzles::PuzzleContext& ctx) {
  const bool wasSolved = engine_.isSolved();
  engine_.poll(ctx.panel, ctx.audio, ctx.nowMs);
  if (!wasSolved && engine_.isSolved()) {
    ctx.audio.playCue(protocol::AudioCueId::Endgame);
    ctx.vibration.pulse(420);
  }
}
