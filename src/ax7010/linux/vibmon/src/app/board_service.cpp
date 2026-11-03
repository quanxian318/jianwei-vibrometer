#include "app/board_service.h"

#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "board";
}

BoardService::BoardService(I2cBus& bus, const AppConfig& cfg, StateStore& state)
    : bus_(bus), cfg_(cfg), state_(state), tca_(bus), ina_(bus), rtc_(bus) {}

bool BoardService::init() {
  const bool tcaOk = tca_.init(tca::kConfig0, tca::kConfig1);
  if (!tcaOk) LOGE(kTag, "TCA9555 (0x20) init failed: loads cannot be controlled");
  inaOk_ = ina_.init(cfg_.power.alertA);
  if (!inaOk_) LOGE(kTag, "INA226 (0x40) init failed");
  rtcOk_ = rtc_.configure1Hz();
  if (!rtcOk_) LOGE(kTag, "DS3231 (0x68) not responding");
  state_.update([&](SystemState& s) {
    s.tcaOk = tcaOk;
    s.inaOk = inaOk_;
    s.rtcOk = rtcOk_;
  });
  if (tcaOk) pollInputs();
  return tcaOk;
}

void BoardService::inhibitPrinter(bool on) {
  std::lock_guard<std::mutex> lock(printerMutex_);
  printerInhibited_ = on;
  if (on && tca_.initialized())
    tca_.setOutputs(0, (1u << tca::kPrnPwrBit) | (1u << tca::kPrnComBit), 0);
}

bool BoardService::setBit(int port, int bit, bool on) {
  std::lock_guard<std::mutex> lock(printerMutex_);
  if (on && printerInhibited_ && port == tca::kPrnPwrPort &&
      (bit == tca::kPrnPwrBit || bit == tca::kPrnComBit)) return false;
  if (!tca_.setBit(port, bit, on)) {
    LOGW(kTag, "TCA write P%d.%d=%d failed", port, bit, on ? 1 : 0);
    return false;
  }
  return true;
}

bool BoardService::setTower(bool red, bool yellow, bool green) {
  const uint8_t mask = (1u << tca::kAlmRedBit) | (1u << tca::kAlmYellowBit) | (1u << tca::kAlmGreenBit);
  const uint8_t v = static_cast<uint8_t>((red ? 1u << tca::kAlmRedBit : 0) | (yellow ? 1u << tca::kAlmYellowBit : 0) |
                                         (green ? 1u << tca::kAlmGreenBit : 0));
  return tca_.setOutputs(tca::kAlmPort, mask, v);
}

bool BoardService::setAlarmOutputs(bool red, bool yellow, bool green, bool buzzer) {
  const uint8_t mask = (1u << tca::kAlmRedBit) | (1u << tca::kAlmYellowBit) |
                       (1u << tca::kAlmGreenBit) | (1u << tca::kBuzzBit);
  const uint8_t value = (red ? 1u << tca::kAlmRedBit : 0) |
      (yellow ? 1u << tca::kAlmYellowBit : 0) | (green ? 1u << tca::kAlmGreenBit : 0) |
      (buzzer ? 1u << tca::kBuzzBit : 0);
  return tca_.setOutputs(tca::kAlmPort, mask, value, true);
}

bool BoardService::allLoadsOff() {
  bool ok = tca_.setOutputs(0, (1u << tca::kPrnPwrBit) | (1u << tca::kPrnComBit), 0);
  ok = tca_.setOutputs(1, (1u << tca::kAlmRedBit) | (1u << tca::kAlmYellowBit) | (1u << tca::kAlmGreenBit) |
                              (1u << tca::kBuzzBit) | (1u << tca::kFanBit),
                       0) && ok;
  return ok;
}

void BoardService::pollInputs() {
  uint8_t p0 = 0, p1 = 0;
  if (!tca_.readInputs(p0, p1)) {
    state_.update([](SystemState& s) { s.tcaOk = false; s.lightAllowed = false; });
    return;
  }
  const bool lightOk = (p0 >> tca::kLightOkBit) & 1;
  const bool alertPin = ((p1 >> tca::kInaAlertBit) & 1) == 0;  // ALERT is active low
  bool latched = false;
  if (alertPin && inaOk_) {
    bool oc = false;
    if (ina_.readAlert(oc) && oc) {
      latched = true;
      LOGW(kTag, "INA226 over-current alert (> %.2f A)", cfg_.power.alertA);
    }
  }
  state_.update([&](SystemState& s) {
    s.tcaOk = true;
    s.lightAllowed = lightOk;
    if (latched) s.overCurrentLatched = true;
  });
}

void BoardService::samplePower() {
  if (!inaOk_) return;
  PowerSample p;
  if (!ina_.read(p)) {
    if (++powerErrors_ == 5) LOGW(kTag, "INA226 reads failing");
    if (powerErrors_ >= 5) state_.update([](SystemState& s) { s.inaOk = false; });
    return;
  }
  powerErrors_ = 0;
  const bool warn = p.currentA >= cfg_.power.warnA;
  const bool deny = p.currentA >= cfg_.power.denyA;
  denyHeavy_ = deny;
  state_.update([&](SystemState& s) {
    s.inaOk = true;
    s.power = p;
    s.powerWarn = warn;
    s.powerDeny = deny;
  });
}

bool BoardService::readRtc(int64_t& unixSec, bool& valid) {
  if (!rtcOk_) return false;
  return rtc_.readTime(unixSec, valid);
}

bool BoardService::writeRtc(int64_t unixSec) {
  if (!rtcOk_) return false;
  return rtc_.writeTime(unixSec);
}

}  // namespace vm
