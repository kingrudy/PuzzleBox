#include "devices/highscore_service.h"

#include <cstring>

namespace {
constexpr char kNamespace[] = "chronolab";
constexpr char kCountKey[] = "hs_count";
constexpr char kBlobKey[] = "hs_blob";
}  // namespace

void HighscoreService::begin() {
  prefs_.begin(kNamespace, /*readOnly=*/false);

  count_ = prefs_.getUChar(kCountKey, 0);
  if (count_ > protocol::kMaxHighscores) {
    count_ = protocol::kMaxHighscores;
  }

  if (count_ > 0) {
    const size_t expectedBytes = sizeof(protocol::HighscoreEntry) * count_;
    const size_t got = prefs_.getBytes(kBlobKey, entries_.data(), expectedBytes);
    if (got != expectedBytes) {
      // Corrupt or missing blob — start clean rather than trusting partial data.
      count_ = 0;
      entries_ = {};
    }
  }
}

bool HighscoreService::qualifies(std::uint32_t scoreSeconds) const {
  if (count_ < protocol::kMaxHighscores) {
    return true;
  }
  return scoreSeconds > entries_[count_ - 1].scoreSeconds;
}

void HighscoreService::submit(const char* initials, std::uint32_t scoreSeconds) {
  if (!qualifies(scoreSeconds)) {
    return;
  }

  protocol::HighscoreEntry entry;
  std::strncpy(entry.initials, initials, protocol::kInitialsLength);
  entry.initials[protocol::kInitialsLength] = '\0';
  entry.scoreSeconds = scoreSeconds;

  std::uint8_t insertAt = count_;
  for (std::uint8_t i = 0; i < count_; ++i) {
    if (scoreSeconds > entries_[i].scoreSeconds) {
      insertAt = i;
      break;
    }
  }

  const std::uint8_t newCount =
      static_cast<std::uint8_t>(count_ < protocol::kMaxHighscores ? count_ + 1 : protocol::kMaxHighscores);
  for (std::uint8_t i = newCount; i > insertAt + 1; --i) {
    entries_[i - 1] = entries_[i - 2];
  }
  entries_[insertAt] = entry;
  count_ = newCount;

  save();
}

void HighscoreService::save() {
  prefs_.putUChar(kCountKey, count_);
  prefs_.putBytes(kBlobKey, entries_.data(), sizeof(protocol::HighscoreEntry) * count_);
}
