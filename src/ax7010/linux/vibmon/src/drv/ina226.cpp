#include "drv/ina226.h"

#include <cmath>

namespace vm {

namespace {
constexpr uint8_t kConfig = 0x00, kShunt = 0x01, kBus = 0x02, kPower = 0x03, kCurrent = 0x04;
constexpr uint8_t kCalibration = 0x05, kMaskEnable = 0x06, kAlertLimit = 0x07;
constexpr uint8_t kManufacturerId = 0xFE, kDieId = 0xFF;
constexpr uint16_t kConfigValue = 0x4527;  // AVG=16, VBUSCT=VSHCT=1.1 ms, shunt+bus continuous
constexpr uint16_t kMaskSol = 0x8000, kMaskAff = 0x0010, kMaskLatch = 0x0001;
}  // namespace

uint16_t Ina226::calibration() const {
  return static_cast<uint16_t>(std::lround(0.00512 / (currentLsbA_ * shuntOhm_)));
}

bool Ina226::init(double alertCurrentA) {
  uint16_t mfg = 0, die = 0;
  if (!bus_.readReg16BE(addr_, kManufacturerId, mfg) || !bus_.readReg16BE(addr_, kDieId, die)) return false;
  if (mfg != 0x5449 || (die & 0xFFF0) != 0x2260) return false;
  if (!bus_.writeReg16BE(addr_, kConfig, kConfigValue)) return false;
  if (!bus_.writeReg16BE(addr_, kCalibration, calibration())) return false;
  // Shunt over-voltage limit in 2.5 uV steps.
  const long limit = std::lround(alertCurrentA * shuntOhm_ / 2.5e-6);
  if (!bus_.writeReg16BE(addr_, kAlertLimit, static_cast<uint16_t>(limit < 0 ? 0 : (limit > 32767 ? 32767 : limit)))) {
    return false;
  }
  if (!bus_.writeReg16BE(addr_, kMaskEnable, kMaskSol | kMaskLatch)) return false;
  uint16_t rb = 0;
  return bus_.readReg16BE(addr_, kCalibration, rb) && rb == calibration();
}

bool Ina226::read(PowerSample& s) {
  uint16_t shunt = 0, busv = 0, cur = 0, pwr = 0;
  if (!bus_.readReg16BE(addr_, kShunt, shunt) || !bus_.readReg16BE(addr_, kBus, busv) ||
      !bus_.readReg16BE(addr_, kCurrent, cur) || !bus_.readReg16BE(addr_, kPower, pwr)) {
    return false;
  }
  s.shuntMv = static_cast<int16_t>(shunt) * 0.0025;
  s.busV = busv * 0.00125;
  s.currentA = static_cast<int16_t>(cur) * currentLsbA_;
  s.powerW = pwr * 25.0 * currentLsbA_;
  return true;
}

bool Ina226::readAlert(bool& overCurrent) {
  uint16_t m = 0;
  if (!bus_.readReg16BE(addr_, kMaskEnable, m)) return false;
  overCurrent = (m & kMaskAff) != 0;
  return true;
}

}  // namespace vm
