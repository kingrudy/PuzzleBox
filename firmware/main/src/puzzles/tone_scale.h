#pragma once

#include <cstdint>

// Shared 8-note diatonic scale used everywhere a puzzle maps a discrete
// position (TM1638 LED index, grid cell, etc.) to a pitch — see
// docs/puzzles/puzzle_01.md's "per-step tone" and every later stage's reuse
// of the same vocabulary. Root A3, major scale, one octave.
namespace tone_scale {

inline constexpr float kRootHz = 220.0f;  // A3
inline constexpr float kRatios[8] = {
    1.0f,     // root
    1.1225f,  // major 2nd
    1.2599f,  // major 3rd
    1.3348f,  // perfect 4th
    1.4983f,  // perfect 5th
    1.6818f,  // major 6th
    1.8877f,  // major 7th
    2.0f,     // octave
};

inline float noteHz(std::uint8_t index) {
  return kRootHz * kRatios[index & 0x07];
}

}  // namespace tone_scale
