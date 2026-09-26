#include "puzzles/sequence_recall.h"

#include "puzzles/tone_scale.h"

void SequenceRecallEngine::reset(const std::uint8_t* sequence, std::uint8_t length,
                                  std::uint16_t onMs, std::uint16_t offMs,
                                  std::uint32_t errorPenaltyMs, const char* playbackPhaseLabel) {
  length_ = length > kMaxLength ? kMaxLength : length;
  for (std::uint8_t i = 0; i < length_; ++i) {
    sequence_[i] = sequence[i] % kAlphabetSize;
  }
  onMs_ = onMs;
  offMs_ = offMs;
  errorPenaltyMs_ = errorPenaltyMs;
  playbackPhaseLabel_ = playbackPhaseLabel;

  phase_ = Phase::kPlayback;
  stepIndex_ = 0;
  inputIndex_ = 0;
  phaseStartMs_ = 0;  // set on first poll() so it lines up with real nowMs
  lastLedOn_ = false;
  pendingPenaltyMs_ = 0;
}

std::uint32_t SequenceRecallEngine::takePenaltyMs() {
  const std::uint32_t owed = pendingPenaltyMs_;
  pendingPenaltyMs_ = 0;
  return owed;
}

void SequenceRecallEngine::poll(InputPanelService& panel, AudioCueBus& audio,
                                 std::uint32_t nowMs) {
  if (phase_ == Phase::kDone) {
    return;
  }

  if (phaseStartMs_ == 0) {
    phaseStartMs_ = nowMs;
  }

  switch (phase_) {
    case Phase::kPlayback: {
      const std::uint32_t elapsed = nowMs - phaseStartMs_;
      const std::uint32_t stepTotal = onMs_ + offMs_;
      if (elapsed >= stepTotal) {
        ++stepIndex_;
        phaseStartMs_ = nowMs;
        if (stepIndex_ >= length_) {
          phase_ = Phase::kInput;
          inputIndex_ = 0;
          audio.stopTone();
          panel.renderPatternPuzzle("INPT", 0, 0, length_, false);
          return;
        }
      }
      const bool ledOn = (nowMs - phaseStartMs_) < onMs_;
      const std::uint8_t mask = ledOn ? static_cast<std::uint8_t>(1 << sequence_[stepIndex_]) : 0;
      if (ledOn && !lastLedOn_) {
        audio.playTone(tone_scale::noteHz(sequence_[stepIndex_]), onMs_);
      }
      lastLedOn_ = ledOn;
      panel.renderPatternPuzzle(playbackPhaseLabel_, mask, stepIndex_, length_, ledOn);
      break;
    }

    case Phase::kInput: {
      std::uint8_t btn;
      while (panel.takeLedKeyButtonPress(btn)) {
        if (btn >= SequenceRecallEngine::kAlphabetSize) {
          continue;
        }
        if (btn == sequence_[inputIndex_]) {
          audio.playTone(tone_scale::noteHz(btn), 180);
          ++inputIndex_;
          if (inputIndex_ >= length_) {
            phase_ = Phase::kDone;
            audio.playCue(protocol::AudioCueId::Success);
            panel.renderPatternPuzzle("GOOD", 0xFF, length_, length_, true);
            return;
          }
        } else {
          phase_ = Phase::kError;
          phaseStartMs_ = nowMs;
          pendingPenaltyMs_ += errorPenaltyMs_;
          audio.playCue(protocol::AudioCueId::Error);
          break;
        }
      }
      panel.renderPatternPuzzle("INPT", 0, inputIndex_, length_, false);
      break;
    }

    case Phase::kError: {
      const bool blinkOn = ((nowMs - phaseStartMs_) / 150) % 2 == 0;
      panel.renderPatternPuzzle("ERR ", 0xFF, inputIndex_, length_, blinkOn);
      if (nowMs - phaseStartMs_ >= kErrorFlashMs) {
        phase_ = Phase::kPlayback;
        stepIndex_ = 0;
        phaseStartMs_ = nowMs;
        lastLedOn_ = false;
      }
      break;
    }

    case Phase::kDone:
      break;
  }
}
