#include "display_speaker_service.h"

#if defined(ESP32_8048S050C)

#include <Arduino.h>
#include <driver/i2s.h>
#include <cmath>

namespace {
constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr int kSampleRateHz = 16000;
constexpr int kChunkSamples = 256;  // ~16ms of audio per poll() call
constexpr float kTwoPi = 6.28318530718f;
constexpr float kMasterGain = 0.22f;  // headroom for up to 4 summed voices

using ToneStep = DisplaySpeakerService::ToneStep;

// Fixed-frequency approximation of each named cue — see the simplification
// note in display_speaker_service.h. Durations sum to (at most)
// protocol::kAudioCueDurationMs[cue]; 0 Hz = silent gap.
constexpr ToneStep kShortBeep[] = {{880.0f, 320}};
constexpr ToneStep kDoubleBeep[] = {{880.0f, 140}, {0.0f, 60}, {880.0f, 140}};
constexpr ToneStep kTestMelody[] = {{523.0f, 300}, {659.0f, 300}, {784.0f, 300}, {1047.0f, 340}};
constexpr ToneStep kHint[] = {{660.0f, 440}};
constexpr ToneStep kCountdown[] = {{440.0f, 390}};
constexpr ToneStep kRunStart[] = {{523.0f, 190}, {784.0f, 190}};
constexpr ToneStep kError[] = {{180.0f, 740}};
constexpr ToneStep kSuccess[] = {{659.0f, 180}, {784.0f, 240}};
constexpr ToneStep kEndgame[] = {{523.0f, 200}, {659.0f, 200}, {784.0f, 200}, {1047.0f, 380}};

void cueSteps(protocol::AudioCueId cue, const ToneStep** steps, std::uint8_t* count) {
  switch (cue) {
    case protocol::AudioCueId::ShortBeep: *steps = kShortBeep; *count = 1; return;
    case protocol::AudioCueId::DoubleBeep: *steps = kDoubleBeep; *count = 3; return;
    case protocol::AudioCueId::TestMelody: *steps = kTestMelody; *count = 4; return;
    case protocol::AudioCueId::Hint: *steps = kHint; *count = 1; return;
    case protocol::AudioCueId::Countdown: *steps = kCountdown; *count = 1; return;
    case protocol::AudioCueId::RunStart: *steps = kRunStart; *count = 2; return;
    case protocol::AudioCueId::Error: *steps = kError; *count = 1; return;
    case protocol::AudioCueId::Success: *steps = kSuccess; *count = 2; return;
    case protocol::AudioCueId::Endgame: *steps = kEndgame; *count = 4; return;
  }
  *steps = kShortBeep;
  *count = 1;
}

std::int16_t squareSample(float phase01, float gain) {
  const float v = (phase01 < 0.5f) ? gain : -gain;
  return static_cast<std::int16_t>(v * 32000.0f);
}

}  // namespace

void DisplaySpeakerService::begin(diagnostics::EventLog& log) {
  i2s_config_t config = {};
  config.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = kSampleRateHz;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  config.dma_buf_count = 4;
  config.dma_buf_len = kChunkSamples;
  config.use_apll = false;

  const esp_err_t installErr = i2s_driver_install(kI2sPort, &config, 0, nullptr);
  if (installErr != ESP_OK) {
    log.logf("speaker", "i2s_driver_install failed: %d", installErr);
    ready_ = false;
    return;
  }

  i2s_pin_config_t pinConfig = {};
  pinConfig.bck_io_num = display_board::kI2sBck;
  pinConfig.ws_io_num = display_board::kI2sLrck;
  pinConfig.data_out_num = display_board::kI2sDin;
  pinConfig.data_in_num = I2S_PIN_NO_CHANGE;
  i2s_set_pin(kI2sPort, &pinConfig);
  i2s_zero_dma_buffer(kI2sPort);

  ready_ = true;
  log.logf("speaker", "I2S ready (16kHz/16bit)");
}

void DisplaySpeakerService::setVoiceHz(std::uint8_t voice, float hz) {
  if (voice >= kVoiceCount) return;
  voiceHz_[voice] = hz;
}

void DisplaySpeakerService::playCue(protocol::AudioCueId cue) {
  cueSteps(cue, &cueSteps_, &cueStepCount_);
  cueStepIndex_ = 0;
  cueStepEndMs_ = millis() + cueSteps_[0].ms;
  lastQueuedCue_ = cue;
}

void DisplaySpeakerService::advanceCue() {
  if (cueSteps_ == nullptr || cueStepIndex_ >= cueStepCount_) {
    return;
  }
  if (millis() >= cueStepEndMs_) {
    ++cueStepIndex_;
    if (cueStepIndex_ < cueStepCount_) {
      cueStepEndMs_ = millis() + cueSteps_[cueStepIndex_].ms;
    }
  }
}

void DisplaySpeakerService::writeChunk() {
  static std::int16_t buffer[kChunkSamples * 2];  // interleaved stereo

  const bool cueActive = cueSteps_ != nullptr && cueStepIndex_ < cueStepCount_;
  const float cueHz = cueActive ? cueSteps_[cueStepIndex_].hz : 0.0f;
  const float cuePhaseStep = kTwoPi * cueHz / kSampleRateHz;

  float voicePhaseStep[kVoiceCount];
  for (std::uint8_t v = 0; v < kVoiceCount; ++v) {
    voicePhaseStep[v] = kTwoPi * voiceHz_[v] / kSampleRateHz;
  }

  for (int i = 0; i < kChunkSamples; ++i) {
    float mix = 0.0f;

    if (cueHz > 0.0f) {
      cuePhase_ += cuePhaseStep;
      if (cuePhase_ >= kTwoPi) cuePhase_ -= kTwoPi;
      mix += (cuePhase_ < PI) ? 1.0f : -1.0f;
    }

    for (std::uint8_t v = 0; v < kVoiceCount; ++v) {
      if (voiceHz_[v] <= 0.0f) continue;
      voicePhase_[v] += voicePhaseStep[v];
      if (voicePhase_[v] >= kTwoPi) voicePhase_[v] -= kTwoPi;
      mix += (voicePhase_[v] < PI) ? 1.0f : -1.0f;
    }

    const float clamped = mix > 4.0f ? 4.0f : (mix < -4.0f ? -4.0f : mix);
    const std::int16_t sample = static_cast<std::int16_t>(clamped * kMasterGain * 32000.0f / 4.0f);
    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
  }

  std::size_t written = 0;
  i2s_write(kI2sPort, buffer, sizeof(buffer), &written, 0);
}

void DisplaySpeakerService::poll() {
  if (!ready_) {
    return;
  }
  advanceCue();
  writeChunk();
}

#endif  // ESP32_8048S050C
