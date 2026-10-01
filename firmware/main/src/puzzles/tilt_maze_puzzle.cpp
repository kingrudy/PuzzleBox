#include "puzzles/tilt_maze_puzzle.h"

#include <Arduino.h>

#include <algorithm>
#include <cmath>

#include "protocol/audio_cue.h"

namespace {

constexpr std::uint8_t kColsByDifficulty[3] = {6, 8, 10};
constexpr std::uint8_t kRowsByDifficulty[3] = {4, 5, 6};
constexpr std::uint8_t kHolesByDifficulty[3] = {1, 2, 3};
constexpr std::uint32_t kHolePenaltyMsByDifficulty[3] = {5000, 8000, 12000};

// Which accelerometer axis (0=x, 1=y, 2=z) and sign drives the ball along the
// screen's X (right) and Y (down). Depends on how the GY-91 is mounted in the
// box. X: sign -1 because an accelerometer reads the reaction to gravity
// (tilting an axis downward makes it read negative). Y: +1, hardware-
// confirmed -- the sensor's Y axis points up the screen as mounted.
constexpr std::uint8_t kScreenXAxis = 0;
constexpr float kScreenXSign = -1.0f;
constexpr std::uint8_t kScreenYAxis = 1;
constexpr float kScreenYSign = 1.0f;

// Physics, in cell units.
constexpr float kTiltDeadzoneMg = 40.0f;       // ignore hand tremor around the baseline
constexpr float kGainCellsPerS2PerG = 14.0f;   // ~10 deg of tilt -> ~2.4 cells/s^2
constexpr float kFrictionPerS = 1.2f;
constexpr float kMaxSpeed = 4.5f;              // cells/s
constexpr float kBallRadius = 0.22f;
constexpr float kHoleRadius = 0.30f;           // ball centre within this of a hole centre -> falls
constexpr float kExitRadius = 0.30f;
constexpr float kBounce = 0.3f;                // fraction of impact speed kept after hitting a wall
constexpr float kMaxDt = 0.05f;
constexpr float kSubstep = 0.01f;              // keeps a fast ball from tunnelling through a wall
constexpr float kBumpSoundSpeed = 0.8f;
constexpr std::uint32_t kBumpSoundGapMs = 90;

constexpr std::uint8_t kStartCell = 0;

}  // namespace

void TiltMazePuzzle::begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) {
  const std::uint8_t tier = static_cast<std::uint8_t>(difficulty);
  cols_ = kColsByDifficulty[tier];
  rows_ = kRowsByDifficulty[tier];
  holeTarget_ = kHolesByDifficulty[tier];
  holePenaltyMs_ = kHolePenaltyMsByDifficulty[tier];

  generateMaze();
  placeHoles();
  ++layoutId_;

  falls_ = 0;
  pendingPenaltyMs_ = 0;
  solved_ = false;
  haveBaseline_ = false;
  prevEncoderButtons_ = 0;
  lastStepMs_ = ctx.nowMs;
  lastBumpMs_ = 0;
  rewardDigit_ = static_cast<std::uint8_t>(random(8));
  resetBall();
}

std::uint32_t TiltMazePuzzle::takePenaltyMs() {
  const std::uint32_t owed = pendingPenaltyMs_;
  pendingPenaltyMs_ = 0;
  return owed;
}

// --- maze generation ----------------------------------------------------------

bool TiltMazePuzzle::canMove(std::uint8_t cell, std::int8_t dCol, std::int8_t dRow) const {
  const std::int8_t col = cell % cols_;
  const std::int8_t row = cell / cols_;
  if (dCol == 1) return col < cols_ - 1 && (walls_[cell] & kWallEast) == 0;
  if (dCol == -1) return col > 0 && (walls_[cell - 1] & kWallEast) == 0;
  if (dRow == 1) return row < rows_ - 1 && (walls_[cell] & kWallSouth) == 0;
  if (dRow == -1) return row > 0 && (walls_[cell - cols_] & kWallSouth) == 0;
  return false;
}

void TiltMazePuzzle::generateMaze() {
  const std::uint8_t cellCount = cols_ * rows_;
  walls_.fill(0);
  for (std::uint8_t i = 0; i < cellCount; ++i) {
    walls_[i] = kWallEast | kWallSouth;
  }

  // Iterative recursive-backtracker: a perfect maze (exactly one path between
  // any two cells), which is what makes "the exit is the farthest cell" a
  // genuinely long route.
  std::array<bool, kMaxCells> visited{};
  std::array<std::uint8_t, kMaxCells> stack{};
  std::uint8_t depth = 0;
  stack[depth++] = kStartCell;
  visited[kStartCell] = true;

  while (depth > 0) {
    const std::uint8_t cur = stack[depth - 1];
    const std::int8_t col = cur % cols_;
    const std::int8_t row = cur / cols_;
    std::uint8_t options[4];
    std::uint8_t optionCount = 0;
    if (col < cols_ - 1 && !visited[cur + 1]) options[optionCount++] = 0;
    if (col > 0 && !visited[cur - 1]) options[optionCount++] = 1;
    if (row < rows_ - 1 && !visited[cur + cols_]) options[optionCount++] = 2;
    if (row > 0 && !visited[cur - cols_]) options[optionCount++] = 3;
    if (optionCount == 0) {
      --depth;
      continue;
    }
    std::uint8_t next = cur;
    switch (options[random(optionCount)]) {
      case 0: next = cur + 1; walls_[cur] &= ~kWallEast; break;
      case 1: next = cur - 1; walls_[next] &= ~kWallEast; break;
      case 2: next = cur + cols_; walls_[cur] &= ~kWallSouth; break;
      case 3: next = cur - cols_; walls_[next] &= ~kWallSouth; break;
    }
    visited[next] = true;
    stack[depth++] = next;
  }

  std::array<std::uint8_t, kMaxCells> fromStart{};
  bfsFrom(kStartCell, fromStart);
  exitCell_ = kStartCell;
  for (std::uint8_t i = 0; i < cellCount; ++i) {
    if (fromStart[i] > fromStart[exitCell_]) {
      exitCell_ = i;
    }
  }
  bfsFrom(exitCell_, distToExit_);
}

void TiltMazePuzzle::bfsFrom(std::uint8_t start, std::array<std::uint8_t, kMaxCells>& dist) const {
  dist.fill(0xFF);
  std::array<std::uint8_t, kMaxCells> queue{};
  std::uint8_t head = 0;
  std::uint8_t tail = 0;
  dist[start] = 0;
  queue[tail++] = start;
  const std::int8_t dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
  while (head < tail) {
    const std::uint8_t cur = queue[head++];
    for (const auto& d : dirs) {
      if (!canMove(cur, d[0], d[1])) continue;
      const std::uint8_t next = cur + d[0] + d[1] * cols_;
      if (dist[next] != 0xFF) continue;
      dist[next] = dist[cur] + 1;
      queue[tail++] = next;
    }
  }
}

void TiltMazePuzzle::placeHoles() {
  const std::uint8_t cellCount = cols_ * rows_;
  const std::int8_t dirs[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};

  // The solution path: every cell whose distance-to-exit plus distance-from-
  // start equals the start's distance-to-exit. Walk it from the start instead
  // (perfect maze -> unique), stepping to the neighbour one closer to the exit.
  std::array<bool, kMaxCells> onPath{};
  std::uint8_t cur = kStartCell;
  onPath[cur] = true;
  while (cur != exitCell_) {
    for (const auto& d : dirs) {
      if (!canMove(cur, d[0], d[1])) continue;
      const std::uint8_t next = cur + d[0] + d[1] * cols_;
      if (distToExit_[next] + 1 == distToExit_[cur]) {
        cur = next;
        break;
      }
    }
    onPath[cur] = true;
  }

  // Dead ends off the path; those right next to the path come first.
  std::array<std::uint8_t, kMaxCells> nearPath{};
  std::array<std::uint8_t, kMaxCells> farPath{};
  std::uint8_t nearCount = 0;
  std::uint8_t farCount = 0;
  for (std::uint8_t i = 0; i < cellCount; ++i) {
    if (onPath[i]) continue;
    std::uint8_t exits = 0;
    bool touchesPath = false;
    for (const auto& d : dirs) {
      if (!canMove(i, d[0], d[1])) continue;
      ++exits;
      touchesPath = touchesPath || onPath[i + d[0] + d[1] * cols_];
    }
    if (exits != 1) continue;
    if (touchesPath) {
      nearPath[nearCount++] = i;
    } else {
      farPath[farCount++] = i;
    }
  }
  auto shuffle = [](std::array<std::uint8_t, kMaxCells>& a, std::uint8_t n) {
    for (std::uint8_t i = n; i > 1; --i) {
      std::swap(a[i - 1], a[random(i)]);
    }
  };
  shuffle(nearPath, nearCount);
  shuffle(farPath, farCount);

  holeCount_ = 0;
  for (std::uint8_t i = 0; i < nearCount && holeCount_ < holeTarget_; ++i) {
    holes_[holeCount_++] = nearPath[i];
  }
  for (std::uint8_t i = 0; i < farCount && holeCount_ < holeTarget_; ++i) {
    holes_[holeCount_++] = farPath[i];
  }
}

void TiltMazePuzzle::resetBall() {
  ballX_ = (kStartCell % cols_) + 0.5f;
  ballY_ = (kStartCell / cols_) + 0.5f;
  velX_ = 0.0f;
  velY_ = 0.0f;
}

// --- physics ------------------------------------------------------------------

bool TiltMazePuzzle::readTilt(float& tiltX, float& tiltY) {
  if (!imu_.online()) {
    return false;
  }
  const ImuService::Sample& s = imu_.sample();
  const std::int32_t a[3] = {s.ax, s.ay, s.az};
  if (!haveBaseline_) {
    baseline_[0] = a[0];
    baseline_[1] = a[1];
    baseline_[2] = a[2];
    haveBaseline_ = true;
  }
  auto deadzone = [](float mg) {
    if (std::fabs(mg) < kTiltDeadzoneMg) return 0.0f;
    return mg > 0 ? mg - kTiltDeadzoneMg : mg + kTiltDeadzoneMg;
  };
  tiltX = deadzone(kScreenXSign * static_cast<float>(a[kScreenXAxis] - baseline_[kScreenXAxis]));
  tiltY = deadzone(kScreenYSign * static_cast<float>(a[kScreenYAxis] - baseline_[kScreenYAxis]));
  return true;
}

void TiltMazePuzzle::collideSegment(float x1, float y1, float x2, float y2, float& impact) {
  const float px = std::min(std::max(ballX_, std::min(x1, x2)), std::max(x1, x2));
  const float py = std::min(std::max(ballY_, std::min(y1, y2)), std::max(y1, y2));
  const float dx = ballX_ - px;
  const float dy = ballY_ - py;
  const float d2 = dx * dx + dy * dy;
  if (d2 >= kBallRadius * kBallRadius || d2 < 1e-8f) {
    return;
  }
  const float d = std::sqrt(d2);
  const float nx = dx / d;
  const float ny = dy / d;
  ballX_ += nx * (kBallRadius - d);
  ballY_ += ny * (kBallRadius - d);
  const float vn = velX_ * nx + velY_ * ny;
  if (vn < 0.0f) {
    velX_ -= (1.0f + kBounce) * vn * nx;
    velY_ -= (1.0f + kBounce) * vn * ny;
    impact = std::max(impact, -vn);
  }
}

void TiltMazePuzzle::step(float dt, float accelX, float accelY, float& impact) {
  velX_ += accelX * dt;
  velY_ += accelY * dt;
  const float damping = std::max(0.0f, 1.0f - kFrictionPerS * dt);
  velX_ *= damping;
  velY_ *= damping;
  const float speed = std::sqrt(velX_ * velX_ + velY_ * velY_);
  if (speed > kMaxSpeed) {
    velX_ *= kMaxSpeed / speed;
    velY_ *= kMaxSpeed / speed;
  }
  ballX_ += velX_ * dt;
  ballY_ += velY_ * dt;

  // Resolve against every wall segment around the ball's cell, as segments
  // (not per-axis clamps), so wall ends/corners push the ball out too.
  const int cx = static_cast<int>(ballX_);
  const int cy = static_cast<int>(ballY_);
  for (int r = cy - 1; r <= cy + 1; ++r) {
    for (int c = cx - 1; c <= cx + 1; ++c) {
      if (r < 0 || c < 0 || r >= rows_ || c >= cols_) continue;
      const std::uint8_t cell = static_cast<std::uint8_t>(r * cols_ + c);
      if (c == cols_ - 1 || (walls_[cell] & kWallEast)) collideSegment(c + 1, r, c + 1, r + 1, impact);
      if (r == rows_ - 1 || (walls_[cell] & kWallSouth)) collideSegment(c, r + 1, c + 1, r + 1, impact);
      if (c == 0) collideSegment(0, r, 0, r + 1, impact);
      if (r == 0) collideSegment(c, 0, c + 1, 0, impact);
    }
  }
  ballX_ = std::min(std::max(ballX_, kBallRadius), cols_ - kBallRadius);
  ballY_ = std::min(std::max(ballY_, kBallRadius), rows_ - kBallRadius);
}

void TiltMazePuzzle::poll(puzzles::PuzzleContext& ctx) {
  if (solved_) {
    return;
  }

  // Any encoder button re-levels: "this is flat now".
  std::uint8_t buttons = 0;
  for (std::uint8_t i = 0; i < 3; ++i) {
    if (ctx.panel.encoder(i).buttonPressed) buttons |= 1u << i;
  }
  if ((buttons & ~prevEncoderButtons_) != 0) {
    haveBaseline_ = false;
    ctx.audio.playCue(protocol::AudioCueId::ShortBeep);
  }
  prevEncoderButtons_ = buttons;

  float dt = (ctx.nowMs - lastStepMs_) / 1000.0f;
  lastStepMs_ = ctx.nowMs;
  dt = std::min(dt, kMaxDt);

  float tiltX = 0.0f;
  float tiltY = 0.0f;
  if (!readTilt(tiltX, tiltY)) {
    velX_ = 0.0f;
    velY_ = 0.0f;
  } else {
    const float accelX = tiltX / 1000.0f * kGainCellsPerS2PerG;
    const float accelY = tiltY / 1000.0f * kGainCellsPerS2PerG;
    float impact = 0.0f;
    while (dt > 0.0f) {
      const float h = std::min(dt, kSubstep);
      step(h, accelX, accelY, impact);
      dt -= h;
    }
    if (impact > kBumpSoundSpeed && ctx.nowMs - lastBumpMs_ >= kBumpSoundGapMs) {
      lastBumpMs_ = ctx.nowMs;
      ctx.audio.playTone(160.0f + impact * 40.0f, 40);
    }
  }

  for (std::uint8_t i = 0; i < holeCount_; ++i) {
    const float hx = holes_[i] % cols_ + 0.5f;
    const float hy = holes_[i] / cols_ + 0.5f;
    const float dx = ballX_ - hx;
    const float dy = ballY_ - hy;
    if (dx * dx + dy * dy < kHoleRadius * kHoleRadius) {
      ++falls_;
      pendingPenaltyMs_ += holePenaltyMs_;
      ctx.vibration.pulse(400);
      ctx.audio.playCue(protocol::AudioCueId::Error);
      resetBall();
      break;
    }
  }

  const float ex = exitCell_ % cols_ + 0.5f;
  const float ey = exitCell_ / cols_ + 0.5f;
  if ((ballX_ - ex) * (ballX_ - ex) + (ballY_ - ey) * (ballY_ - ey) < kExitRadius * kExitRadius) {
    solved_ = true;
    ctx.audio.playCue(protocol::AudioCueId::Success);
  }

  // TM1638: "KERN" + distance-to-core / total, LEDs as a progress bar.
  const std::uint8_t cell = static_cast<std::uint8_t>(static_cast<int>(ballY_) * cols_ + static_cast<int>(ballX_));
  const std::uint8_t total = distToExit_[kStartCell];
  const std::uint8_t remaining = distToExit_[cell];
  const std::uint8_t lit = total == 0 ? 8 : static_cast<std::uint8_t>((total - std::min(remaining, total)) * 8 / total);
  const std::uint8_t ledMask = lit >= 8 ? 0xFF : static_cast<std::uint8_t>((1u << lit) - 1);
  ctx.panel.renderTetrisPuzzle("KERN", remaining, total, ledMask, false);
}
