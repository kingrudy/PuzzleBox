#pragma once

#include <array>
#include <cstdint>

#include "puzzles/puzzle.h"

// Stage 6 — Reactoroverbelasting (Tetris). See docs/puzzles/puzzle_06.md.
//
// 10x16 board. No gravity timer — pieces only move on S1/S3 (left/right),
// S2 (rotate), S4 (soft drop, 1 row + 1 pt, locks on collision), S5 (hard
// drop, N rows + 2 pts/row, always locks), matching the button mapping
// already documented in spec/puzzlebox_hw.md section 4.4. S6 pauses (with a
// 1200 ms guard after the puzzle starts, so the mode-switch bounce can't
// accidentally pause immediately). Rotation is a plain 90-degree transform
// within each piece's 4x4 bounding box — no wall kicks.
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
// out), that's played as a "reactor overload" — the board clears and
// stability takes a big hit — rather than ending the puzzle, consistent
// with docs/puzzles/puzzle_06.md's "no hard fail from stability alone."
class TetrisPuzzle : public puzzles::Puzzle {
 public:
  static constexpr std::uint8_t kWidth = 10;
  static constexpr std::uint8_t kHeight = 16;

  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return solved_; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::Tetris; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }

  // Display-facing state.
  std::uint8_t cellAt(std::uint8_t row, std::uint8_t col) const { return board_[row][col]; }
  bool activeCellAt(std::uint8_t row, std::uint8_t col) const;
  std::uint8_t level() const { return level_; }
  std::uint8_t targetLevel() const { return targetLevel_; }
  float stability01() const { return stability_; }
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
  std::uint8_t stabilityThresholdRow() const;
  std::uint8_t stackHeight() const;

  std::array<std::array<std::uint8_t, kWidth>, kHeight> board_{};

  std::array<Cell, 4> activeCells_{};
  std::uint8_t activeType_ = 0;
  std::int8_t pieceRow_ = 0;
  std::int8_t pieceCol_ = 0;

  std::uint8_t linesCleared_ = 0;
  std::uint8_t level_ = 1;
  std::uint8_t targetLevel_ = 3;
  std::uint32_t score_ = 0;
  float stability_ = 1.0f;
  std::uint8_t drainThresholdRow_ = 12;
  float drainRatePerLock_ = 0.03f;

  bool paused_ = false;
  std::uint32_t puzzleStartMs_ = 0;
  std::uint32_t nextHeartbeatMs_ = 0;
  bool solved_ = false;
  std::uint8_t rewardDigit_ = 0;
};
