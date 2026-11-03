#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
#include "hw/uio.h"

namespace vm {
// AXI UART16550 register access for systems without a Linux 8250 driver.
// Called by the printer worker only; Nano keeps its native termios UART.
class Uart16550 {
 public:
  explicit Uart16550(RegIo& io) : io_(io) {}
  bool init(uint32_t clockHz, uint32_t baud, bool ctsFlow);
  ssize_t readSome(uint8_t* data, size_t count);
  ssize_t writeSome(const uint8_t* data, size_t count);
  bool ctsAsserted() const;
  bool readable() const;
  int outputQueued() const;
  void flushInput();
  void flushOutput();
 private:
  RegIo& io_;
  bool ctsFlow_ = false;
};
}  // namespace vm
