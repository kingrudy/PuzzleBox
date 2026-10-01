#pragma once

#include <array>
#include <cstdint>

#include "devices/imu_service.h"
#include "puzzles/puzzle.h"

// Zwaartekrachtlabyrint (tilt maze). A ball rolls through a maze on the main
// display as the players tilt the whole box (GY-91 accelerometer via
// ImuService). Reach the core (exit) to solve; rolling into a hole costs time
// and puts the ball back at the start.
//
// Physics is authoritative here on the main controller (same split as
// Tetris); the display only renders walls/holes/exit once per layout and
// moves the ball. A fresh maze is generated every begin() (recursive
// backtracker), the exit is the cell farthest from the start, and holes sit
// in dead ends that branch directly off the solution path, so overshooting a
// junction is what drops you in.
//
// Tilt is measured relative to a baseline captured when the puzzle starts
// (and re-captured on any encoder button press), not against true level: the
// GY-91 on this box reads ~1.7 g at rest (uncalibrated offsets), and the
// sensor's mounting angle in the box is arbitrary anyway.
class TiltMazePuzzle : public puzzles::Puzzle {
 public:
  static constexpr std::uint8_t kMaxCols = 10;
  static constexpr std::uint8_t kMaxRows = 6;
  static constexpr std::uint8_t kMaxCells = kMaxCols * kMaxRows;
  static constexpr std::uint8_t kMaxHoles = 3;
  // Per-cell wall bits; the outer border is implicit.
  static constexpr std::uint8_t kWallEast = 0x01;
  static constexpr std::uint8_t kWallSouth = 0x02;

  explicit TiltMazePuzzle(const ImuService& imu) : imu_(imu) {}

  void begin(puzzles::PuzzleContext& ctx, puzzles::Difficulty difficulty) override;
  void poll(puzzles::PuzzleContext& ctx) override;
  bool isSolved() const override { return solved_; }
  protocol::PuzzleId id() const override { return protocol::PuzzleId::TiltMaze; }
  std::uint8_t rewardDigit() const override { return rewardDigit_; }
  std::uint32_t takePenaltyMs() override;

  // Display-facing state.
  std::uint32_t layoutId() const { return layoutId_; }
  std::uint8_t cols() const { return cols_; }
  std::uint8_t rows() const { return rows_; }
  std::uint8_t walls(std::uint8_t cell) const { return walls_[cell]; }
  std::uint8_t holeCount() const { return holeCount_; }
  std::uint8_t hole(std::uint8_t i) const { return holes_[i]; }
  std::uint8_t exitCell() const { return exitCell_; }
  float ballX() const { return ballX_; }  // in cell units, 0..cols
  float ballY() const { return ballY_; }
  std::uint8_t falls() const { return falls_; }
  bool sensorOnline() const { return imu_.online(); }

 private:
  bool canMove(std::uint8_t cell, std::int8_t dCol, std::int8_t dRow) const;
  void generateMaze();
  void bfsFrom(std::uint8_t start, std::array<std::uint8_t, kMaxCells>& dist) const;
  void placeHoles();
  void resetBall();
  void step(float dt, float accelX, float accelY, float& impact);
  void collideSegment(float x1, float y1, float x2, float y2, float& impact);
  bool readTilt(float& tiltX, float& tiltY);

  const ImuService& imu_;

  std::uint8_t cols_ = 8;
  std::uint8_t rows_ = 5;
  std::array<std::uint8_t, kMaxCells> walls_{};
  std::array<std::uint8_t, kMaxCells> distToExit_{};
  std::array<std::uint8_t, kMaxHoles> holes_{};
  std::uint8_t holeCount_ = 0;
  std::uint8_t holeTarget_ = 2;
  std::uint8_t exitCell_ = 0;
  std::uint32_t layoutId_ = 0;

  float ballX_ = 0.5f;
  float ballY_ = 0.5f;
  float velX_ = 0.0f;
  float velY_ = 0.0f;
  std::uint32_t lastStepMs_ = 0;
  std::uint32_t lastBumpMs_ = 0;

  bool haveBaseline_ = false;
  std::int32_t baseline_[3] = {0, 0, 0};
  std::uint8_t prevEncoderButtons_ = 0;

  std::uint8_t falls_ = 0;
  std::uint32_t holePenaltyMs_ = 8000;
  std::uint32_t pendingPenaltyMs_ = 0;
  bool solved_ = false;
  std::uint8_t rewardDigit_ = 0;
};
