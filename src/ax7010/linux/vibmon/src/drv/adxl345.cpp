#include "drv/adxl345.h"

#include <cstring>

namespace vm {

namespace {
constexpr uint8_t kDevId = 0x00, kBwRate = 0x2C, kPowerCtl = 0x2D, kIntEnable = 0x2E, kIntMap = 0x2F;
constexpr uint8_t kDataFormat = 0x31, kDataX0 = 0x32, kFifoCtl = 0x38;
constexpr uint8_t kRead = 0x80, kMulti = 0x40;
}  // namespace

bool Adxl345::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  uint8_t tx[16] = {0};
  uint8_t rx[16] = {0};
  if (n + 1 > sizeof(tx)) return false;
  tx[0] = static_cast<uint8_t>(kRead | (n > 1 ? kMulti : 0) | (reg & 0x3F));
  if (!spi_.transfer(tx, rx, n + 1)) return false;
  memcpy(buf, rx + 1, n);
  return true;
}

bool Adxl345::writeReg(uint8_t reg, uint8_t value) {
  const uint8_t tx[2] = {static_cast<uint8_t>(reg & 0x3F), value};
  uint8_t rx[2];
  return spi_.transfer(tx, rx, 2);
}

bool Adxl345::devId(uint8_t& id) { return readRegs(kDevId, &id, 1); }

bool Adxl345::standby() { return writeReg(kPowerCtl, 0x00); }

bool Adxl345::init(uint8_t rateCode, int rangeG) {
  uint8_t id = 0;
  if (!devId(id) || id != 0xE5) return false;
  uint8_t range = 0;
  switch (rangeG) {
    case 4: range = 1; break;
    case 8: range = 2; break;
    case 16: range = 3; break;
    default: range = 0; break;
  }
  if (!standby()) return false;
  if (!writeReg(kDataFormat, static_cast<uint8_t>(0x08 | range))) return false;  // FULL_RES, 4-wire, INT active high
  if (!writeReg(kBwRate, static_cast<uint8_t>(rateCode & 0x0F))) return false;
  if (!writeReg(kFifoCtl, 0x00)) return false;
  if (!writeReg(kIntMap, 0x00)) return false;     // all sources on INT1
  if (!writeReg(kIntEnable, 0x80)) return false;  // DATA_READY
  if (!writeReg(kPowerCtl, 0x08)) return false;   // measure
  uint8_t rb[2] = {0, 0};
  return readRegs(kBwRate, rb, 2) && (rb[0] & 0x1F) == (rateCode & 0x0F) && rb[1] == 0x08;
}

bool Adxl345::readXyz(int16_t& x, int16_t& y, int16_t& z) {
  uint8_t b[6];
  if (!readRegs(kDataX0, b, 6)) return false;
  x = static_cast<int16_t>(b[0] | (b[1] << 8));
  y = static_cast<int16_t>(b[2] | (b[3] << 8));
  z = static_cast<int16_t>(b[4] | (b[5] << 8));
  return true;
}

}  // namespace vm
