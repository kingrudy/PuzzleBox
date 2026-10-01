#pragma once

#include <cstdint>

// Shared game-state and puzzle identifiers. Referenced by the main
// controller (owner of game logic) and echoed to the sidecar/displays over
// ESP-NOW and HTTP/JSON. See spec/puzzlebox_hw.md sections 5, 6, 8 and
// docs/puzzles/plan.md.

namespace protocol {

enum class GameState : std::uint8_t {
  Boot = 0,
  Idle,
  Setup,           // time-limit selection (encoder 1) + start button
  Countdown,       // short countdown before the first puzzle begins
  Active,          // playing the current puzzle in the shuffled order
  Paused,
  Timeout,         // room clock hit zero before all 7 were solved
  Success,         // all 7 solved; remainingSeconds_ is the score
  HighscoreEntry,  // score qualifies for the top-10 list; entering initials
  Maintenance,
};

// The 7 puzzles this box can run, built entirely from hardware marked
// `Connected` in spec/components.md — see docs/puzzles/plan.md section 4 for
// why this departs from the box's originally-sketched RFID/RTC/hidden-sensor/
// servo lineup. Played in a random order each run (AppController shuffles a
// copy of this list at Countdown), so this enum's declaration order is not
// play order.
enum class PuzzleId : std::uint8_t {
  Pattern = 0,       // Energiepatroon — TM1638 LEDs + buttons
  ResonantGrid,      // Resonantieraster — touchscreen warm/cold ordering
  VibrationalCipher, // Trillingscijfer — vibration-motor Morse + TM1638
  SpectralTuner,     // Spectraalresonantie — 3 rotary encoders
  LivingInterval,    // Levend Interval — 3 encoders, drifting target
  Tetris,            // Reactoroverbelasting — TM1638 + display
  Finale,            // Eindsequentie — recap + celebration, no physical lock
  TiltMaze,          // Zwaartekrachtlabyrint — GY-91 tilt + display (added after Finale to keep earlier values stable)
};

inline constexpr std::uint8_t kPuzzleCount = 8;

inline const char* puzzleName(PuzzleId id) {
  switch (id) {
    case PuzzleId::Pattern: return "Pattern";
    case PuzzleId::ResonantGrid: return "ResonantGrid";
    case PuzzleId::VibrationalCipher: return "VibrationalCipher";
    case PuzzleId::SpectralTuner: return "SpectralTuner";
    case PuzzleId::LivingInterval: return "LivingInterval";
    case PuzzleId::Tetris: return "Tetris";
    case PuzzleId::Finale: return "Finale";
    case PuzzleId::TiltMaze: return "TiltMaze";
  }
  return "Unknown";
}

}  // namespace protocol
