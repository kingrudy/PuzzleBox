#pragma once

#include <Adafruit_NeoPixel.h>

#include "protocol/game_state.h"

// 2x WS2812B 8x8 matrices, chained on one data line. See
// spec/puzzlebox_hw.md section 4.1.
class MatrixService {
 public:
  void begin();
  void renderGameState(protocol::GameState state);
  void runTest();

 private:
  static constexpr std::uint16_t kPixelCount = 128;
  static constexpr std::uint16_t kHalf = kPixelCount / 2;

  // Pin 13 is hardcoded here to match the documented (known) discrepancy
  // with pins::kMatrixData — see spec/puzzlebox_hw.md section 11.
  Adafruit_NeoPixel strip_{kPixelCount, 13, NEO_GRB + NEO_KHZ800};
};
