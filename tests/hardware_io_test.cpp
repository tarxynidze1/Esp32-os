#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <vector>

using byte = uint8_t;
constexpr int LOW = 0;
constexpr byte PINS[] = {0, 1, 3, 4, 5};
bool pins[6] = {};
uint32_t clockMs = 0;

uint32_t millis() { return clockMs; }
int digitalRead(byte pin) { return pins[pin] ? LOW : 1; }

struct FakeWire {
  std::array<uint8_t, 256> registers{};
  std::vector<uint8_t> sent;
  uint8_t position = 0;
  unsigned transmissions = 0;
  bool missing = false, shortRead = false;

  void beginTransmission(uint8_t address) {
    assert(address == 0x53);
    ++transmissions;
    sent.clear();
  }

  void write(uint8_t value) { sent.push_back(value); }

  int endTransmission(bool stop = true) {
    if (missing) return 2;
    assert(sent.size() == (stop ? 2 : 1));
    position = sent[0];
    if (stop) registers[position] = sent[1];
    return 0;
  }

  size_t requestFrom(uint8_t address, size_t count, bool stop) {
    assert(address == 0x53 && stop);
    return shortRead && count == 6 ? 0 : count;
  }

  int read() { return registers[position++]; }
} Wire;

#include "hardware_io_under_test.h"

static void testAdxl() {
  auto& reg = Wire.registers;
  reg[0] = 0xE5;
  assert(!readADXL() && Wire.transmissions == 0);
  i2cReady = true;
  Wire.missing = true;
  clockMs = 1;
  assert(!readADXL() && Wire.transmissions == 1);
  clockMs = 250;
  assert(!readADXL() && Wire.transmissions == 1);
  Wire.missing = false;
  clockMs = 251;
  assert(!readADXL() && Wire.transmissions == 7);
  assert(reg[0x2C] == 0x0A && reg[0x2D] == 0x08 && reg[0x31] == 0x0B);

  reg[0x30] = 0x80;
  reg[0x32] = 0xFF; reg[0x33] = 0xFE;
  reg[0x34] = 0x23; reg[0x35] = 0x01;
  reg[0x36] = 0x00; reg[0x37] = 0x80;
  clockMs = 260;
  assert(readADXL() && adxlX == -257 && adxlY == 291 && adxlZ == INT16_MIN);
  Wire.shortRead = true;
  clockMs = 270;
  assert(!readADXL() && adxlX == -257);
  const unsigned transmissions = Wire.transmissions;
  clockMs = 519;
  assert(!readADXL() && Wire.transmissions == transmissions);
  clockMs = 520;
  Wire.shortRead = false;
  assert(readADXL());

  // The device can reset between polls while the bus remains responsive.
  reg[0x2D] = reg[0x30] = 0;
  clockMs = 669;
  assert(!readADXL() && adxlConfigured);
  clockMs = 670;
  assert(!readADXL() && !adxlConfigured);
  clockMs = 680;
  assert(!readADXL() && adxlConfigured && reg[0x2D] == 0x08);
  reg[0x30] = 0x80;
  clockMs = 690;
  assert(readADXL());
}

static void pollAt(uint32_t time) {
  clockMs = time;
  updateButtons();
}

static void testDebounce() {
  pollAt(0);
  pins[0] = true;
  pollAt(1);
  pins[0] = false;
  pollAt(10);
  pins[0] = true;
  pollAt(20);
  pollAt(49);
  assert(!bp(0));
  pollAt(50);
  assert(bp(0) && btnState[0]);
  pollAt(51);
  assert(!bp(0) && btnState[0]);
  pins[0] = false;
  pollAt(53);
  pollAt(82);
  assert(btnState[0]);
  pollAt(83);
  assert(!btnState[0] && btnJustReleased[0]);

  pins[5] = true;
  pollAt(100);
  pollAt(130);
  assert(!bp(4));
  pins[5] = false;
  pollAt(699);
  pollAt(729);
  assert(bp(4) && !selLongTriggeredNow);
  pollAt(730);
  assert(!bp(4));

  pins[5] = true;
  pollAt(UINT32_MAX - 320);
  pollAt(UINT32_MAX - 290);
  pollAt(278);
  assert(!selLongTriggeredNow);
  pollAt(279);
  assert(selLongTriggeredNow && !bp(4));
  pollAt(280);
  assert(!selLongTriggeredNow);
  pins[5] = false;
  pollAt(281);
  pollAt(311);
  assert(!bp(4) && !btnState[4]);
}

int main() {
  testAdxl();
  testDebounce();
  std::cout << "ADXL345 fault recovery and GPIO debounce regressions passed.\n";
}
