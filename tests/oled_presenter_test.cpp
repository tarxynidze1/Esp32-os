#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <vector>

constexpr uint8_t OLED_WIDTH = 128;
constexpr uint8_t OLED_HEIGHT = 64;
constexpr uint8_t OLED_ADDRESS = 0x3C;
constexpr uint8_t SSD1306_COLUMNADDR = 0x21;
constexpr uint8_t SSD1306_PAGEADDR = 0x22;
constexpr uint8_t SSD1306_MEMORYMODE = 0x20;

struct FakeDisplay {
  uint8_t frame[1024] = {};
  uint8_t* getBuffer() { return frame; }
} display;

struct FakeWire {
  uint8_t panel[1024] = {};
  std::vector<uint8_t> packet;
  int transactions = 0;
  int dataBytes = 0;
  size_t writeBytes = 0;
  int failAfter = -1;

  void beginTransmission(uint8_t address) {
    assert(address == OLED_ADDRESS);
    packet.clear();
  }

  void write(const uint8_t* bytes, size_t size) {
    packet.insert(packet.end(), bytes, bytes + size);
  }

  int endTransmission() {
    ++transactions;
    writeBytes += packet.size();
    assert(!packet.empty() && packet.size() <= 32);
    const bool failed = failAfter == transactions;
    if (failed) packet.resize(std::min<size_t>(packet.size(), 2));
    if (packet[0] == 0x00) {
      for (size_t i = 1; i < packet.size(); ++i) readCommand(packet[i]);
    } else {
      assert(packet[0] == 0x40);
      for (size_t i = 1; i < packet.size(); ++i) writePixelByte(packet[i]);
    }
    return failed;
  }

 private:
  int command = 0;
  int arguments = 0;
  int left = 0, right = OLED_WIDTH - 1;
  int first = 0, last = OLED_HEIGHT / 8 - 1;
  int column = 0, page = 0;

  void readCommand(uint8_t value) {
    if (arguments) {
      if (command == SSD1306_COLUMNADDR) (arguments == 2 ? left : right) = value;
      if (command == SSD1306_PAGEADDR) (arguments == 2 ? first : last) = value;
      if (command == SSD1306_MEMORYMODE) assert(value == 0);
      if (--arguments == 0) {
        column = left;
        page = first;
      }
    } else if (value == SSD1306_COLUMNADDR || value == SSD1306_PAGEADDR ||
               value == SSD1306_MEMORYMODE) {
      command = value;
      arguments = value == SSD1306_MEMORYMODE ? 1 : 2;
    } else {
      assert(value == 0xE3);
    }
  }

  void writePixelByte(uint8_t value) {
    assert(column >= 0 && column < OLED_WIDTH && page >= 0 && page < OLED_HEIGHT / 8);
    panel[page * OLED_WIDTH + column] = value;
    ++dataBytes;
    if (++column > right) {
      column = left;
      if (++page > last) page = first;
    }
  }
} Wire;

#include "oled_presenter_under_test.h"

void assertFrameMatchesPanel() {
  assert(memcmp(display.frame, Wire.panel, sizeof(display.frame)) == 0);
}

int main() {
  presentDisplay();
  assertFrameMatchesPanel();
  const int bufferSize = sizeof(display.frame);
  assert(Wire.dataBytes == bufferSize);

  const size_t fullRefreshBytes = Wire.writeBytes;
  const int transactions = Wire.transactions;
  presentDisplay();
  assertFrameMatchesPanel();
  assert(Wire.transactions == transactions);
  assert(Wire.writeBytes == fullRefreshBytes);

  display.frame[0] = 1;
  display.frame[1023] = 2;
  presentDisplay();
  assertFrameMatchesPanel();
  assert(Wire.dataBytes == bufferSize + 2);
  const size_t sparseRefreshBytes = Wire.writeBytes - fullRefreshBytes;
  assert(sparseRefreshBytes < fullRefreshBytes);

  display.frame[256 + 42] = 3;
  display.frame[384 + 42] = 4;
  presentDisplay();
  assertFrameMatchesPanel();
  assert(Wire.dataBytes == bufferSize + 4);

  display.frame[0] = 5;
  Wire.failAfter = Wire.transactions + 1;
  presentDisplay();
  presentDisplay();
  assertFrameMatchesPanel();

  display.frame[1023] = 6;
  display.frame[1021] = 7;
  Wire.failAfter = Wire.transactions + 2;
  presentDisplay();
  presentDisplay();
  assertFrameMatchesPanel();

  std::mt19937 rng(12345);
  for (int frame = 1; frame <= 800; ++frame) {
    for (int change = 0; change < frame % 25; ++change) {
      const auto index = rng() % bufferSize;
      display.frame[index] = static_cast<uint8_t>(rng());
    }
    presentDisplay();
    assertFrameMatchesPanel();
  }

  std::cout << "OLED I2C write bytes (excluding addresses and acknowledgments): "
            << fullRefreshBytes << " full refresh, 0 idle, " << sparseRefreshBytes
            << " for two distant changed bytes. Retry and randomized checks passed.\n";
}
