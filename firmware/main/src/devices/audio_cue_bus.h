#pragma once

#include <Arduino.h>

#include <array>
#include <cstdint>

#include "protocol/audio_cue.h"

// The main controller has no speaker of its own (spec/puzzlebox_hw.md
// section 6.3) — this class only queues *requests*, surfaced to the display
// node via AppController's /api/game JSON. The display's DisplaySpeakerService
// (firmware/display/src) sums whatever voices are active and actually
// renders sound.
//
// Two independent channels:
//  - Fixed named cues (playCue) — cueSeq() increments on every call so the
//    display can dedupe an unchanged cue id from a genuinely new play
//    request.
//  - Up to kVoiceCount simultaneous tone voices. Most puzzles only ever need
//    voice 0 (the playTone/holdTone/stopTone/toneActive/toneHz convenience
//    methods below are aliases for voice 0). SpectralTuner and
//    LivingInterval need all 3 at once — one per encoder, exactly as their
//    docs/puzzles/*.md sound design sections describe — hence the array
//    instead of a single frequency.
class AudioCueBus {
 public:
  static constexpr std::uint8_t kVoiceCount = 3;

  void playCue(protocol::AudioCueId cue) {
    cueId_ = cue;
    ++cueSeq_;
  }

  // One-shot: voice audible for durationMs from now, then silent unless refreshed.
  void playToneVoice(std::uint8_t voice, float freqHz, std::uint32_t durationMs) {
    if (voice >= kVoiceCount) return;
    voices_[voice].hz = freqHz;
    voices_[voice].untilMs = millis() + durationMs;
    voices_[voice].holding = false;
  }

  // Continuous: voice audible until stopToneVoice(). Safe to call every
  // poll() with an updated frequency — that's how a live pitch sweep works.
  void holdToneVoice(std::uint8_t voice, float freqHz) {
    if (voice >= kVoiceCount) return;
    voices_[voice].hz = freqHz;
    voices_[voice].holding = true;
  }

  void stopToneVoice(std::uint8_t voice) {
    if (voice >= kVoiceCount) return;
    voices_[voice] = Voice{};
  }

  bool voiceActive(std::uint8_t voice) const {
    if (voice >= kVoiceCount) return false;
    const Voice& v = voices_[voice];
    if (v.hz <= 0.0f) return false;
    return v.holding || millis() < v.untilMs;
  }

  float voiceHz(std::uint8_t voice) const { return voiceActive(voice) ? voices_[voice].hz : 0.0f; }

  // Voice-0 convenience aliases, used by every single-tone puzzle.
  void playTone(float freqHz, std::uint32_t durationMs) { playToneVoice(0, freqHz, durationMs); }
  void holdTone(float freqHz) { holdToneVoice(0, freqHz); }
  void stopTone() { stopToneVoice(0); }
  bool toneActive() const { return voiceActive(0); }
  float toneHz() const { return voiceHz(0); }

  protocol::AudioCueId lastCue() const { return cueId_; }
  std::uint32_t cueSeq() const { return cueSeq_; }

 private:
  struct Voice {
    float hz = 0.0f;
    std::uint32_t untilMs = 0;
    bool holding = false;
  };

  protocol::AudioCueId cueId_ = protocol::AudioCueId::ShortBeep;
  std::uint32_t cueSeq_ = 0;
  std::array<Voice, kVoiceCount> voices_{};
};
