#pragma once
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string>

enum { D0, D1, D2, D3, D4, D5, D6, D7, D8, D9, D10, D11, D12, D13 };
enum { A0 = 17, A1, A2, A3, A4, A5, A6, A7 };
constexpr uint32_t SERIAL_8N1 = 0x800001c;
constexpr int UART_HW_FLOWCTRL_DISABLE = 0;

struct TestSerial {
  bool connected = true;
  std::string input, output;
  size_t quota = 64, maxWrite = 16384;
  uint32_t baud = 0, config = 0, timeout = 999;
  int rxPin = -1, txPin = -1, flow = -1;
  size_t rxBuffer = 0, txBuffer = 0;
  explicit operator bool() const { return connected; }
  void begin(uint32_t b, uint32_t c = 0, int rx = -1, int tx = -1) {
    baud = b; config = c; rxPin = rx; txPin = tx;
  }
  void setTxTimeoutMs(uint32_t t) { timeout = t; }
  void setRxBufferSize(size_t n) { rxBuffer = n; }
  void setTxBufferSize(size_t n) { txBuffer = n; }
  void setHwFlowCtrlMode(int mode) { flow = mode; }
  int available() const { return static_cast<int>(input.size()); }
  int read() {
    if (input.empty()) return -1;
    const uint8_t ch = input[0]; input.erase(0, 1); return ch;
  }
  int availableForWrite() const { return static_cast<int>(quota); }
  size_t write(const uint8_t* data, size_t len) {
    assert(len <= quota);  // production must never ask a slow port to wait
    const size_t n = std::min(len, maxWrite);
    output.append(reinterpret_cast<const char*>(data), n); quota -= n; return n;
  }
};
extern TestSerial Serial, Serial0;
extern uint32_t testMillis;
inline uint32_t millis() { return testMillis; }
inline void delay(uint32_t ms) { testMillis += ms; }
