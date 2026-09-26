#pragma once

#include <array>
#include <cstdint>

#include "puzzles/puzzle.h"

// Stage 3 — Vibrational Cipher. See docs/puzzles/puzzle_03.md.
//
// Implementation note: rather than true dot/dash Morse timing (hard to feel
// reliably through a vibration motor bolted to a box), each digit (0-7, the
// same 8-symbol alphabet every TM1638-entry puzzle uses — see
// sequence_recall.h) is transmitted as (value + 1) short counted pulses.
// Counting distinct pulses through touch is far more robust than judging
// pulse *duration*, while keeping the docs' core idea intact: silence marks
// the start of a transmission, and the code is only ever felt, never shown.
class VibrationalCipherPuzzle : public puzzles::Puzzle {
 public:
  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return phase_ == Phase::kDone; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::VibrationalCipher; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }
  std::uint32_t takePenaltyMs() override;

 private:
  enum class Phase { kStandby, kTransmitOn, kTransmitGap, kDigitGap, kInput, kError, kDone };

  void beginTransmission(std::uint32_t nowMs, bool isRetry);

  static constexpr std::uint8_t kMaxDigits = 4;
  std::array<std::uint8_t, kMaxDigits> digits_{};
  std::uint8_t digitCount_ = 3;

  std::uint16_t pulseOnMs_ = 150;
  std::uint16_t pulseGapMs_ = 180;
  std::uint16_t digitGapMs_ = 550;
  std::uint32_t standbyMinMs_ = 1000;
  std::uint32_t standbyMaxMs_ = 3000;
  std::uint32_t retransmitPenaltyMs_ = 8000;

  Phase phase_ = Phase::kStandby;
  std::uint32_t phaseStartMs_ = 0;
  std::uint32_t standbyDelayMs_ = 1500;
  std::uint8_t currentDigitIndex_ = 0;
  std::uint8_t currentPulseIndex_ = 0;
  std::uint8_t inputIndex_ = 0;
  std::array<std::uint8_t, kMaxDigits> entered_{};

  std::uint8_t rewardDigit_ = 0;
  std::uint32_t pendingPenaltyMs_ = 0;
};
