#pragma once

#include <cstdint>

#include "hw/i2c_bus.h"

namespace vm {

// U802 DS3231 (0x68), kept in UTC, 24-hour mode, years 2000..2199.
class Ds3231 {
 public:
  explicit Ds3231(I2cBus& bus, uint8_t addr = 0x68) : bus_(bus), addr_(addr) {}

  // INTCN=0, RS=00 (1 Hz on INT/SQW -> RTC_1HZ), BBSQW=0, oscillator on, 32 kHz off.
  bool configure1Hz();
  // valid=false when OSF is set (oscillator stopped since last set) or the
  // registers hold an impossible date.
  bool readTime(int64_t& unixSec, bool& valid);
  bool writeTime(int64_t unixSec);  // also clears OSF
  bool readTemperature(double& celsius);

  static uint8_t toBcd(int v) { return static_cast<uint8_t>(((v / 10) << 4) | (v % 10)); }
  static int fromBcd(uint8_t b) { return (b >> 4) * 10 + (b & 0x0F); }

 private:
  I2cBus& bus_;
  uint8_t addr_;
};

}  // namespace vm
