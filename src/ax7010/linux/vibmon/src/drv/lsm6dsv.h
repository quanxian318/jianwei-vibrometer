#pragma once

#include <cstdint>

#include "hw/spi_dev.h"

namespace vm {

struct ImuRaw {
  int16_t temp = 0;
  int16_t gx = 0, gy = 0, gz = 0;
  int16_t ax = 0, ay = 0, az = 0;
};

// LSM6DSV camera-motion IMU on J702 (4-wire SPI, mode 0). Register values
// follow DS13476 Rev 5; nothing is borrowed from LSM6DS3/DSOX tables.
class Lsm6dsv {
 public:
  struct Config {
    int odrHz = 240;        // 7.5 .. 7680 (high-performance mode)
    int accelRangeG = 2;    // 2/4/8/16
    int gyroRangeDps = 250; // 125/250/500/1000/2000/4000
  };

  explicit Lsm6dsv(SpiDevice& spi) : spi_(spi) {}
  bool whoAmI(uint8_t& id);  // expect 0x70
  // Software reset, I2C/I3C disabled (the bus is shared with ADXL345),
  // BDU + auto-increment, pulsed data-ready: INT1 = accel DRDY, INT2 = gyro DRDY.
  bool init(const Config& cfg);
  bool powerDown();
  bool readRaw(ImuRaw& r);  // OUT_TEMP_L..OUTZ_H_A in one burst
  double accelG(int16_t v) const { return v * accelScale_; }
  double gyroDps(int16_t v) const { return v * gyroScale_; }
  static double tempC(int16_t v) { return 25.0 + v / 256.0; }
  int odrHz() const { return actualOdr_; }

  static uint8_t odrCode(int hz, int& actual);

 private:
  bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
  bool writeReg(uint8_t reg, uint8_t value);
  SpiDevice& spi_;
  double accelScale_ = 0.061e-3;
  double gyroScale_ = 8.75e-3;
  int actualOdr_ = 0;
};

}  // namespace vm
