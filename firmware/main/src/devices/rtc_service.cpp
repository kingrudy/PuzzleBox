#include "devices/rtc_service.h"

namespace {
constexpr unsigned long kRefreshIntervalMs = 1000;
}

void RtcService::begin() {
  if (ds3231_.begin()) {
    kind_ = Kind::kDs3231;
    ready_ = true;
  } else if (ds1307_.begin()) {
    kind_ = Kind::kDs1307;
    ready_ = true;
  } else {
    kind_ = Kind::kNone;
    ready_ = false;
    formattedNow_ = "niet beschikbaar";
  }
}

void RtcService::poll() {
  if (!ready_) {
    return;
  }

  const unsigned long now = millis();
  if (now - lastRefreshMs_ < kRefreshIntervalMs) {
    return;
  }
  lastRefreshMs_ = now;

  DateTime dt = (kind_ == Kind::kDs3231) ? ds3231_.now() : ds1307_.now();

  char buf[17];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", dt.year(), dt.month(), dt.day(),
           dt.hour(), dt.minute());
  formattedNow_ = buf;
}
