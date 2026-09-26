#include "puzzles/tetris_puzzle.h"

#include <Arduino.h>

#include "protocol/audio_cue.h"

namespace {
// Base shapes in a 4x4 box, spawn orientation. Index = piece type (0-6).
using CellArr = std::array<std::int8_t, 8>;  // {r0,c0, r1,c1, r2,c2, r3,c3}
constexpr CellArr kBaseShapes[7] = {
    {1, 0, 1, 1, 1, 2, 1, 3},  // I
    {1, 1, 1, 2, 2, 1, 2, 2},  // O
    {1, 1, 2, 0, 2, 1, 2, 2},  // T
    {1, 1, 1, 2, 2, 0, 2, 1},  // S
    {1, 0, 1, 1, 2, 1, 2, 2},  // Z
    {1, 0, 2, 0, 2, 1, 2, 2},  // J
    {1, 2, 2, 0, 2, 1, 2, 2},  // L
};

constexpr std::uint8_t kTargetLevelByDifficulty[3] = {3, 5, 7};
constexpr std::uint8_t kStackHeightThresholdByDifficulty[3] = {12, 10, 8};
constexpr float kDrainPerLockByDifficulty[3] = {0.02f, 0.035f, 0.05f};

constexpr std::uint8_t kSpawnCol = 3;
}  // namespace

void TetrisPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  const std::uint8_t tier = static_cast<std::uint8_t>(difficulty);
  targetLevel_ = kTargetLevelByDifficulty[tier];
  drainThresholdRow_ = kStackHeightThresholdByDifficulty[tier];
  drainRatePerLock_ = kDrainPerLockByDifficulty[tier];

  board_ = {};
  linesCleared_ = 0;
  level_ = 1;
  score_ = 0;
  stability_ = 1.0f;
  paused_ = false;
  solved_ = false;
  puzzleStartMs_ = ctx.nowMs;
  nextHeartbeatMs_ = ctx.nowMs;
  rewardDigit_ = static_cast<std::uint8_t>(random(8));

  spawnPiece(ctx.nowMs);
}

bool TetrisPuzzle::activeCellAt(std::uint8_t row, std::uint8_t col) const {
  for (const Cell& cell : activeCells_) {
    if (pieceRow_ + cell.row == row && pieceCol_ + cell.col == col) {
      return true;
    }
  }
  return false;
}

void TetrisPuzzle::spawnPiece(std::uint32_t nowMs) {
  (void)nowMs;
  activeType_ = static_cast<std::uint8_t>(random(7));
  const CellArr& shape = kBaseShapes[activeType_];
  for (std::uint8_t i = 0; i < 4; ++i) {
    activeCells_[i] = Cell{shape[i * 2], shape[i * 2 + 1]};
  }
  pieceRow_ = 0;
  pieceCol_ = kSpawnCol;
}

bool TetrisPuzzle::collides(const std::array<Cell, 4>& cells, std::int8_t row,
                             std::int8_t col) const {
  for (const Cell& cell : cells) {
    const int wr = row + cell.row;
    const int wc = col + cell.col;
    if (wc < 0 || wc >= kWidth) return true;
    if (wr >= kHeight) return true;
    if (wr >= 0 && board_[wr][wc] != 0) return true;
  }
  return false;
}

void TetrisPuzzle::rotateActivePiece() {
  std::array<Cell, 4> rotated{};
  for (std::uint8_t i = 0; i < 4; ++i) {
    // 90-degree clockwise rotation within a 4x4 box: (r,c) -> (c, 3-r).
    rotated[i] = Cell{activeCells_[i].col, static_cast<std::int8_t>(3 - activeCells_[i].row)};
  }
  if (!collides(rotated, pieceRow_, pieceCol_)) {
    activeCells_ = rotated;
  }
}

std::uint8_t TetrisPuzzle::stackHeight() const {
  for (std::uint8_t r = 0; r < kHeight; ++r) {
    for (std::uint8_t c = 0; c < kWidth; ++c) {
      if (board_[r][c] != 0) {
        return static_cast<std::uint8_t>(kHeight - r);
      }
    }
  }
  return 0;
}

void TetrisPuzzle::lockPiece(puzzles::PuzzleContext& ctx) {
  for (const Cell& cell : activeCells_) {
    const int wr = pieceRow_ + cell.row;
    const int wc = pieceCol_ + cell.col;
    if (wr >= 0 && wr < kHeight && wc >= 0 && wc < kWidth) {
      board_[wr][wc] = static_cast<std::uint8_t>(activeType_ + 1);
    }
  }

  if (stackHeight() >= drainThresholdRow_) {
    stability_ -= drainRatePerLock_;
    if (stability_ < 0.0f) stability_ = 0.0f;
  }

  clearCompletedRows(ctx);

  if (level_ >= targetLevel_) {
    solved_ = true;
    ctx.audio.playCue(protocol::AudioCueId::Success);
    return;
  }

  spawnPiece(ctx.nowMs);
  if (collides(activeCells_, pieceRow_, pieceCol_)) {
    overload(ctx);
  }
}

void TetrisPuzzle::clearCompletedRows(puzzles::PuzzleContext& ctx) {
  decltype(board_) newBoard{};
  std::uint8_t writeRow = kHeight - 1;
  std::uint8_t clearedCount = 0;

  for (int r = kHeight - 1; r >= 0; --r) {
    bool full = true;
    for (std::uint8_t c = 0; c < kWidth; ++c) {
      if (board_[r][c] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      ++clearedCount;
      continue;
    }
    newBoard[writeRow] = board_[r];
    if (writeRow > 0) --writeRow;
  }
  board_ = newBoard;

  if (clearedCount > 0) {
    linesCleared_ = static_cast<std::uint8_t>(linesCleared_ + clearedCount);
    level_ = static_cast<std::uint8_t>(1 + linesCleared_ / 10);
    stability_ += 0.08f * clearedCount;
    if (stability_ > 1.0f) stability_ = 1.0f;
    ctx.audio.playTone(660.0f, 180);
  }
}

void TetrisPuzzle::overload(puzzles::PuzzleContext& ctx) {
  board_ = {};
  stability_ -= 0.4f;
  if (stability_ < 0.0f) stability_ = 0.0f;
  ctx.audio.playCue(protocol::AudioCueId::Error);
  ctx.vibration.pulse(300);
  spawnPiece(ctx.nowMs);
}

void TetrisPuzzle::poll(puzzles::PuzzleContext& ctx) {
  if (solved_) {
    return;
  }

  InputPanelService& panel = ctx.panel;

  std::uint8_t btn;
  while (panel.takeLedKeyButtonPress(btn)) {
    if (btn == 5) {  // S6 pause, guarded for 1200ms after this puzzle started
      if (ctx.nowMs - puzzleStartMs_ >= 1200) {
        paused_ = !paused_;
      }
      continue;
    }
    if (paused_) {
      continue;
    }
    switch (btn) {
      case 0:  // S1 left
        if (!collides(activeCells_, pieceRow_, static_cast<std::int8_t>(pieceCol_ - 1))) {
          --pieceCol_;
        }
        break;
      case 1:  // S2 rotate
        rotateActivePiece();
        break;
      case 2:  // S3 right
        if (!collides(activeCells_, pieceRow_, static_cast<std::int8_t>(pieceCol_ + 1))) {
          ++pieceCol_;
        }
        break;
      case 3:  // S4 soft drop
        if (!collides(activeCells_, static_cast<std::int8_t>(pieceRow_ + 1), pieceCol_)) {
          ++pieceRow_;
          ++score_;
        } else {
          lockPiece(ctx);
        }
        break;
      case 4: {  // S5 hard drop
        std::uint8_t rows = 0;
        while (!collides(activeCells_, static_cast<std::int8_t>(pieceRow_ + 1), pieceCol_)) {
          ++pieceRow_;
          ++rows;
        }
        score_ += rows * 2;
        lockPiece(ctx);
        break;
      }
      default:
        break;
    }
    if (solved_) {
      break;
    }
  }

  if (solved_) {
    return;
  }

  // Ambient stability tone: pitch rises as stability falls (read pitch
  // without looking, per docs/puzzles/puzzle_06.md's sound design).
  ctx.audio.holdTone(120.0f + (1.0f - stability_) * 500.0f);

  // Heartbeat vibration below the tension threshold, quickening as
  // stability keeps dropping.
  if (stability_ < 0.4f && !paused_) {
    if (ctx.nowMs >= nextHeartbeatMs_) {
      const std::uint32_t period = 150 + static_cast<std::uint32_t>(stability_ * 1875.0f);
      ctx.vibration.pulse(80);
      nextHeartbeatMs_ = ctx.nowMs + period;
    }
  }

  const char* phase = paused_ ? "PAUS" : "PLAY";
  panel.renderTetrisPuzzle(phase, level_, targetLevel_, paused_ ? 0 : 0x3F, !paused_);
}
