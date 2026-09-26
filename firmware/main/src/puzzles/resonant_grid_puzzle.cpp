#include "puzzles/resonant_grid_puzzle.h"

#include <Arduino.h>

#include "protocol/audio_cue.h"

namespace {
constexpr std::uint8_t kGridSizeByDifficulty[3] = {2, 3, 4};
// Easy: no penalty at all (repeatTouchThreshold effectively infinite).
// Medium: 2nd touch of a wrong cell starts costing time.
// Hard: any repeat touch of a wrong cell costs time immediately.
constexpr std::uint32_t kRepeatThresholdByDifficulty[3] = {0xFFFFFFFFu, 2, 1};
constexpr std::uint32_t kRepeatPenaltyMs = 5000;
}  // namespace

void ResonantGridPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  const std::uint8_t tier = static_cast<std::uint8_t>(difficulty);
  gridSize_ = kGridSizeByDifficulty[tier];
  totalCells_ = static_cast<std::uint8_t>(gridSize_ * gridSize_);
  repeatTouchThreshold_ = kRepeatThresholdByDifficulty[tier];
  repeatPenaltyMs_ = kRepeatPenaltyMs;

  for (std::uint8_t i = 0; i < totalCells_; ++i) {
    order_[i] = i;
    touchCount_[i] = 0;
  }
  // Fisher-Yates shuffle -> the hidden correct order.
  for (std::uint8_t i = totalCells_; i > 1; --i) {
    const std::uint8_t j = static_cast<std::uint8_t>(random(i));
    std::swap(order_[i - 1], order_[j]);
  }

  cursor_ = 0;
  lockedMask_ = 0;
  pendingPenaltyMs_ = 0;
  rewardDigit_ = static_cast<std::uint8_t>(random(8));

  (void)ctx;
}

std::uint32_t ResonantGridPuzzle::takePenaltyMs() {
  const std::uint32_t owed = pendingPenaltyMs_;
  pendingPenaltyMs_ = 0;
  return owed;
}

void ResonantGridPuzzle::poll(puzzles::PuzzleContext& ctx) {
  if (isSolved()) {
    return;
  }

  if (ctx.touch.pending) {
    ctx.touch.pending = false;
    const std::uint8_t cell = ctx.touch.cellIndex;
    if (cell < totalCells_) {
      const std::uint8_t target = order_[cursor_];
      if (cell == target) {
        lockedMask_ |= static_cast<std::uint16_t>(1u << cell);
        ++cursor_;
        ctx.audio.playTone(220.0f + 40.0f * cursor_, 200);
        if (cursor_ >= totalCells_) {
          ctx.audio.playCue(protocol::AudioCueId::Success);
        }
      } else {
        ++touchCount_[cell];
        if (touchCount_[cell] >= repeatTouchThreshold_) {
          pendingPenaltyMs_ += repeatPenaltyMs_;
          ctx.audio.playCue(protocol::AudioCueId::Error);
        }
        // Manhattan distance in grid coordinates -> warm/cold pitch hint.
        // Close = high pitch, far = low pitch.
        const int cellRow = cell / gridSize_;
        const int cellCol = cell % gridSize_;
        const int targetRow = target / gridSize_;
        const int targetCol = target % gridSize_;
        const int distance = abs(cellRow - targetRow) + abs(cellCol - targetCol);
        const int maxDistance = (gridSize_ - 1) * 2;
        const float closeness01 =
            maxDistance > 0 ? 1.0f - (static_cast<float>(distance) / maxDistance) : 1.0f;
        ctx.audio.playTone(180.0f + closeness01 * 520.0f, 350);
      }
    }
  }

  ctx.panel.renderPatternPuzzle("GRID", 0, cursor_, totalCells_, false);
}
