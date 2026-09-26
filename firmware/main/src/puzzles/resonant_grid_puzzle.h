#pragma once

#include <array>
#include <cstdint>

#include "puzzles/puzzle.h"

// Stage 2 — Resonant Grid. See docs/puzzles/puzzle_02.md.
//
// Touch-only: the display owns the grid's pixel layout and reports which
// cell (row-major index) was touched via AppController's POST /api/touch,
// which lands in PuzzleContext::touch. This class never sees pixel
// coordinates, only cell indices, and exposes just enough state (grid size,
// locked mask, cursor) for the display to render — see AppController's
// /api/game handler.
class ResonantGridPuzzle : public puzzles::Puzzle {
 public:
  static constexpr std::uint8_t kMaxGridSize = 4;  // 4x4 = 16 cells, fits a uint16_t mask

  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return cursor_ >= totalCells_; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::ResonantGrid; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }
  std::uint32_t takePenaltyMs() override;

  // For the display-facing status endpoint.
  std::uint8_t gridSize() const { return gridSize_; }
  std::uint8_t totalCells() const { return totalCells_; }
  std::uint8_t cursor() const { return cursor_; }
  std::uint16_t lockedMask() const { return lockedMask_; }

 private:
  static constexpr std::uint8_t kMaxCells = kMaxGridSize * kMaxGridSize;

  std::array<std::uint8_t, kMaxCells> order_{};
  std::array<std::uint8_t, kMaxCells> touchCount_{};
  std::uint8_t gridSize_ = 2;
  std::uint8_t totalCells_ = 4;
  std::uint8_t cursor_ = 0;
  std::uint16_t lockedMask_ = 0;
  std::uint8_t rewardDigit_ = 0;

  std::uint32_t repeatTouchThreshold_ = 2;  // touches of a wrong cell before it starts costing time
  std::uint32_t repeatPenaltyMs_ = 5000;
  std::uint32_t pendingPenaltyMs_ = 0;
};
