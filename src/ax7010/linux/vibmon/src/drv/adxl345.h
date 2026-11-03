#pragma once

#include <cstdint>

#include "hw/spi_dev.h"

namespace vm {

// GY-291 ADXL345 reference accelerometer on J703 (4-wire SPI, mode 3).
// Not register compatible with ADXL355. Before fitting it on the shared bus,
// check that the module's SDO is not tied to GND and that CS is free.
class Adxl345 {
 public:
  explicit Adxl345(SpiDevice& spi) : spi_(spi) {}
  bool devId(uint8_t& id);  // expect 0xE5
  // rateCode: BW_RATE[3:0] (0x0A = 100 Hz, 0x0B = 200 Hz, 0x0C = 400 Hz, 0x0D = 800 Hz).
  // Full resolution (3.9 mg/LSB), DATA_READY on INT1 (active high), FIFO bypass.
  bool init(uint8_t rateCode, int rangeG);
  bool standby();
  bool readXyz(int16_t& x, int16_t& y, int16_t& z);  // reading DATAX0..DATAZ1 clears DATA_READY
  static double lsbToG() { return 1.0 / 256.0; }
  static double rateHz(uint8_t code) { return 3200.0 / static_cast<double>(1 << (15 - (code & 0x0F))); }

 private:
  bool readRegs(uint8_t reg, uint8_t* buf, size_t n);
  bool writeReg(uint8_t reg, uint8_t value);
  SpiDevice& spi_;
};

}  // namespace vm
