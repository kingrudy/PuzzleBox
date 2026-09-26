#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include <strings.h>  // strcasecmp

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Small persistent event log, shared by every node (main/sidecar/display).
// Every call to logf() does three things: prints to Serial (unchanged
// behaviour from before this existed), appends to a RAM ring buffer, and
// rewrites that ring buffer to NVS flash (via the Arduino Preferences API,
// same mechanism as HighscoreService) so the history survives a reboot or
// power loss. That's the "what actually happened" trail for troubleshooting
// a box that's already sealed up and running somewhere without a laptop
// attached.
//
// Deliberately NOT wired up for high-frequency/periodic output (e.g. a 5s
// heap-diagnostic heartbeat, or a poll that fires every few hundred ms) —
// each logf() call is a full NVS blob rewrite, so only discrete, meaningful
// events (boot, state changes, connectivity changes, scans, errors) should
// go through this. Keep chatty/periodic Serial output Serial-only. See
// troubleshoot.md for the full writeup.
namespace diagnostics {

class EventLog {
 public:
  static constexpr std::uint8_t kMaxEntries = 40;
  static constexpr std::uint8_t kTagLen = 12;      // includes null terminator
  static constexpr std::uint8_t kMessageLen = 40;  // includes null terminator

  struct Entry {
    std::uint32_t seq = 0;
    std::uint32_t timestampMs = 0;  // millis() at log time; resets to 0 every boot
    char tag[kTagLen] = {0};
    char message[kMessageLen] = {0};
  };

  // namespaceName becomes the NVS namespace (Preferences limits this to 15
  // chars) -- pass a short per-node name, e.g. "evtlog_main".
  void begin(const char* namespaceName) {
    prefs_.begin(namespaceName, /*readOnly=*/false);

    count_ = prefs_.getUChar(kCountKey, 0);
    nextIndex_ = prefs_.getUChar(kIndexKey, 0);
    nextSeq_ = prefs_.getUInt(kSeqKey, 0);

    if (count_ > kMaxEntries || nextIndex_ >= kMaxEntries) {
      count_ = 0;
      nextIndex_ = 0;
    }

    if (count_ > 0) {
      const size_t expectedBytes = sizeof(Entry) * kMaxEntries;
      const size_t got = prefs_.getBytes(kBlobKey, entries_.data(), expectedBytes);
      if (got != expectedBytes) {
        // Corrupt or missing blob -- start clean rather than trusting partial data.
        count_ = 0;
        nextIndex_ = 0;
        entries_ = {};
      }
    }
  }

  // printf-style. Prints to Serial and persists to NVS. Keep call sites to
  // discrete events, not periodic/high-frequency output (see class comment).
  void logf(const char* tag, const char* fmt, ...) {
    Entry& e = entries_[nextIndex_];
    e.seq = nextSeq_++;
    e.timestampMs = millis();
    std::strncpy(e.tag, tag, kTagLen - 1);
    e.tag[kTagLen - 1] = '\0';

    va_list args;
    va_start(args, fmt);
    vsnprintf(e.message, kMessageLen, fmt, args);
    va_end(args);

    Serial.printf("[%s] %s\n", e.tag, e.message);

    nextIndex_ = static_cast<std::uint8_t>((nextIndex_ + 1) % kMaxEntries);
    if (count_ < kMaxEntries) {
      ++count_;
    }
    save();
  }

  // Oldest-first iteration, for both dumpToSerial() and callers that want to
  // build their own representation (e.g. AppController's GET /api/logs JSON).
  template <typename Fn>
  void forEachOldestFirst(Fn&& fn) const {
    if (count_ == 0) {
      return;
    }
    const std::uint8_t start = (count_ < kMaxEntries) ? 0 : nextIndex_;
    for (std::uint8_t i = 0; i < count_; ++i) {
      const std::uint8_t idx = static_cast<std::uint8_t>((start + i) % kMaxEntries);
      fn(entries_[idx]);
    }
  }

  void dumpToSerial() const {
    Serial.printf("---- event log: %u/%u entries ----\n", count_, kMaxEntries);
    forEachOldestFirst([](const Entry& e) {
      Serial.printf("#%-4u t+%8lums [%-10s] %s\n", e.seq, static_cast<unsigned long>(e.timestampMs), e.tag,
                    e.message);
    });
    Serial.println("---- end event log ----");
  }

  // Non-blocking; call once per tick()/loop(). Typing "log" (or "logs") into
  // the Serial monitor and pressing enter dumps the persisted history.
  void pollSerialCommand() {
    while (Serial.available() > 0) {
      const char c = static_cast<char>(Serial.read());
      if (c == '\n' || c == '\r') {
        if (cmdLen_ > 0) {
          cmdBuf_[cmdLen_] = '\0';
          if (strcasecmp(cmdBuf_, "log") == 0 || strcasecmp(cmdBuf_, "logs") == 0) {
            dumpToSerial();
          }
          cmdLen_ = 0;
        }
        continue;
      }
      if (cmdLen_ < sizeof(cmdBuf_) - 1) {
        cmdBuf_[cmdLen_++] = c;
      }
    }
  }

  std::uint8_t count() const { return count_; }

 private:
  void save() {
    prefs_.putBytes(kBlobKey, entries_.data(), sizeof(Entry) * kMaxEntries);
    prefs_.putUChar(kCountKey, count_);
    prefs_.putUChar(kIndexKey, nextIndex_);
    prefs_.putUInt(kSeqKey, nextSeq_);
  }

  static constexpr char kCountKey[] = "count";
  static constexpr char kIndexKey[] = "index";
  static constexpr char kSeqKey[] = "seq";
  static constexpr char kBlobKey[] = "blob";

  Preferences prefs_;
  std::array<Entry, kMaxEntries> entries_{};
  std::uint8_t count_ = 0;
  std::uint8_t nextIndex_ = 0;  // where the next entry will be written
  std::uint32_t nextSeq_ = 0;

  char cmdBuf_[16] = {0};
  std::uint8_t cmdLen_ = 0;
};

}  // namespace diagnostics
