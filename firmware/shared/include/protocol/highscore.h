#pragma once

#include <cstdint>

// Arcade-style highscore entry: 3-letter initials + score. Score is the room
// clock's remaining seconds at the moment all 7 puzzles were solved — higher
// is better, matching "faster clear = higher score". See HighscoreService
// (devices/highscore_service.h) for persistence.

namespace protocol {

inline constexpr std::uint8_t kInitialsLength = 3;
inline constexpr std::uint8_t kMaxHighscores = 10;

struct HighscoreEntry {
  char initials[kInitialsLength + 1] = "---";  // null-terminated, always 3 real chars
  std::uint32_t scoreSeconds = 0;
};

}  // namespace protocol
