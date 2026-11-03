#pragma once

#include <cstddef>
#include <cstdint>

#include "hw/i2c_bus.h"

namespace vm {

// GY-SHT40 on I2C1 (0x44). High-repeatability measurement, heater never used.
class Sht40 {
 public:
  explicit Sht40(I2cBus& bus, uint8_t addr = 0x44) : bus_(bus), addr_(addr) {}
  bool softReset();
  bool readSerial(uint32_t& serial);
  // Blocks ~10 ms for the conversion. Rejects frames whose CRC fails.
  bool measure(double& tempC, double& rhPercent);

  static uint8_t crc8(const uint8_t* data, size_t n);
  static double tempFromRaw(uint16_t raw) { return -45.0 + 175.0 * raw / 65535.0; }
  static double rhFromRaw(uint16_t raw) {
    double rh = -6.0 + 125.0 * raw / 65535.0;
    return rh < 0 ? 0 : (rh > 100 ? 100 : rh);
  }

 private:
  bool readWords(uint8_t cmd, int waitMs, uint16_t& a, uint16_t& b);
  I2cBus& bus_;
  uint8_t addr_;
};

}  // namespace vm
