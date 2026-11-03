#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vm {

// Full-duplex SPI transfer with this device selected for the whole buffer.
class SpiDevice {
 public:
  virtual ~SpiDevice() = default;
  virtual bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) = 0;
};

// spidev node. The AXI Quad SPI driver applies each device's CPOL/CPHA when
// that chip select is asserted, so IMU (mode 0) and ADXL345 (mode 3) can share
// the bus with all selects high during the switch.
class LinuxSpiDevice : public SpiDevice {
 public:
  ~LinuxSpiDevice() override;
  bool open(const std::string& device, uint8_t mode, uint32_t speedHz);
  void close();
  bool isOpen() const { return fd_ >= 0; }
  bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override;
  const std::string& device() const { return device_; }

 private:
  int fd_ = -1;
  uint32_t speedHz_ = 1000000;
  std::string device_;
};

}  // namespace vm
