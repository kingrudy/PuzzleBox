#include "puzzles/vibrational_cipher_puzzle.h"

#include <Arduino.h>

#include "protocol/audio_cue.h"

namespace {
constexpr std::uint8_t kAlphabetSize = 8;  // digit values 0-7, all TM1638 buttons
constexpr std::uint8_t kDigitCountByDifficulty[3] = {3, 3, 4};
constexpr std::uint16_t kPulseOnMsByDifficulty[3] = {180, 150, 100};
constexpr std::uint16_t kPulseGapMsByDifficulty[3] = {220, 180, 130};
constexpr std::uint16_t kDigitGapMsByDifficulty[3] = {700, 550, 400};
constexpr std::uint32_t kStandbyMinMs = 1000;
constexpr std::uint32_t kStandbyMaxMsByDifficulty[3] = {3000, 3000, 5000};
constexpr std::uint32_t kPenaltyMsByDifficulty[3] = {5000, 8000, 12000};
constexpr std::uint16_t kErrorFlashMs = 600;
constexpr std::uint32_t kRetryStandbyMs = 500;
constexpr float kHumHz = 90.0f;
}  // namespace

void VibrationalCipherPuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  const std::uint8_t tier = static_cast<std::uint8_t>(difficulty);
  digitCount_ = kDigitCountByDifficulty[tier];
  pulseOnMs_ = kPulseOnMsByDifficulty[tier];
  pulseGapMs_ = kPulseGapMsByDifficulty[tier];
  digitGapMs_ = kDigitGapMsByDifficulty[tier];
  standbyMinMs_ = kStandbyMinMs;
  standbyMaxMs_ = kStandbyMaxMsByDifficulty[tier];
  retransmitPenaltyMs_ = kPenaltyMsByDifficulty[tier];

  for (std::uint8_t i = 0; i < digitCount_; ++i) {
    digits_[i] = static_cast<std::uint8_t>(random(kAlphabetSize));
  }
  rewardDigit_ = static_cast<std::uint8_t>(random(kAlphabetSize));

  phase_ = Phase::kStandby;
  standbyDelayMs_ = standbyMinMs_ + random(standbyMaxMs_ - standbyMinMs_ + 1);
  phaseStartMs_ = ctx.nowMs;
  pendingPenaltyMs_ = 0;
  inputIndex_ = 0;
}

std::uint32_t VibrationalCipherPuzzle::takePenaltyMs() {
  const std::uint32_t owed = pendingPenaltyMs_;
  pendingPenaltyMs_ = 0;
  return owed;
}

void VibrationalCipherPuzzle::beginTransmission(std::uint32_t nowMs, bool isRetry) {
  (void)isRetry;
  phase_ = Phase::kTransmitOn;
  currentDigitIndex_ = 0;
  currentPulseIndex_ = 0;
  phaseStartMs_ = nowMs;
}

void VibrationalCipherPuzzle::poll(puzzles::PuzzleContext& ctx) {
  if (phase_ == Phase::kDone) {
    return;
  }

  switch (phase_) {
    case Phase::kStandby: {
      ctx.audio.holdTone(kHumHz);
      ctx.panel.renderCipherPuzzle("STBY", entered_.data(), 0, digitCount_);
      if (ctx.nowMs - phaseStartMs_ >= standbyDelayMs_) {
        ctx.audio.stopTone();
        ctx.vibration.pulse(pulseOnMs_);
        beginTransmission(ctx.nowMs, false);
      }
      break;
    }

    case Phase::kTransmitOn: {
      ctx.panel.renderCipherPuzzle("XMIT", entered_.data(), 0, digitCount_);
      if (ctx.nowMs - phaseStartMs_ >= pulseOnMs_) {
        ++currentPulseIndex_;
        const std::uint8_t targetPulses = static_cast<std::uint8_t>(digits_[currentDigitIndex_] + 1);
        if (currentPulseIndex_ >= targetPulses) {
          ++currentDigitIndex_;
          currentPulseIndex_ = 0;
          if (currentDigitIndex_ >= digitCount_) {
            phase_ = Phase::kInput;
            inputIndex_ = 0;
            phaseStartMs_ = ctx.nowMs;
          } else {
            phase_ = Phase::kDigitGap;
            phaseStartMs_ = ctx.nowMs;
          }
        } else {
          phase_ = Phase::kTransmitGap;
          phaseStartMs_ = ctx.nowMs;
        }
      }
      break;
    }

    case Phase::kTransmitGap: {
      if (ctx.nowMs - phaseStartMs_ >= pulseGapMs_) {
        ctx.vibration.pulse(pulseOnMs_);
        phase_ = Phase::kTransmitOn;
        phaseStartMs_ = ctx.nowMs;
      }
      break;
    }

    case Phase::kDigitGap: {
      if (ctx.nowMs - phaseStartMs_ >= digitGapMs_) {
        ctx.vibration.pulse(pulseOnMs_);
        phase_ = Phase::kTransmitOn;
        phaseStartMs_ = ctx.nowMs;
      }
      break;
    }

    case Phase::kInput: {
      std::uint8_t btn;
      while (ctx.panel.takeLedKeyButtonPress(btn)) {
        if (btn >= kAlphabetSize) {
          continue;
        }
        entered_[inputIndex_] = btn;
        if (btn == digits_[inputIndex_]) {
          ++inputIndex_;
          if (inputIndex_ >= digitCount_) {
            phase_ = Phase::kDone;
            ctx.audio.playCue(protocol::AudioCueId::Success);
            ctx.panel.renderCipherPuzzle("GOOD", entered_.data(), digitCount_, digitCount_);
            return;
          }
        } else {
          phase_ = Phase::kError;
          phaseStartMs_ = ctx.nowMs;
          pendingPenaltyMs_ += retransmitPenaltyMs_;
          ctx.audio.playCue(protocol::AudioCueId::Error);
          break;
        }
      }
      ctx.panel.renderCipherPuzzle("INPT", entered_.data(), inputIndex_, digitCount_);
      break;
    }

    case Phase::kError: {
      ctx.panel.renderCipherPuzzle("ERR ", entered_.data(), inputIndex_, digitCount_);
      if (ctx.nowMs - phaseStartMs_ >= kErrorFlashMs) {
        inputIndex_ = 0;
        standbyDelayMs_ = kRetryStandbyMs;
        phase_ = Phase::kStandby;
        phaseStartMs_ = ctx.nowMs;
      }
      break;
    }

    case Phase::kDone:
      break;
  }
}
