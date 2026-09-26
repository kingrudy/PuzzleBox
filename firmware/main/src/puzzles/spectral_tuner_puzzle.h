#pragma once

#include <array>
#include <cstdint>

#include "devices/encoder_hardware_config.h"
#include "devices/input_panel_service.h"
#include "puzzles/puzzle.h"

// Stage 4 — Spectraalresonantie (SpectralTuner). See docs/puzzles/puzzle_04.md.
//
// Each of the three rotary encoders has a hidden target position, set
// relative to wherever the encoder happened to be at begin() (encoder
// values are a free-running accumulator with no absolute zero — see
// EncoderState::value). The encoder's own LED slides blue -> amber -> green
// as it nears its target. All three must be within tolerance
// *simultaneously* to count as locked; letting a previously-locked encoder
// drift back out jitters the target of the other encoders that were still
// holding lock, so the last stretch is a genuine three-way balancing act
// rather than three independent dials. Confirm by pressing any encoder's
// button while all three are locked.
class SpectralTunerPuzzle : public puzzles::Puzzle {
 public:
  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;

  bool isSolved() const override { return solved_; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::SpectralTuner; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }

  bool allLocked() const;
  std::uint8_t lockedMask() const;  // bit i set = encoder i currently locked
  std::uint8_t encoderCount() const { return kCount; }

  // 0 (far) .. 1 (locked), same value the LED colour is derived from.
  // Exposed for the web status API / the big-screen visualizer.
  float proximity(std::uint8_t index) const { return proximity_[index]; }
  bool locked(std::uint8_t index) const { return locked_[index]; }

 private:
  static constexpr std::uint8_t kCount = encoder_hw::kEncoderCount;

  struct LedColor {
    std::uint8_t r;
    std::uint8_t g;
    std::uint8_t b;
  };

  static LedColor colorForProximity(float proximity01);
  static std::int32_t randomTargetOffset();

  std::int32_t relativePosition(const InputPanelService& panel, std::uint8_t index) const;
  void jitterOtherLockedTargets(std::uint8_t exceptIndex);

  std::array<std::int32_t, kCount> baseline_{};
  std::array<std::int32_t, kCount> targetOffset_{};
  std::array<bool, kCount> locked_{};
  std::array<bool, kCount> lastButtonPressed_{};
  std::array<float, kCount> proximity_{};

  std::int32_t tolerance_ = 2;
  bool solved_ = false;
  std::uint8_t rewardDigit_ = 0;
};
