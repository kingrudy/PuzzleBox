#include "devices/imu_service.h"

#include <ArduinoJson.h>

#include <cmath>

#include "devices/device_pins.h"

void ImuService::begin() {
  // RX only; the Trinket never needs anything from us.
  uart_.begin(115200, SERIAL_8N1, pins::kImuUartRx, -1);
  line_.reserve(192);
}

void ImuService::poll() {
  while (uart_.available() > 0) {
    const char c = static_cast<char>(uart_.read());
    if (c == '\r') {
      continue;
    }
    if (c != '\n') {
      line_ += c;
      if (line_.length() > 256) {
        line_ = "";  // garbage -- resync on the next '\n'
        ++linesBad_;
      }
      continue;
    }
    if (line_.length() > 0) {
      handleLine();
      line_ = "";
    }
  }
}

void ImuService::handleLine() {
  JsonDocument doc;
  if (deserializeJson(doc, line_) != DeserializationError::Ok) {
    ++linesBad_;
    return;
  }
  ++linesOk_;
  lastLineMs_ = millis();

  const std::uint32_t seq = doc["n"] | 0;
  if (sample_.seq != 0 && seq > sample_.seq + 1) {
    samplesDropped_ += seq - sample_.seq - 1;
  }

  if (!doc["err"].isNull()) {
    sensorError_ = String("mpu=") + (doc["mpu"] | 0) + " mag=" + (doc["mag"] | 0) + " baro=" + (doc["baro"] | 0);
    sample_.seq = seq;
    return;
  }
  sensorError_ = "";

  sample_.ax = doc["ax"] | 0;
  sample_.ay = doc["ay"] | 0;
  sample_.az = doc["az"] | 0;
  sample_.gx = doc["gx"] | 0;
  sample_.gy = doc["gy"] | 0;
  sample_.gz = doc["gz"] | 0;
  sample_.mx = doc["mx"] | 0;
  sample_.my = doc["my"] | 0;
  sample_.mz = doc["mz"] | 0;
  sample_.pressurePa = doc["p"] | 0;
  sample_.tempCenti = doc["t"] | 0;
  sample_.seq = seq;
}

bool ImuService::online() const {
  return linesOk_ > 0 && millis() - lastLineMs_ < kOnlineTimeoutMs;
}

float ImuService::rollDeg() const {
  return std::atan2(static_cast<float>(sample_.ay), static_cast<float>(sample_.az)) * 57.29578f;
}

float ImuService::pitchDeg() const {
  const float ay = sample_.ay;
  const float az = sample_.az;
  return std::atan2(-static_cast<float>(sample_.ax), std::sqrt(ay * ay + az * az)) * 57.29578f;
}
