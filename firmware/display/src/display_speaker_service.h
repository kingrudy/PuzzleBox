#pragma once

#include "diagnostics/event_log.h"
#include "display_board_profile.h"

#if defined(ESP32_8048S050C)

#include <cstdint>

#include "protocol/audio_cue.h"

// I2S square-wave synthesizer — the audio half spec/puzzlebox_hw.md section
// 6.3 documents but that never had an implementation (display/src/main.cpp
// used to carry a standing TODO for exactly this). 16 kHz / 16-bit, Philips
// mode, matching the doc.
//
// Simplification vs. the doc's "musically defined phrase" ambition: each
// fixed cue is a short table of {frequency, duration} steps (see
// display_speaker_service.cpp) rather than a full note/semitone engine —
// enough to make every AudioCueId distinguishable by ear, not a composer.
//
// Two independent channels, mirroring AudioCueBus on the main controller:
//  - playCue(): fire-and-forget, plays out over its own fixed duration.
//  - setVoiceHz(): up to 3 continuous voices, summed in the mix, driven
//    every poll() from AppController's /api/game "audio.voices" array —
//    this is what SpectralTuner/LivingInterval/ResonantGrid's proximity
//    tones use.
class DisplaySpeakerService {
 public:
  static constexpr std::uint8_t kVoiceCount = 3;

  void begin(diagnostics::EventLog& log);
  void poll();  // call every loop; feeds the I2S DMA buffer in small chunks

  void playCue(protocol::AudioCueId cue);
  void setVoiceHz(std::uint8_t voice, float hz);  // 0 = silent

  struct ToneStep {
    float hz;
    std::uint16_t ms;
  };

 private:
  void writeChunk();
  void advanceCue();

  float voiceHz_[kVoiceCount] = {0.0f, 0.0f, 0.0f};
  float voicePhase_[kVoiceCount] = {0.0f, 0.0f, 0.0f};

  const ToneStep* cueSteps_ = nullptr;
  std::uint8_t cueStepCount_ = 0;
  std::uint8_t cueStepIndex_ = 0;
  std::uint32_t cueStepEndMs_ = 0;
  float cuePhase_ = 0.0f;
  protocol::AudioCueId lastQueuedCue_ = protocol::AudioCueId::ShortBeep;
  bool ready_ = false;
};

#endif  // ESP32_8048S050C
