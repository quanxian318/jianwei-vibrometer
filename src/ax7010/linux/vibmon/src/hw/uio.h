#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vm {

// 32-bit register access, implemented by UIO and by test fakes.
class RegIo {
 public:
  virtual ~RegIo() = default;
  virtual uint32_t read32(uint32_t offset) = 0;
  virtual void write32(uint32_t offset, uint32_t value) = 0;
};

// /dev/uioN bound by uio_pdrv_genirq: map0 = registers, read() = interrupt
// count; the kernel masks the interrupt until enableIrq() is written.
class UioDevice : public RegIo {
 public:
  ~UioDevice() override;
  bool open(const std::string& device, size_t mapSize);
  void close();
  bool isOpen() const { return regs_ != nullptr; }
  int fd() const { return fd_; }
  uint32_t read32(uint32_t offset) override;
  void write32(uint32_t offset, uint32_t value) override;
  bool enableIrq();
  // Consumes a pending interrupt notification; returns false if none was pending.
  bool ackIrq();

 private:
  int fd_ = -1;
  volatile uint32_t* regs_ = nullptr;
  size_t size_ = 0;
};

}  // namespace vm
