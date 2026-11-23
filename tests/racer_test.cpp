#include "RacerGame.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <iostream>

namespace {
struct Picker {
  uint32_t& state;

  int operator()(int limit) const {
    state = state * 1664525u + 1013904223u;
    return static_cast<int>((state >> 8) % static_cast<uint32_t>(limit));
  }
};

int middleLane(int limit) {
  return limit == 3 ? 1 : 0;
}

uint32_t tickTime(uint32_t tick) {
  return (tick * 1000000u + 59u) / 60u;
}

void assertSameGame(const RacerGame& left, const RacerGame& right) {
  assert(left.playerX() == right.playerX());
  assert(left.roadOffset() == right.roadOffset());
  assert(left.score() == right.score());
  assert(left.crashed() == right.crashed());
  for (int i = 0; i < RacerGame::ObstacleCount; ++i) {
    assert(left.obstacle(i).x == right.obstacle(i).x);
    assert(left.obstacle(i).y == right.obstacle(i).y);
  }
}

void assertFairGaps(const RacerGame& game) {
  std::array<float, RacerGame::ObstacleCount> y;
  for (int i = 0; i < RacerGame::ObstacleCount; ++i) {
    y[i] = game.obstacle(i).y;
    assert(game.obstacle(i).x >= RacerGame::RoadLeft);
    assert(game.obstacle(i).x + RacerGame::CarWidth <= RacerGame::RoadRight);
  }
  std::sort(y.begin(), y.end());
  for (size_t i = 1; i < y.size(); ++i) {
    const float gap = y[i] - y[i - 1];
    assert(gap >= RacerGame::MinimumGap - 0.001f);
    assert(gap <= RacerGame::MaximumGap + 0.001f);
  }
}

void testCrashAndReset() {
  RacerGame game;
  for (uint32_t start : {0u, UINT32_MAX - 10000u, 5000000u}) {
    game.reset(start, middleLane);
    assert(!game.crashed());
    assert(game.playerX() == 27);
    assert(game.score() == 0);
    assert(game.roadOffset() == 0);
    assert(!game.due(start + 16666u));
    assert(game.due(start + 16667u));

    unsigned crashCount = 0;
    for (uint32_t tick = 1; tick < 180; ++tick) {
      if (game.advance(start + tickTime(tick), 0, middleLane)) {
        ++crashCount;
        assert(game.crashed());
        assert(game.obstacle(0).y >= RacerGame::PlayerY - RacerGame::CarHeight + 1);
        assert(game.obstacle(0).y < RacerGame::PlayerY);
      }
    }
    assert(crashCount == 1);
    assert(game.score() == 0);
  }
}

void testWheelCollision() {
  for (float target : {17.1f, 15.1f}) {
    RacerGame game;
    game.reset(0, middleLane);
    for (uint32_t tick = 1; tick <= 130; ++tick) {
      const float steering = (target - game.playerX()) * 60 / RacerGame::SteeringSpeed;
      game.advance(tickTime(tick), std::max(-1.0f, std::min(1.0f, steering)), middleLane);
    }
    assert(game.crashed() == (target > 16));
    assert(game.score() == (target > 16 ? 0 : 1));
  }
}

void testCadenceAndStall() {
  RacerGame perTick;
  RacerGame batched;
  uint32_t perTickSeed = 17;
  uint32_t batchedSeed = perTickSeed;
  const Picker perTickPick{perTickSeed};
  const Picker batchedPick{batchedSeed};
  perTick.reset(0, perTickPick);
  batched.reset(0, batchedPick);
  for (uint32_t tick = 1; tick <= 600; ++tick) {
    perTick.advance(tickTime(tick), -1, perTickPick);
    if (tick % 3 == 0) batched.advance(tickTime(tick), -1, batchedPick);
  }
  assertSameGame(perTick, batched);
  assert(perTick.playerX() == RacerGame::PlayerMinX);
  assert(perTickSeed == batchedSeed);

  RacerGame stalled;
  RacerGame sixTicks;
  stalled.reset(0, middleLane);
  sixTicks.reset(0, middleLane);
  stalled.advance(3000000, 1, middleLane);
  for (uint32_t tick = 1; tick <= 6; ++tick) {
    sixTicks.advance(tickTime(tick), 1, middleLane);
  }
  assertSameGame(stalled, sixTicks);
  assert(!stalled.due(3000000));
  assert(stalled.due(3016667));
}

void testAvoidanceAndScoring() {
  for (uint32_t seed = 1; seed <= 10; ++seed) {
    uint32_t state = seed;
    const Picker pick{state};
    RacerGame game;
    game.reset(0, pick);
    int previousScore = 0;
    for (uint32_t tick = 1; tick <= 2400; ++tick) {
      const RacerGame::Obstacle* leading = &game.obstacle(0);
      for (int i = 1; i < RacerGame::ObstacleCount; ++i) {
        if (game.obstacle(i).y > leading->y) leading = &game.obstacle(i);
      }
      const int target = leading->x == 27 ? 9 : 27;
      const float distance = target - game.playerX();
      const int steering = distance > 2.0f ? 1 : distance < -2.0f ? -1 : 0;
      game.advance(tickTime(tick), steering, pick);
      assert(!game.crashed());
      assert(game.score() == previousScore || game.score() == previousScore + 1);
      assertFairGaps(game);
      previousScore = game.score();
    }
    assert(game.score() > 40);
  }
}
}  // namespace

int main() {
  testCrashAndReset();
  testWheelCollision();
  testCadenceAndStall();
  testAvoidanceAndScoring();
  std::cout << "Racer regression tests passed\n";
}
