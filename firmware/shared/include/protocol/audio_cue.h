#pragma once

#include <cstdint>

// Audio cue IDs emitted by the main controller and played by whichever
// display can (only the 8048S050C has an I2S DAC — see
// DisplaySpeakerService in spec/puzzlebox_hw.md section 6.3). The main
// controller only ever emits a cue ID into diagnostics; it never renders
// audio itself.

namespace protocol {

enum class AudioCueId : std::uint8_t {
  ShortBeep = 0,
  DoubleBeep,
  TestMelody,
  Hint,
  Countdown,
  RunStart,
  Error,
  Success,
  Endgame,
};

inline constexpr std::uint16_t kAudioCueDurationMs[] = {
    320,   // ShortBeep
    340,   // DoubleBeep
    1240,  // TestMelody
    440,   // Hint
    390,   // Countdown
    380,   // RunStart
    740,   // Error
    420,   // Success
    980,   // Endgame
};

inline std::uint16_t audioCueDurationMs(AudioCueId cue) {
  return kAudioCueDurationMs[static_cast<std::uint8_t>(cue)];
}

}  // namespace protocol
