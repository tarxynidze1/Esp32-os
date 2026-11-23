#ifndef RACER_GAME_H
#define RACER_GAME_H

#include <limits.h>
#include <stdint.h>

class RacerGame {
 public:
  struct Obstacle {
    int x;
    float y;
  };

  enum {
    ObstacleCount = 3,
    RoadLeft = 4,
    RoadRight = 59,
    RoadTop = 12,
    RoadBottom = 127,
    StripeSpacing = 20,
    CarWidth = 10,
    CarHeight = 16,
    PlayerY = 108,
    PlayerMinX = RoadLeft + 2,
    PlayerMaxX = RoadRight - CarWidth - 1,
    MinimumGap = 60,
    MaximumGap = 75,
    MaximumSpeed = 200,
    SteeringSpeed = 156
  };

  template <typename Pick>
  void reset(uint32_t now, Pick pick) {
    playerX_ = 27;
    score_ = 0;
    crashed_ = false;
    roadOffset_ = 0;
    startClock(now);
    float y = -28;
    for (auto& car : obstacles_) {
      car = {laneX(pick(3)), y};
      y -= MinimumGap + pick(MaximumGap - MinimumGap + 1);
    }
  }

  void startClock(uint32_t now) {
    lastUpdate_ = now;
    phase_ = 0;
  }

  bool due(uint32_t now) const {
    return phase_ + elapsed(now) * TicksPerSecond >= MicrosPerSecond;
  }

  // Scale elapsed microseconds by the tick rate to avoid rounding the period.
  // Unsigned subtraction handles micros() rollover, and the cap bounds catch-up.
  template <typename Pick>
  bool advance(uint32_t now, float steering, Pick pick) {
    if (crashed_ || !due(now)) return false;
    phase_ += elapsed(now) * TicksPerSecond;
    lastUpdate_ = now;
    while (phase_ >= MicrosPerSecond) {
      phase_ -= MicrosPerSecond;
      if (step(steering, pick)) return true;
    }
    return false;
  }

  float playerX() const { return playerX_; }
  float roadOffset() const { return roadOffset_; }
  int score() const { return score_; }
  bool crashed() const { return crashed_; }
  const Obstacle& obstacle(int index) const { return obstacles_[index]; }

 private:
  enum {
    TicksPerSecond = 60,
    MicrosPerSecond = 1000000,
    MaxElapsed = 100000,
    StartingSpeed = 86,
    SpeedIncrease = 5
  };

  Obstacle obstacles_[ObstacleCount] = {};
  float playerX_ = 27;
  float roadOffset_ = 0;
  int score_ = 0;
  bool crashed_ = false;
  uint32_t lastUpdate_ = 0;
  uint32_t phase_ = 0;

  static int laneX(int lane) { return 9 + 18 * lane; }

  uint32_t elapsed(uint32_t now) const {
    const uint32_t delta = now - lastUpdate_;
    return delta < MaxElapsed ? delta : static_cast<uint32_t>(MaxElapsed);
  }

  // The rounded nose and wheel spans match the outline drawn on the OLED.
  static int carInset(int row) {
    if (row == 0 || row == CarHeight - 1) return 3;
    if (row == 1 || row == CarHeight - 2) return 2;
    if ((row >= 2 && row < 6) || (row >= 10 && row < 14)) return 0;
    return 1;
  }

  static bool collides(int playerX, int obstacleX, int obstacleY) {
    if (playerX + CarWidth + 1 < obstacleX || obstacleX + CarWidth + 1 < playerX) {
      return false;
    }
    const int top = obstacleY > PlayerY ? obstacleY : PlayerY;
    const int obstacleBottom = obstacleY + CarHeight;
    const int playerBottom = PlayerY + CarHeight;
    const int bottom = obstacleBottom < playerBottom ? obstacleBottom : playerBottom;
    for (int y = top; y < bottom; ++y) {
      const int insets = carInset(y - PlayerY) + carInset(y - obstacleY);
      if (playerX - obstacleX <= CarWidth + 1 - insets &&
          obstacleX - playerX <= CarWidth + 1 - insets) {
        return true;
      }
    }
    return false;
  }

  template <typename Pick>
  bool step(float steering, Pick pick) {
    const float previousX = playerX_;
    playerX_ += steering * SteeringSpeed / TicksPerSecond;
    if (playerX_ < PlayerMinX) playerX_ = PlayerMinX;
    if (playerX_ > PlayerMaxX) playerX_ = PlayerMaxX;
    int speed = MaximumSpeed;
    if (score_ <= (MaximumSpeed - StartingSpeed) / SpeedIncrease) {
      speed = StartingSpeed + SpeedIncrease * score_;
    }
    const float distance = static_cast<float>(speed) / TicksPerSecond;

    // At top speed a car moves less than four pixels per tick. Check at
    // subpixel intervals to catch contact during diagonal movement.
    const int slices = 4;
    for (int slice = 1; slice <= slices; ++slice) {
      const float progress = static_cast<float>(slice) / slices;
      const int sampleX = static_cast<int>(previousX + (playerX_ - previousX) * progress);
      const float partialDistance = distance * progress;
      for (const auto& car : obstacles_) {
        if (collides(sampleX, car.x, static_cast<int>(car.y + partialDistance))) {
          playerX_ = sampleX;
          moveRoad(partialDistance);
          crashed_ = true;
          return true;
        }
      }
    }

    moveRoad(distance);
    for (auto& car : obstacles_) {
      if (car.y < RoadBottom + 1) continue;
      if (score_ < INT_MAX) ++score_;
      float tail = car.y;
      for (const auto& other : obstacles_) {
        if (other.y < tail) tail = other.y;
      }
      // Common speed preserves the gap. The next car leaves enough room to
      // move one lane after the previous car clears, even at maximum speed.
      car = {laneX(pick(3)), tail - MinimumGap - pick(MaximumGap - MinimumGap + 1)};
    }
    return false;
  }

  void moveRoad(float distance) {
    for (auto& car : obstacles_) car.y += distance;
    roadOffset_ += distance;
    if (roadOffset_ >= StripeSpacing) roadOffset_ -= StripeSpacing;
  }
};

#endif
