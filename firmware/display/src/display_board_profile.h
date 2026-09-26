#pragma once

#include <cstdint>

// Pin maps for both Sunton display boards live here, and only here. App
// code reads through this profile and never hardcodes a pin. Branched at
// compile time on ESP32_2424S012N / ESP32_8048S050C (set via platformio.ini
// build_flags for the s3_display / round_display_2424 environments). See
// spec/puzzlebox_hw.md section 6.

#if defined(ESP32_8048S050C) && defined(ESP32_2424S012N)
#error "Exactly one of ESP32_8048S050C / ESP32_2424S012N must be defined"
#elif !defined(ESP32_8048S050C) && !defined(ESP32_2424S012N)
#error "One of ESP32_8048S050C / ESP32_2424S012N must be defined"
#endif

namespace display_board {

#if defined(ESP32_8048S050C)

inline constexpr char kBoardName[] = "ESP32-8048S050C";
inline constexpr std::uint16_t kWidth = 800;
inline constexpr std::uint16_t kHeight = 480;
inline constexpr bool kRound = false;
inline constexpr bool kHasTouch = true;
inline constexpr bool kHasAudio = true;
inline constexpr bool kHasTfCard = true;

inline constexpr std::uint8_t kBacklightPin = 2;
inline constexpr float kBacklightDuty = 0.68f;

// RGB parallel panel, 14 MHz pixel clock.
inline constexpr std::uint8_t kRgbHsync = 39;
inline constexpr std::uint8_t kRgbVsync = 41;
inline constexpr std::uint8_t kRgbDe = 40;
inline constexpr std::uint8_t kRgbPclk = 42;
inline constexpr std::uint32_t kRgbPclkHz = 14000000;

// R/B swapped from the original spec.md transcription — two independent
// hardware-validated example repos for this board family (Sunton S043,
// same PCB/timing as our S050C) both agree R={45,48,47,21,14},
// B={8,3,46,9,1}, the opposite of what was first written down here.
inline constexpr std::uint8_t kRgbR[5] = {45, 48, 47, 21, 14};
inline constexpr std::uint8_t kRgbG[6] = {5, 6, 7, 15, 16, 4};
inline constexpr std::uint8_t kRgbB[5] = {8, 3, 46, 9, 1};

// GT911 capacitive touch, I2C.
inline constexpr std::uint8_t kTouchSda = 19;
inline constexpr std::uint8_t kTouchScl = 20;
inline constexpr std::uint8_t kTouchRst = 38;
inline constexpr std::uint8_t kTouchAddr = 0x5D;
inline constexpr std::uint32_t kTouchI2cHz = 400000;

// I2S audio out.
inline constexpr std::uint8_t kI2sBck = 0;
inline constexpr std::uint8_t kI2sLrck = 18;
inline constexpr std::uint8_t kI2sDin = 17;

// TF card, SPI.
inline constexpr std::uint8_t kTfCs = 10;
inline constexpr std::uint8_t kTfMosi = 11;
inline constexpr std::uint8_t kTfSclk = 12;
inline constexpr std::uint8_t kTfMiso = 13;

#elif defined(ESP32_2424S012N)

inline constexpr char kBoardName[] = "ESP32-2424S012N";
inline constexpr std::uint16_t kWidth = 240;
inline constexpr std::uint16_t kHeight = 240;
inline constexpr bool kRound = true;
inline constexpr bool kHasTouch = false;
inline constexpr bool kHasAudio = false;
inline constexpr bool kHasTfCard = false;

inline constexpr std::uint8_t kBacklightPin = 3;
inline constexpr float kBacklightDuty = 1.0f;

// GC9A01 over SPI, 80 MHz, MIRROR_X, BGR colour space. MISO/RESET not
// connected.
inline constexpr std::uint8_t kSpiMosi = 7;
inline constexpr std::uint8_t kSpiSclk = 6;
inline constexpr std::uint8_t kSpiCs = 10;
inline constexpr std::uint8_t kSpiDc = 2;
inline constexpr std::uint32_t kSpiHz = 80000000;
inline constexpr bool kMirrorX = true;

#endif

}  // namespace display_board
