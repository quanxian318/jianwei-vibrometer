#include "drv/lsm6dsv.h"

#include <cstring>

#include "util/clock.h"

namespace vm {

namespace {
constexpr uint8_t kIfCfg = 0x03, kInt1Ctrl = 0x0D, kInt2Ctrl = 0x0E, kWhoAmI = 0x0F;
constexpr uint8_t kCtrl1 = 0x10, kCtrl2 = 0x11, kCtrl3 = 0x12, kCtrl4 = 0x13, kCtrl6 = 0x15, kCtrl8 = 0x17;
constexpr uint8_t kOutTempL = 0x20;
}  // namespace

uint8_t Lsm6dsv::odrCode(int hz, int& actual) {
  static const struct {
    int hz;
    uint8_t code;
  } kTable[] = {{8, 0x2}, {15, 0x3}, {30, 0x4}, {60, 0x5}, {120, 0x6}, {240, 0x7},
                {480, 0x8}, {960, 0x9}, {1920, 0xA}, {3840, 0xB}, {7680, 0xC}};
  for (const auto& e : kTable) {
    if (hz <= e.hz) {
      actual = e.hz == 8 ? 7 : e.hz;
      return e.code;
    }
  }
  actual = 7680;
  return 0xC;
}

bool Lsm6dsv::readRegs(uint8_t reg, uint8_t* buf, size_t n) {
  uint8_t tx[32] = {0};
  uint8_t rx[32] = {0};
  if (n + 1 > sizeof(tx)) return false;
  tx[0] = static_cast<uint8_t>(0x80 | reg);
  if (!spi_.transfer(tx, rx, n + 1)) return false;
  memcpy(buf, rx + 1, n);
  return true;
}

bool Lsm6dsv::writeReg(uint8_t reg, uint8_t value) {
  const uint8_t tx[2] = {static_cast<uint8_t>(reg & 0x7F), value};
  uint8_t rx[2];
  return spi_.transfer(tx, rx, 2);
}

bool Lsm6dsv::whoAmI(uint8_t& id) { return readRegs(kWhoAmI, &id, 1); }

bool Lsm6dsv::powerDown() { return writeReg(kCtrl1, 0x00) && writeReg(kCtrl2, 0x00); }

bool Lsm6dsv::init(const Config& cfg) {
  uint8_t id = 0;
  if (!whoAmI(id) || id != 0x70) return false;
  if (!powerDown() || !writeReg(kCtrl3, 0x01)) return false;  // SW_RESET
  for (int i = 0; i < 10; ++i) {
    sleepMs(1);
    uint8_t c3 = 0;
    if (readRegs(kCtrl3, &c3, 1) && (c3 & 0x01) == 0) break;
  }
  // IF_CFG survives SW_RESET: push-pull, active-high interrupts, 4-wire SPI, I2C/I3C off.
  if (!writeReg(kIfCfg, 0x01)) return false;
  if (!writeReg(kCtrl3, 0x44)) return false;  // BDU | IF_INC
  if (!writeReg(kCtrl4, 0x02)) return false;  // DRDY_PULSED (65 us pulses)

  uint8_t fsXl = 0;
  switch (cfg.accelRangeG) {
    case 4: fsXl = 1; accelScale_ = 0.122e-3; break;
    case 8: fsXl = 2; accelScale_ = 0.244e-3; break;
    case 16: fsXl = 3; accelScale_ = 0.488e-3; break;
    default: fsXl = 0; accelScale_ = 0.061e-3; break;
  }
  uint8_t fsG = 1;
  switch (cfg.gyroRangeDps) {
    case 125: fsG = 0x0; gyroScale_ = 4.375e-3; break;
    case 500: fsG = 0x2; gyroScale_ = 17.5e-3; break;
    case 1000: fsG = 0x3; gyroScale_ = 35e-3; break;
    case 2000: fsG = 0x4; gyroScale_ = 70e-3; break;
    case 4000: fsG = 0xC; gyroScale_ = 140e-3; break;
    default: fsG = 0x1; gyroScale_ = 8.75e-3; break;
  }
  if (!writeReg(kCtrl6, fsG) || !writeReg(kCtrl8, fsXl)) return false;
  if (!writeReg(kInt1Ctrl, 0x01) || !writeReg(kInt2Ctrl, 0x02)) return false;  // DRDY_XL -> INT1, DRDY_G -> INT2
  const uint8_t odr = odrCode(cfg.odrHz, actualOdr_);
  if (!writeReg(kCtrl1, odr) || !writeReg(kCtrl2, odr)) return false;  // high-performance mode
  uint8_t rb[2] = {0, 0};
  return readRegs(kCtrl1, rb, 2) && rb[0] == odr && rb[1] == odr;
}

bool Lsm6dsv::readRaw(ImuRaw& r) {
  uint8_t b[14];
  // Burst starts at 0x20: a first byte of 0xA0 cannot match ADXL345's I2C address.
  if (!readRegs(kOutTempL, b, sizeof(b))) return false;
  auto s16 = [&](int i) { return static_cast<int16_t>(b[i] | (b[i + 1] << 8)); };
  r.temp = s16(0);
  r.gx = s16(2);
  r.gy = s16(4);
  r.gz = s16(6);
  r.ax = s16(8);
  r.ay = s16(10);
  r.az = s16(12);
  return true;
}

}  // namespace vm
