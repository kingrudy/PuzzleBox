#pragma once

#include <Arduino.h>
#include <HardwareSerial.h>

#include <cstdint>

// GY-91 (MPU-9250 + AK8963 + BMP280) read by a Trinket M0 sensor node
// (firmware/trinket/src) and streamed here as JSON lines over a one-way UART:
// Trinket pin 4 (TX) -> GPIO27, common GND. The sensor sits on the Trinket's
// own short I2C bus on purpose -- a long run onto this board's bus would share
// it with the MCP23017/PCA9685 the encoders depend on.
//
// Units match the Trinket's line format: milli-g, 0.1 deg/s, 0.1 uT, Pa,
// 0.01 degC. poll() never blocks.
class ImuService {
 public:
  struct Sample {
    std::int32_t ax = 0, ay = 0, az = 0;
    std::int32_t gx = 0, gy = 0, gz = 0;
    std::int32_t mx = 0, my = 0, mz = 0;
    std::int32_t pressurePa = 0;
    std::int32_t tempCenti = 0;
    std::uint32_t seq = 0;
  };

  void begin();
  void poll();

  // True while lines keep arriving (Trinket streams at 50 Hz).
  bool online() const;
  const Sample& sample() const { return sample_; }
  // Tilt from the accelerometer, degrees. Only meaningful while roughly
  // still; uncalibrated until the accel offsets are measured.
  float rollDeg() const;
  float pitchDeg() const;

  std::uint32_t linesOk() const { return linesOk_; }
  std::uint32_t linesBad() const { return linesBad_; }
  // Gaps in the Trinket's sample counter: samples lost on the wire.
  std::uint32_t samplesDropped() const { return samplesDropped_; }
  // Non-empty when the Trinket reports a sensor init failure.
  const String& sensorError() const { return sensorError_; }

 private:
  void handleLine();

  static constexpr std::uint32_t kOnlineTimeoutMs = 500;

  HardwareSerial uart_{2};
  String line_;
  Sample sample_;
  std::uint32_t lastLineMs_ = 0;
  std::uint32_t linesOk_ = 0;
  std::uint32_t linesBad_ = 0;
  std::uint32_t samplesDropped_ = 0;
  String sensorError_;
};
