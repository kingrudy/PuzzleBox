#pragma once

#include <array>
#include <cstdint>

#include "puzzles/puzzle.h"

// Stage 6 — Reactoroverbelasting (Tetris). See docs/puzzles/puzzle_06.md.
//
// 10x16 board. Pieces fall on their own on a fixed timer (dropIntervalMs_) —
// an earlier version had no gravity at all and required manually soft-
// dropping every single row, which real playtesting found tedious rather
// than relaxed (see spec/Specifications.md). S1/S3 move left/right, S2
// rotates, S4 soft-drops one row early (+1 pt) without waiting for the
// timer, S5 hard-drops instantly (+2 pts/row), matching the button mapping
// already documented in spec/puzzlebox_hw.md section 4.4. S6 pauses (with a
// 1200 ms guard after the puzzle starts, so the mode-switch bounce can't
// accidentally pause immediately) — pausing also suspends the fall timer.
// Rotation is a plain 90-degree transform within each piece's 4x4 bounding
// box — no wall kicks.
//
// Win condition is a target number of cleared lines (targetLines_), not a
// derived "level" -- simpler to reason about and to tune directly.
//
// Deliberate simplification vs. spec/puzzlebox_hw.md's original
// "kDisplayOwnsTetris" framing: board/piece logic lives here on the main
// controller (where TM1638 input is actually read), and the display is a
// pure renderer polling this puzzle's state. Splitting real-time input
// handling across the ~750ms HTTP poll link the display uses would make the
// game feel laggy; keeping one authoritative source of truth avoids that at
// the cost of diverging from the doc's original split.
//
// No hard-fail condition: if a spawning piece has nowhere to go (topped
// out), that's played as a "reactor overload" — the board clears and a
// time penalty is deducted from the shared room clock via takePenaltyMs()
// — rather than ending the puzzle. (An earlier version drained a cosmetic
// "stability" meter instead, which had no actual consequence once empty;
// real playtesting flagged that as pointless. See spec/Specifications.md.)
class TetrisPuzzle : public puzzles::Puzzle {
 public:
  static constexpr std::uint8_t kWidth = 10;
  static constexpr std::uint8_t kHeight = 16;

  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return solved_; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::Tetris; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }
  std::uint32_t takePenaltyMs() override;

  // Display-facing state.
  std::uint8_t cellAt(std::uint8_t row, std::uint8_t col) const { return board_[row][col]; }
  bool activeCellAt(std::uint8_t row, std::uint8_t col) const;
  std::uint8_t linesCleared() const { return linesCleared_; }
  std::uint8_t targetLines() const { return targetLines_; }
  bool paused() const { return paused_; }

 private:
  struct Cell {
    std::int8_t row;
    std::int8_t col;
  };

  void spawnPiece(std::uint32_t nowMs);
  bool collides(const std::array<Cell, 4>& cells, std::int8_t row, std::int8_t col) const;
  void lockPiece(puzzles::PuzzleContext& ctx);
  void clearCompletedRows(puzzles::PuzzleContext& ctx);
  void overload(puzzles::PuzzleContext& ctx);
  void rotateActivePiece();

  std::array<std::array<std::uint8_t, kWidth>, kHeight> board_{};

  std::array<Cell, 4> activeCells_{};
  std::uint8_t activeType_ = 0;
  std::int8_t pieceRow_ = 0;
  std::int8_t pieceCol_ = 0;

  std::uint8_t linesCleared_ = 0;
  std::uint8_t targetLines_ = 10;
  std::uint32_t score_ = 0;
  std::uint32_t overloadPenaltyMs_ = 8000;
  std::uint32_t pendingPenaltyMs_ = 0;

  // Auto-fall: the active piece drops one row whenever nowMs passes
  // nextDropMs_, independent of button presses. S4 (soft drop) still lets
  // the player speed a single row up early; S5 (hard drop) is unaffected.
  std::uint32_t dropIntervalMs_ = 1000;
  std::uint32_t nextDropMs_ = 0;

  bool paused_ = false;
  std::uint32_t puzzleStartMs_ = 0;
  bool solved_ = false;
  std::uint8_t rewardDigit_ = 0;
};
