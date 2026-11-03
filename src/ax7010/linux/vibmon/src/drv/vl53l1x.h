#pragma once

#include <cstdint>

#include "hw/i2c_bus.h"

namespace vm {

struct TofReading {
  int distanceMm = 0;
  int rangeStatus = -1;  // ULD status: 0 = valid; anything else is not a distance
  int signalKcps = 0;
  int ambientKcps = 0;
  bool valid = false;
};

// VL53L1X on I2C1 (7-bit 0x29). Identification works with this file alone;
// ranging uses ST's ULD (STSW-IMG009), which must be downloaded from ST and
// copied into third_party/vl53l1x_uld (see README). Without it, ranging
// reports "driver missing" instead of fake distances.
class Vl53l1x {
 public:
  explicit Vl53l1x(I2cBus& bus, uint8_t addr = 0x29) : bus_(bus), addr_(addr) {}
  bool readModelId(uint16_t& id);  // 0xEACC for VL53L1X; VL53L0X answers differently
  static bool driverAvailable();
  bool start(int timingBudgetMs, int periodMs, bool longRange);
  bool stop();
  bool poll(TofReading& r, bool& ready);

 private:
  I2cBus& bus_;
  uint8_t addr_;
  bool running_ = false;
};

}  // namespace vm
