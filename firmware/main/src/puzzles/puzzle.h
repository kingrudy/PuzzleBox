#pragma once

#include <cstdint>

#include "devices/audio_cue_bus.h"
#include "devices/input_panel_service.h"
#include "devices/vibration_service.h"
#include "protocol/game_state.h"

namespace puzzles {

enum class Difficulty : std::uint8_t { kEasy, kMedium, kHard };

// A touch event relayed from the display over HTTP (see AppController's
// POST /api/touch handler). AppController fills this in fresh before each
// poll() and a puzzle consumes it by reading `pending` — there is no queue,
// since touches arrive far slower than the poll loop runs.
struct TouchEvent {
  bool pending = false;
  std::uint8_t cellIndex = 0;
};

// Bundles every shared service a puzzle might need. Puzzles that don't use a
// given member (e.g. Pattern never touches `touch`) simply ignore it — this
// is a read/write view, not a capability restriction.
struct PuzzleContext {
  InputPanelService& panel;
  VibrationService& vibration;
  AudioCueBus& audio;
  TouchEvent touch;
  std::uint32_t nowMs = 0;      // millis() snapshot, same value for the whole tick
  std::uint32_t elapsedMs = 0;  // ms since this puzzle's begin()
};

// Common shape every one of the 7 stages implements, so AppController's
// sequencer (docs/puzzles/plan.md section 3, item 4) can drive whichever
// puzzle is current without a per-puzzle-type switch. See
// docs/puzzles/plan.md for the full design and each docs/puzzles/puzzle_0N.md
// for the stage this class implements.
class Puzzle {
 public:
  virtual ~Puzzle() = default;

  virtual void begin(PuzzleContext& ctx, Difficulty difficulty) = 0;
  virtual void poll(PuzzleContext& ctx) = 0;
  virtual bool isSolved() const = 0;
  virtual protocol::PuzzleId id() const = 0;

  // Time (ms) this puzzle wants deducted from the shared room clock since the
  // last call — mistakes cost time, not attempts, per every docs/puzzles/*.md
  // spec. Default: no penalty mechanism. AppController calls this every tick
  // and subtracts the result from remainingSeconds_.
  virtual std::uint32_t takePenaltyMs() { return 0; }

  // The single digit (0-9) this puzzle contributes to the Finale's recap —
  // see docs/puzzles/puzzle_07.md. Only meaningful once isSolved() is true;
  // deterministic from whatever the puzzle privately randomized at begin(),
  // not from a separate seed, so a solved puzzle doesn't need extra state.
  virtual std::uint8_t rewardDigit() const = 0;
};

}  // namespace puzzles
