#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace vm {

// I2C master with 7-bit addresses (never pre-shifted).
class I2cBus {
 public:
  virtual ~I2cBus() = default;
  // Writes w then, with a repeated start, reads r. Either length may be 0.
  virtual bool transfer(uint8_t addr, const uint8_t* w, size_t wlen, uint8_t* r, size_t rlen) = 0;

  bool writeBytes(uint8_t addr, const uint8_t* data, size_t n) { return transfer(addr, data, n, nullptr, 0); }
  bool readBytes(uint8_t addr, uint8_t* data, size_t n) { return transfer(addr, nullptr, 0, data, n); }
  bool writeReg8(uint8_t addr, uint8_t reg, uint8_t value);
  bool readReg8(uint8_t addr, uint8_t reg, uint8_t& value);
  bool writeReg16BE(uint8_t addr, uint8_t reg, uint16_t value);
  bool readReg16BE(uint8_t addr, uint8_t reg, uint16_t& value);
  bool readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n);
  // One-byte read; AXI IIC does not support zero-length probe messages.
  bool probe(uint8_t addr);
};

class LinuxI2cBus : public I2cBus {
 public:
  ~LinuxI2cBus() override;
  bool open(const std::string& device);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  const std::string& device() const { return device_; }
  bool transfer(uint8_t addr, const uint8_t* w, size_t wlen, uint8_t* r, size_t rlen) override;
  uint32_t errorCount() const { return errors_; }

 private:
  int fd_ = -1;
  std::string device_;
  std::mutex mutex_;
  uint32_t errors_ = 0;
};

}  // namespace vm
