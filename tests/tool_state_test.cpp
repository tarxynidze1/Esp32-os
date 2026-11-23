#include <algorithm>
#include <atomic>
#include <cassert>
#include <climits>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>

using byte = uint8_t;
using std::isfinite;
using std::max;
using std::min;

uint32_t clockMs = 0, clockUs = 0, seed = 42;
bool presses[5] = {};
int16_t adxlX = 0, adxlY = 0, adxlZ = 0;
int sensorReads = 0;
bool sensorPresent = true;

uint32_t millis() { return clockMs; }
uint32_t micros() { return clockUs; }
bool bp(byte key) { return presses[key]; }
bool readADXL() { ++sensorReads; return sensorPresent; }
float constrain(float value, float low, float high) { return max(low, min(value, high)); }

long random(long limit) {
  assert(limit > 0);
  seed = seed * 1664525u + 1013904223u;
  return (seed >> 8) % limit;
}
long random(long low, long high) { return low + random(high - low); }

#define F(value) value
struct FakeSerial {
  void println(const char*) {}
} Serial;

struct FakeStorage {
  uint8_t data[512] = {};
  int attempts = 0;
  bool commitSucceeds = true;

  bool begin(size_t size) { return size == sizeof(data); }
  uint8_t read(size_t offset) { return data[offset]; }
  void write(size_t offset, uint8_t value) { data[offset] = value; }
  template <typename T> void get(size_t offset, T& value) {
    assert(offset + sizeof(T) <= sizeof(data));
    memcpy(&value, data + offset, sizeof(T));
  }
  template <typename T> void put(size_t offset, const T& value) {
    assert(offset + sizeof(T) <= sizeof(data));
    memcpy(data + offset, &value, sizeof(T));
  }
  bool commit() { ++attempts; return commitSucceeds; }
} EEPROM;

#include "tool_logic_under_test.h"

static void selectNoteKey(int column, int row) {
  nKbdX = column;
  nKbdY = row;
  presses[4] = true;
  loopNotes();
  presses[4] = false;
}

static void testStorageAndNotes() {
  const int legacyScore = 4242;
  const int corruptScore = -1;
  Note legacy{};
  memset(legacy.title, 'a', sizeof(legacy.title));
  memset(legacy.text, 'b', sizeof(legacy.text));
  legacy.inUse = 1;
  legacy.isTicked = 1;
  EEPROM.write(StorageMagicAddress, StorageMagic);
  EEPROM.put(0, legacyScore);
  EEPROM.put(4, corruptScore);
  EEPROM.put(NotesAddress + sizeof(Note), legacy);

  loadScoresAndNotes();
  assert(hiRacer == legacyScore && hiTetris == 0);
  assert(noteCount() == 1 && notes[0].isTicked);
  assert(strlen(notes[0].title) == sizeof(legacy.title) - 1);
  assert(strlen(notes[0].text) == sizeof(legacy.text) - 1);
  saveScore(4, hiTetris, 10);
  saveScore(4, hiTetris, 20);
  clockMs = 499;
  serviceStorage();
  assert(EEPROM.attempts == 0);
  EEPROM.commitSucceeds = false;
  clockMs = 500;
  serviceStorage();
  assert(storageDirty && EEPROM.attempts == 1);
  clockMs = 5499;
  serviceStorage();
  assert(EEPROM.attempts == 1);
  EEPROM.commitSucceeds = true;
  clockMs = 5500;
  serviceStorage();
  assert(!storageDirty && EEPROM.attempts == 2);

  notesInit();
  nSel = 1;
  selectNoteKey(0, 0);
  assert(nSt == N_EDIT_TITLE);
  selectNoteKey(0, 0);  // "a"
  selectNoteKey(3, 7);  // Confirm title.
  selectNoteKey(4, 7);  // Cancel body.
  assert(noteCount() == 1);
  selectNoteKey(0, 0);
  selectNoteKey(1, 0);  // "b"
  selectNoteKey(3, 7);
  selectNoteKey(2, 0);  // "c"
  selectNoteKey(3, 7);
  assert(noteCount() == 2);
  assert(strcmp(notes[1].title, "b") == 0 && strcmp(notes[1].text, "c") == 0);
  assert(!notes[1].isTicked);
  selectNoteKey(0, 0);  // Existing note action.
  nActSel = 3;
  selectNoteKey(0, 0);  // Delete prompt.
  nActSel = 1;
  selectNoteKey(0, 0);
  assert(noteCount() == 1 && nSt == N_LIST && !notes[1].inUse);
}

static void testCalculator() {
  cClear();
  for (char key : "999999999999*999999999999=") {
    if (key) cKey(key);
  }
  assert(cNumber() > 9.99e23 && strlen(dv) <= 9);
  cKey('/');
  cKey('0');
  cKey('=');
  assert(strcmp(dv, "ERR") == 0 && !oper);
  cKey('1');
  cKey('=');
  assert(cEng && cMaxRows == 5);
  for (char key : "4+9R=") {
    if (key) cKey(key);
  }
  assert(cNumber() == 7.0);
  cKey('^');
  assert(cNumber() == 49.0);
  cKey('C');
  assert(!cEng && cMaxRows == 4 && strcmp(dv, "0") == 0);
}

static void testClockAndMath() {
  clockMs = 61500;
  updateClock();
  assert(hr == 12 && mn == 1 && sc == 1);
  clockMs = 62000;
  updateClock();
  assert(sc == 2);

  clockMs = UINT32_MAX - 500;
  mathInit();
  for (int level : {1, 4, 8, 99, INT_MAX}) {
    mgLevel = level;
    for (int trial = 0; trial < 250; ++trial) {
      mathGenerate();
      assert(strlen(mgOpStr) <= 10);
      assert(std::count(mgOpts, mgOpts + 4, mgAns) == 1);
      for (int i = 0; i < 4; ++i) {
        assert(mgOpts[i] >= 0);
        for (int j = i + 1; j < 4; ++j) assert(mgOpts[i] != mgOpts[j]);
      }
    }
  }

  mgSel = std::find(mgOpts, mgOpts + 4, mgAns) - mgOpts;
  presses[4] = true;
  loopMath();
  presses[4] = false;
  assert(mgLevel == INT_MAX && mgSt == MG_PLAY);
  clockMs += MathQuestionMs;
  loopMath();
  assert(mgSt == MG_OVER && hiMath == INT_MAX);
  presses[4] = true;
  loopMath();
  presses[4] = false;
  assert(mgSt == MG_PLAY && mgLevel == 1);
}

static void testAdxlDemo() {
  enterAdxlApp();
  presses[1] = presses[4] = true;
  loopADXL();
  presses[1] = presses[4] = false;
  assert(adxlSt == ADXL_BALLS);

  adxlX = INT16_MIN;
  adxlY = INT16_MAX;
  for (int i = 0; i < 1000; ++i) {
    clockUs += 1000;
    loopADXL();
    for (const Ball& ball : balls) {
      assert(ball.x >= ball.radius && ball.x <= 63 - ball.radius);
      assert(ball.y >= ball.radius && ball.y <= 127 - ball.radius);
    }
  }
  assert(sensorReads == 60);
  sensorPresent = false;
  clockUs += 1000000;
  loopADXL();
  assert(sensorReads == 61);
  presses[4] = true;
  loopADXL();
  presses[4] = false;
  assert(adxlSt == ADXL_MENU);
}

int main() {
  testStorageAndNotes();
  testCalculator();
  testClockAndMath();
  testAdxlDemo();
  std::cout << "Calculator, EEPROM, Notes, Clock, Math and ADXL demo regressions passed.\n";
}
