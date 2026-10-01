#include <Arduino.h>
#include <Wire.h>

#include <cstdint>

// IMU sensor node: Trinket M0 + GY-91 (MPU-9250 @0x68, AK8963 @0x0C via
// bypass, BMP280 @0x76 -- hardware-confirmed by an I2C ID probe). Streams one
// JSON line per sample on Serial1 TX (pin 4) to the main controller's
// ImuService (GPIO27). Integers only: printf has no float support on this
// core.
//
// Line fields (all in the MPU accel frame, magnetometer axes remapped):
//   ax ay az  acceleration, milli-g
//   gx gy gz  rotation rate, 0.1 deg/s
//   mx my mz  magnetic field, 0.1 uT (0 when no fresh sample yet)
//   p         pressure, Pa
//   t         temperature, 0.01 degC
//   n         sample counter (gaps = dropped lines)

namespace {

constexpr std::uint8_t kMpuAddr = 0x68;
constexpr std::uint8_t kMagAddr = 0x0C;
constexpr std::uint8_t kBaroAddr = 0x76;
constexpr std::uint32_t kLinkBaud = 115200;
constexpr std::uint32_t kSamplePeriodMs = 20;  // 50 Hz

bool writeReg(std::uint8_t addr, std::uint8_t reg, std::uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegs(std::uint8_t addr, std::uint8_t reg, std::uint8_t* out, std::uint8_t len) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  if (Wire.requestFrom(addr, len) != len) {
    return false;
  }
  for (std::uint8_t i = 0; i < len; ++i) {
    out[i] = Wire.read();
  }
  return true;
}

// --- MPU-9250 ---------------------------------------------------------------

bool mpuOk = false;

bool initMpu() {
  std::uint8_t who = 0;
  if (!readRegs(kMpuAddr, 0x75, &who, 1) || who != 0x71) {
    return false;
  }
  writeReg(kMpuAddr, 0x6B, 0x80);  // reset
  delay(100);
  writeReg(kMpuAddr, 0x6B, 0x01);  // wake, PLL clock
  delay(10);
  writeReg(kMpuAddr, 0x1A, 0x03);  // gyro DLPF 41 Hz
  writeReg(kMpuAddr, 0x19, 0x04);  // 200 Hz internal sample rate
  writeReg(kMpuAddr, 0x1B, 0x08);  // gyro +-500 dps
  writeReg(kMpuAddr, 0x1C, 0x08);  // accel +-4 g
  writeReg(kMpuAddr, 0x1D, 0x03);  // accel DLPF 41 Hz
  writeReg(kMpuAddr, 0x37, 0x02);  // I2C bypass -> AK8963 visible at 0x0C
  return true;
}

// --- AK8963 magnetometer ----------------------------------------------------

bool magOk = false;
std::uint8_t magAsa[3] = {128, 128, 128};
std::int32_t magX = 0, magY = 0, magZ = 0;  // 0.1 uT, sensor frame

bool initMag() {
  std::uint8_t wia = 0;
  if (!readRegs(kMagAddr, 0x00, &wia, 1) || wia != 0x48) {
    return false;
  }
  writeReg(kMagAddr, 0x0A, 0x00);  // power down
  delay(10);
  writeReg(kMagAddr, 0x0A, 0x0F);  // fuse ROM access
  delay(10);
  readRegs(kMagAddr, 0x10, magAsa, 3);
  writeReg(kMagAddr, 0x0A, 0x00);
  delay(10);
  writeReg(kMagAddr, 0x0A, 0x16);  // 16-bit, continuous mode 2 (100 Hz)
  delay(10);
  return true;
}

void readMag() {
  std::uint8_t st1 = 0;
  if (!readRegs(kMagAddr, 0x02, &st1, 1) || (st1 & 0x01) == 0) {
    return;  // no new sample; keep the previous one
  }
  std::uint8_t b[7];  // HXL..HZH + ST2 (reading ST2 releases the data lock)
  if (!readRegs(kMagAddr, 0x03, b, 7) || (b[6] & 0x08) != 0) {
    return;  // magnetic overflow
  }
  const std::int16_t raw[3] = {static_cast<std::int16_t>(b[1] << 8 | b[0]),
                               static_cast<std::int16_t>(b[3] << 8 | b[2]),
                               static_cast<std::int16_t>(b[5] << 8 | b[4])};
  std::int32_t adj[3];
  for (int i = 0; i < 3; ++i) {
    // Datasheet sensitivity adjustment, then 0.15 uT/LSB -> 0.1 uT units.
    adj[i] = static_cast<std::int32_t>(raw[i]) * (magAsa[i] + 128) / 256 * 15 / 10;
  }
  magX = adj[0];
  magY = adj[1];
  magZ = adj[2];
}

// --- BMP280 -----------------------------------------------------------------

bool baroOk = false;
std::uint16_t digT1, digP1;
std::int16_t digT2, digT3, digP2, digP3, digP4, digP5, digP6, digP7, digP8, digP9;

bool initBaro() {
  std::uint8_t id = 0;
  if (!readRegs(kBaroAddr, 0xD0, &id, 1) || id != 0x58) {
    return false;
  }
  std::uint8_t c[24];
  if (!readRegs(kBaroAddr, 0x88, c, 24)) {
    return false;
  }
  auto u16 = [&](int i) { return static_cast<std::uint16_t>(c[i + 1] << 8 | c[i]); };
  auto s16 = [&](int i) { return static_cast<std::int16_t>(c[i + 1] << 8 | c[i]); };
  digT1 = u16(0);
  digT2 = s16(2);
  digT3 = s16(4);
  digP1 = u16(6);
  digP2 = s16(8);
  digP3 = s16(10);
  digP4 = s16(12);
  digP5 = s16(14);
  digP6 = s16(16);
  digP7 = s16(18);
  digP8 = s16(20);
  digP9 = s16(22);
  writeReg(kBaroAddr, 0xF5, 0x10);  // standby 0.5 ms, IIR filter x16
  writeReg(kBaroAddr, 0xF4, 0x57);  // temp x2, pressure x16, normal mode
  return true;
}

// Bosch reference integer compensation (BMP280 datasheet section 8.2).
bool readBaro(std::int32_t& pressurePa, std::int32_t& tempCenti) {
  std::uint8_t b[6];
  if (!readRegs(kBaroAddr, 0xF7, b, 6)) {
    return false;
  }
  const std::int32_t adcP = (static_cast<std::int32_t>(b[0]) << 12) | (b[1] << 4) | (b[2] >> 4);
  const std::int32_t adcT = (static_cast<std::int32_t>(b[3]) << 12) | (b[4] << 4) | (b[5] >> 4);

  std::int32_t var1 = ((((adcT >> 3) - (static_cast<std::int32_t>(digT1) << 1))) * digT2) >> 11;
  std::int32_t var2 = (((((adcT >> 4) - digT1) * ((adcT >> 4) - digT1)) >> 12) * digT3) >> 14;
  const std::int32_t tFine = var1 + var2;
  tempCenti = (tFine * 5 + 128) >> 8;

  std::int64_t p1 = static_cast<std::int64_t>(tFine) - 128000;
  std::int64_t p2 = p1 * p1 * digP6;
  p2 = p2 + ((p1 * digP5) << 17);
  p2 = p2 + (static_cast<std::int64_t>(digP4) << 35);
  p1 = ((p1 * p1 * digP3) >> 8) + ((p1 * digP2) << 12);
  p1 = ((static_cast<std::int64_t>(1) << 47) + p1) * digP1 >> 33;
  if (p1 == 0) {
    return false;
  }
  std::int64_t p = 1048576 - adcP;
  p = (((p << 31) - p2) * 3125) / p1;
  p1 = (static_cast<std::int64_t>(digP9) * (p >> 13) * (p >> 13)) >> 25;
  p2 = (static_cast<std::int64_t>(digP8) * p) >> 19;
  p = ((p + p1 + p2) >> 8) + (static_cast<std::int64_t>(digP7) << 4);
  pressurePa = static_cast<std::int32_t>(p / 256);
  return true;
}

// --- main loop --------------------------------------------------------------

std::uint32_t sampleCount = 0;
std::uint32_t nextSampleMs = 0;
std::uint32_t lastInitTryMs = 0;

void initSensors() {
  mpuOk = initMpu();
  magOk = mpuOk && initMag();
  baroOk = initBaro();
}

void emit(const char* line) {
  Serial1.println(line);
  // Mirror a fraction to USB for bench debugging (USB serial may not be open).
  if (sampleCount % 25 == 0 && Serial) {
    Serial.println(line);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  Serial1.begin(kLinkBaud);
  Wire.begin();
  Wire.setClock(400000);
  initSensors();
}

void loop() {
  const std::uint32_t now = millis();
  if (static_cast<std::int32_t>(now - nextSampleMs) < 0) {
    return;
  }
  nextSampleMs = now + kSamplePeriodMs;
  ++sampleCount;

  if (!mpuOk || !baroOk) {
    if (now - lastInitTryMs >= 1000) {
      lastInitTryMs = now;
      initSensors();
    }
    char err[64];
    snprintf(err, sizeof(err), "{\"err\":\"init\",\"mpu\":%d,\"mag\":%d,\"baro\":%d,\"n\":%lu}", mpuOk, magOk,
             baroOk, static_cast<unsigned long>(sampleCount));
    emit(err);
    return;
  }

  std::uint8_t b[14];
  if (!readRegs(kMpuAddr, 0x3B, b, 14)) {
    mpuOk = false;
    return;
  }
  auto s16be = [&](int i) { return static_cast<std::int32_t>(static_cast<std::int16_t>(b[i] << 8 | b[i + 1])); };
  // +-4 g -> 8192 LSB/g; +-500 dps -> 65.5 LSB/dps.
  const std::int32_t ax = s16be(0) * 1000 / 8192;
  const std::int32_t ay = s16be(2) * 1000 / 8192;
  const std::int32_t az = s16be(4) * 1000 / 8192;
  const std::int32_t gx = s16be(8) * 100 / 655;
  const std::int32_t gy = s16be(10) * 100 / 655;
  const std::int32_t gz = s16be(12) * 100 / 655;

  if (magOk) {
    readMag();
  }

  std::int32_t pressurePa = 0;
  std::int32_t tempCenti = 0;
  readBaro(pressurePa, tempCenti);

  // AK8963 axes relative to the MPU accel frame: X=Y, Y=X, Z=-Z.
  char line[160];
  snprintf(line, sizeof(line),
           "{\"ax\":%ld,\"ay\":%ld,\"az\":%ld,\"gx\":%ld,\"gy\":%ld,\"gz\":%ld,\"mx\":%ld,\"my\":%ld,\"mz\":%ld,"
           "\"p\":%ld,\"t\":%ld,\"n\":%lu}",
           static_cast<long>(ax), static_cast<long>(ay), static_cast<long>(az), static_cast<long>(gx),
           static_cast<long>(gy), static_cast<long>(gz), static_cast<long>(magY), static_cast<long>(magX),
           static_cast<long>(-magZ), static_cast<long>(pressurePa), static_cast<long>(tempCenti),
           static_cast<unsigned long>(sampleCount));
  emit(line);
}
