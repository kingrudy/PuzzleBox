#pragma once

#include <array>
#include <cstdint>

#include <Preferences.h>

#include "protocol/highscore.h"

// Top-10 arcade highscore list, persisted in NVS flash (via the Arduino
// Preferences API) so it survives a reboot. Sorted descending by score —
// index 0 is the best run.
class HighscoreService {
 public:
  void begin();  // loads from NVS

  const std::array<protocol::HighscoreEntry, protocol::kMaxHighscores>& entries() const {
    return entries_;
  }
  std::uint8_t count() const { return count_; }

  // True if scoreSeconds would enter the top 10 (or the list isn't full yet).
  bool qualifies(std::uint32_t scoreSeconds) const;

  // Inserts in sorted position, truncates to kMaxHighscores, persists to NVS.
  // initials must be exactly protocol::kInitialsLength characters.
  void submit(const char* initials, std::uint32_t scoreSeconds);

 private:
  void save();

  Preferences prefs_;
  std::array<protocol::HighscoreEntry, protocol::kMaxHighscores> entries_{};
  std::uint8_t count_ = 0;
};
