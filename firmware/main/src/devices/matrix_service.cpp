#include "devices/matrix_service.h"

namespace {

// (R, G, B) per half, indexed by protocol::GameState.
struct HalfColors {
  std::uint32_t left;
  std::uint32_t right;
};

}  // namespace

void MatrixService::begin() {
  strip_.begin();
  strip_.setBrightness(24);  // deliberate current limit, see spec section 4.1
  strip_.clear();
  strip_.show();
}

void MatrixService::renderGameState(protocol::GameState state) {
  std::uint32_t left = strip_.Color(0, 0, 0);
  std::uint32_t right = strip_.Color(0, 0, 0);

  switch (state) {
    case protocol::GameState::Boot:
      left = right = strip_.Color(32, 32, 32);
      break;
    case protocol::GameState::Idle:
      left = right = strip_.Color(0, 0, 64);
      break;
    case protocol::GameState::Setup:
    case protocol::GameState::Countdown:
      left = right = strip_.Color(64, 64, 0);
      break;
    case protocol::GameState::Active:
      left = strip_.Color(0, 64, 0);
      right = strip_.Color(0, 64, 0);
      break;
    case protocol::GameState::Paused:
      left = right = strip_.Color(64, 32, 0);
      break;
    case protocol::GameState::Timeout:
      left = right = strip_.Color(64, 0, 0);
      break;
    case protocol::GameState::Success:
    case protocol::GameState::HighscoreEntry:
      left = right = strip_.Color(0, 64, 64);
      break;
    case protocol::GameState::Maintenance:
      left = right = strip_.Color(64, 0, 64);
      break;
  }

  for (std::uint16_t i = 0; i < kHalf; ++i) {
    strip_.setPixelColor(i, left);
  }
  for (std::uint16_t i = kHalf; i < kPixelCount; ++i) {
    strip_.setPixelColor(i, right);
  }
  strip_.show();
}

void MatrixService::runTest() {
  for (std::uint16_t i = 0; i < kPixelCount; ++i) {
    strip_.setPixelColor(i, (i % 2 == 0) ? strip_.Color(64, 0, 0) : strip_.Color(0, 64, 0));
  }
  strip_.show();
}
