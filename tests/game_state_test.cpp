#include <algorithm>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

using byte = uint8_t;
using std::max;
using std::min;

bool btnState[5] = {};
unsigned long testClock = 0;
int hiTetris = 0, hiBlast = 0, hiSnake = 0;
unsigned long millis() { return testClock; }
bool bp(byte input) { return btnState[input]; }
long random(long limit) {
  assert(limit > 0);
  return 0;
}
long random(long first, long limit) { return first + random(limit - first); }
#define PROGMEM
constexpr double PI = 3.14159265358979323846;
uint8_t pgm_read_byte(const uint8_t* address) { return *address; }
uint8_t frameBuffer[1024] = {};
uint8_t* display_buf = frameBuffer;
struct FakeDisplay {
  void invertDisplay(bool) {}
} display;
void saveScore(int, int& high, int score) { high = max(high, score); }

#include "game_logic_under_test.h"

namespace {
void testTetris() {
  tetrisInit();
  tTy = 3;
  tX = 3;
  tY = 18;
  tBrd[18] = tBrd[19] = 0x03FFu & ~(3u << 3);
  tBrd[17] = 1;
  assert(!tCol(tX, tY, tRot));
  assert(tCol(tX, tY + 1, tRot));
  tMerge();
  assert(tSc == 20);
  assert(tBrd[19] == 1);
  assert(tBrd[18] == 0);
  assert(tBrd[0] == 0);

  tTy = 0;
  tX = 7;
  tRot = 1;
  btnState[0] = true;
  loopTetris();
  btnState[0] = false;
  assert(tX == 6 && tRot == 2);

  tBrd[1] = 0x03FF;
  tSpawn();
  assert(tDead);
  assert(hiTetris == 20);
  btnState[4] = true;
  loopTetris();
  btnState[4] = false;
  assert(!tDead && tSc == 0 && tBrd[1] == 0);
}

void testBlocks() {
  blocksInit();
  for (int cell = 0; cell < 8; ++cell) {
    grid[4][cell] = 1;
    grid[cell][2] = 1;
  }
  grid[3][3] = 1;
  bCheckLines();
  for (int cell = 0; cell < 8; ++cell) {
    assert(!grid[4][cell]);
    assert(!grid[cell][2]);
  }
  assert(grid[3][3] == 1 && bScore == 250);

  memset(grid, 1, sizeof(grid));
  grid[7][7] = 0;
  activeShapes[0] = 6;
  activeShapes[1] = 0;
  assert(!bCanPlace(8, 7, 0));
  assert(bCanPlace(7, 7, 0));
  bSelectNextShape();
  assert(bState == B_SELECT && selectedShapeIdx == 1);
  activeShapes[1] = -1;
  bSelectNextShape();
  assert(bState == B_GAMEOVER && hiBlast == 250);
  btnState[4] = true;
  loopBlocks();
  btnState[4] = false;
  assert(bState == B_GENERATE && bScore == 0 && !grid[0][0]);
}

void testSnake() {
  snakeInit();
  const SnakePt head = snk[0];
  btnState[2] = true;
  loopSnake();
  btnState[2] = false;
  btnState[1] = true;
  loopSnake();
  btnState[1] = false;
  testClock += 250;
  loopSnake();
  assert(snk[0].x == head.x - 1 && snk[0].y == head.y);

  // Stepping into the previous tail is legal when the tail moves away.
  snk[0] = {1, 1};
  snk[1] = {1, 2};
  snk[2] = {0, 2};
  snk[3] = {0, 1};
  snkFX = snkFY = 6;
  testClock += 250;
  loopSnake();
  assert(!snkDead && snk[0].x == 0 && snk[0].y == 1);
  testClock += 250;
  loopSnake();
  assert(snkDead && !snkWon);

  snakeInit();
  snkLen = SNAKE_CELLS - 1;
  for (int i = 0; i < snkLen; ++i) {
    snk[i] = {static_cast<int8_t>(i % SNAKE_WIDTH), static_cast<int8_t>(i / SNAKE_WIDTH)};
  }
  sSpawnFood();
  assert(snkFX == SNAKE_WIDTH - 1 && snkFY == SNAKE_HEIGHT - 1);

  std::swap(snk[0], snk[snkLen - 1]);
  snkDX = snkNextDX = 1;
  snkDY = snkNextDY = 0;
  testClock += 250;
  loopSnake();
  assert(snkLen == SNAKE_CELLS && snkDead && snkWon && hiSnake > 0);
}

void testDoom() {
  player.velocity = 2;
  player.dir_y = 1;
  doomInit();
  assert(player.velocity == 0 && player.dir_x == 1 && player.dir_y == 0);
  assert(player.health == 100 && player.ammo == 20 && current_wave == 1);

  bool hitY = false;
  assert(doomWallDistance(1.5, 1.5, 1, 0, hitY) == 29.5 && !hitY);
  assert(doomWallDistance(1.5, 1.5, 0, -1, hitY) == 0.5 && hitY);

  player.x = 1.5;
  player.y = 1.5;
  num_entities = 0;
  spawnEntity(E_ENEMY, 4.5, 1.5);
  spawnEntity(E_ENEMY, 2.5, 1.5);
  fire();
  assert(entity[0].health == 100 && entity[1].health < 100);
  assert(entity[1].state == S_HIT);

  // A wall near x=5 obscures the enemy even when it is in the crosshairs.
  player.y = 5.5;
  num_entities = 0;
  spawnEntity(E_ENEMY, 8.5, 5.5);
  fire();
  assert(entity[0].health == 100);

  std::fill_n(zbuffer, DOOM_WIDTH, INFINITY);
  doomSprite(-8, -8, bmp_imp_bits, bmp_imp_mask, 32, 32, 3, 0.5);
  assert(std::any_of(frameBuffer, frameBuffer + sizeof(frameBuffer),
                     [](uint8_t pixel) { return pixel != 0; }));
}
}  // namespace

int main() {
  testTetris();
  testBlocks();
  testSnake();
  testDoom();
  std::cout << "Tetris, Blocks, Snake, and DOOM regression tests passed\n";
}
