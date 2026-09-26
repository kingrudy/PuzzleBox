#pragma once

#include <array>
#include <cstdint>

#include "devices/audio_cue_bus.h"
#include "devices/input_panel_service.h"

// Simon-style playback/repeat engine: light+tone each step of a sequence in
// turn, then require the same sequence back via TM1638 buttons. Shared by
// PatternPuzzle (stage 1) and FinalePuzzle (stage 7) — see
// docs/puzzles/puzzle_07.md's explicit note that the finale should reuse
// stage 1's implementation rather than duplicate it.
//
// All TM1638 buttons S1-S8 (indices 0-7) are used as game input, so every
// sequence value must be in [0, 8), an 8-symbol alphabet.
class SequenceRecallEngine {
 public:
  static constexpr std::uint8_t kMaxLength = 8;
  static constexpr std::uint8_t kAlphabetSize = 8;

  // playbackPhaseLabel must be a string literal (or otherwise outlive this
  // call) — it's stored by pointer, not copied.
  void reset(const std::uint8_t* sequence, std::uint8_t length, std::uint16_t onMs,
             std::uint16_t offMs, std::uint32_t errorPenaltyMs, const char* playbackPhaseLabel);

  void poll(InputPanelService& panel, AudioCueBus& audio, std::uint32_t nowMs);

  bool isSolved() const { return phase_ == Phase::kDone; }

  // Returns accumulated time-penalty (ms) owed to the room clock since the
  // last call, then clears it. AppController deducts this from
  // remainingSeconds_ each poll.
  std::uint32_t takePenaltyMs();

 private:
  enum class Phase { kPlayback, kInput, kError, kDone };

  std::array<std::uint8_t, kMaxLength> sequence_{};
  std::uint8_t length_ = 0;
  std::uint16_t onMs_ = 500;
  std::uint16_t offMs_ = 250;
  std::uint32_t errorPenaltyMs_ = 15000;
  const char* playbackPhaseLabel_ = "PLAY";

  Phase phase_ = Phase::kPlayback;
  std::uint8_t stepIndex_ = 0;
  std::uint8_t inputIndex_ = 0;
  std::uint32_t phaseStartMs_ = 0;
  bool lastLedOn_ = false;
  std::uint32_t pendingPenaltyMs_ = 0;

  static constexpr std::uint16_t kErrorFlashMs = 600;
};
