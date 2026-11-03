#pragma once

#include <cstdint>

#include "hw/i2c_bus.h"

namespace vm {

struct PowerSample {
  double busV = 0;     // 12V_MAIN
  double shuntMv = 0;
  double currentA = 0;  // + = 12V_PROTECTED -> 12V_MAIN
  double powerW = 0;
};

// U1 INA226 (0x40) on RS101 = 5 mOhm; Current_LSB 0.25 mA -> CAL 4096.
class Ina226 {
 public:
  explicit Ina226(I2cBus& bus, uint8_t addr = 0x40, double shuntOhm = 0.005, double currentLsbA = 0.00025)
      : bus_(bus), addr_(addr), shuntOhm_(shuntOhm), currentLsbA_(currentLsbA) {}

  // Checks the IDs, sets 16x averaging with 1.1 ms conversions, writes the
  // calibration and a latched shunt over-voltage alert at alertCurrentA.
  bool init(double alertCurrentA);
  bool read(PowerSample& s);
  // Reads Mask/Enable (which releases a latched ALERT); returns the SOL/AFF state.
  bool readAlert(bool& overCurrent);
  uint16_t calibration() const;

 private:
  I2cBus& bus_;
  uint8_t addr_;
  double shuntOhm_;
  double currentLsbA_;
};

}  // namespace vm
