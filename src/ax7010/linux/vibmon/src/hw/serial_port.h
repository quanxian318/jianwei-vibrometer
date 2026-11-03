#pragma once

#include <sys/types.h>

#include <cstddef>
#include <atomic>
#include <cstdint>
#include <string>
#include <memory>
#include "hw/phys_window.h"
#include "hw/uart16550.h"

namespace vm {

// Raw 8N1 serial port with arbitrary baud (termios2/BOTHER) and optional
// RTS/CTS. The AXI UART16550 runs from the AXI clock, so the achieved rate is
// clk / (16 * round(clk / (16 * baud))); both ends must agree on the real rate.
class SerialPort {
 public:
  ~SerialPort();
  bool open(const std::string& device, uint32_t baud, bool rtscts);
  void close();
  bool isOpen() const { return fd_ >= 0 || mmio_ != nullptr; }
  int fd() const { return fd_; }
  const std::string& device() const { return device_; }

  ssize_t readSome(uint8_t* buf, size_t n);         // non-blocking: 0 = nothing, -1 = error
  ssize_t writeSome(const uint8_t* buf, size_t n);  // non-blocking partial write
  // Blocking write with poll; gives up after timeoutMs without progress.
  bool writeAll(const uint8_t* buf, size_t n, int timeoutMs, const std::atomic<bool>* cancel = nullptr);
  bool waitReadable(int timeoutMs);

  void flushInput();
  void flushOutput();
  int outputQueued() const;  // bytes still in the driver/UART TX path
  bool ctsAsserted() const;  // modem status CTS (true = peer allows sending)

 private:
  int fd_ = -1;
  std::string device_;
  PhysWindow mmioWindow_;
  std::unique_ptr<Uart16550> mmio_;
};

}  // namespace vm
