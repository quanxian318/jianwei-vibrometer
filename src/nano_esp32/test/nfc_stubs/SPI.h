#pragma once
#include <cstdint>
#include <vector>
#include <cassert>
constexpr int LSBFIRST = 0, SPI_MODE0 = 0;
struct SPISettings { SPISettings(uint32_t, int, int) {} };
struct TestSPI {
  bool responding = false, locked = false, ack = false;
  unsigned transactions = 0;
  uint8_t command = 0;
  std::vector<uint8_t> tx, rx;
  void beginTransaction(SPISettings) {
    assert(!locked); locked = true; ++transactions; tx.clear(); rx.clear();
  }
  uint8_t transfer(uint8_t value) {
    assert(locked);
    const size_t pos = tx.size(); tx.push_back(value);
    if (!responding) return 0;
    if (pos == 0 && value == 3) {
      if (ack) rx = {0, 0, 255, 0, 255, 0};
      else {
        std::vector<uint8_t> body = {0xd5, static_cast<uint8_t>(command + 1)};
        if (command == 2) body.insert(body.end(), {0x32, 1, 6, 7});
        const uint8_t len = static_cast<uint8_t>(body.size());
        rx = {0, 255, len, static_cast<uint8_t>(-len)};
        uint8_t sum = 0;
        for (auto b : body) { sum += b; rx.push_back(b); }
        rx.push_back(static_cast<uint8_t>(-sum)); rx.push_back(0);
      }
    }
    if (pos > 0 && tx[0] == 2) return 1;
    if (pos > 0 && tx[0] == 3 && pos - 1 < rx.size()) return rx[pos - 1];
    return 0;
  }
  void endTransaction() {
    assert(locked);
    if (!tx.empty() && tx[0] == 1) {
      if (tx.size() > 7) { command = tx[7]; ack = true; }
      else ack = false;
    } else if (!tx.empty() && tx[0] == 3) ack = false;
    locked = false;
  }
};
extern TestSPI SPI;
