#include "drv/carrier_ctrl.h"

#include <cmath>

namespace vm {

bool CarrierCtrl::probe(uint32_t* version) {
  if (io_.read32(creg::kId) != creg::kIdValue) return false;
  if (version) *version = io_.read32(creg::kVersion);
  clkHz_ = io_.read32(creg::kClkHz);
  period_ = io_.read32(creg::kPwmPeriod);
  ctrl_ = io_.read32(creg::kPwmCtrl);
  return true;
}

uint32_t CarrierCtrl::clockHz() {
  if (clkHz_ == 0) clkHz_ = io_.read32(creg::kClkHz);
  return clkHz_;
}

uint64_t CarrierCtrl::timestamp() {
  std::lock_guard<std::mutex> lock(tsMutex_);
  const uint32_t lo = io_.read32(creg::kTsLo);  // latches the high word
  const uint32_t hi = io_.read32(creg::kTsHi);
  return (static_cast<uint64_t>(hi) << 32) | lo;
}

PanelState CarrierCtrl::panel() {
  PanelState p;
  const uint32_t k = io_.read32(creg::kKeyState);
  p.keyPressed = (k & 1u) != 0;
  p.encSwPressed = (k & 2u) != 0;
  p.encCount = static_cast<int32_t>(io_.read32(creg::kEncCount));
  p.encErrors = io_.read32(creg::kEncErrors);
  p.inRaw = io_.read32(creg::kInRaw);
  return p;
}

void CarrierCtrl::configurePanel(int debounceMs, int longMs, int encFilterUs) {
  io_.write32(creg::kDebounceMs, static_cast<uint32_t>(debounceMs < 1 ? 1 : (debounceMs > 255 ? 255 : debounceMs)));
  io_.write32(creg::kLongMs, static_cast<uint32_t>(longMs < 100 ? 100 : (longMs > 65535 ? 65535 : longMs)));
  io_.write32(creg::kEncFilterUs, static_cast<uint32_t>(encFilterUs < 1 ? 1 : (encFilterUs > 1023 ? 1023 : encFilterUs)));
}

void CarrierCtrl::setPwmFrequency(uint32_t hz) {
  if (hz == 0) hz = 20000;
  uint32_t period = clockHz() / hz;
  if (period < 2) period = 2;
  if (period > 0xFFFFFF) period = 0xFFFFFF;
  period_ = period;
  io_.write32(creg::kPwmPeriod, period);
}

void CarrierCtrl::setPwmPercent(int channel, double percent) {
  if (period_ == 0) period_ = io_.read32(creg::kPwmPeriod);
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  uint32_t duty = percent >= 100.0 ? period_ : static_cast<uint32_t>(std::lround(period_ * percent / 100.0));
  io_.write32(channel == 0 ? creg::kPwmDuty0 : creg::kPwmDuty1, duty);
}

void CarrierCtrl::setPwmEnable(bool ch0, bool ch1) {
  ctrl_ = (ctrl_ & ~3u) | (ch0 ? 1u : 0u) | (ch1 ? 2u : 0u);
  io_.write32(creg::kPwmCtrl, ctrl_);
}

void CarrierCtrl::setPwmKills(bool pfailKill, bool watchdogKill) {
  ctrl_ = (ctrl_ & 3u) | (pfailKill ? 0x10u : 0u) | (watchdogKill ? 0x20u : 0u);
  io_.write32(creg::kPwmCtrl, ctrl_);
}

void CarrierCtrl::setLeds(const uint32_t rgb[5]) {
  for (uint32_t i = 0; i < 5; ++i) io_.write32(creg::kWsColor0 + 4 * i, rgb[i] & 0xFFFFFF);
}

void CarrierCtrl::setFaultFrame(const uint32_t rgb[5]) {
  for (uint32_t i = 0; i < 5; ++i) io_.write32(creg::kWsFault0 + 4 * i, rgb[i] & 0xFFFFFF);
}

void CarrierCtrl::configureEvents(uint32_t riseMask, uint32_t fallMask) {
  io_.write32(creg::kEvtEnRise, riseMask);
  io_.write32(creg::kEvtEnFall, fallMask);
}

size_t CarrierCtrl::popEvents(std::vector<CarrierEvent>& out, size_t max) {
  std::lock_guard<std::mutex> lock(evtMutex_);
  const uint32_t status = io_.read32(creg::kEvtStatus);
  size_t count = status & 0x3FF;
  if (count > max) count = max;
  size_t added = 0;
  for (size_t i = 0; i < count; ++i) {
    uint32_t info = 0;
    // A read may overtake the previous POP on the AXI bus; the sequence number
    // exposes that, so re-read until the head has advanced.
    bool fresh = false;
    for (int attempt = 0; attempt < 4; ++attempt) {
      info = io_.read32(creg::kEvtInfo);
      const uint16_t seq = static_cast<uint16_t>(info >> 16);
      if (!haveSeq_ || seq != lastSeq_) {
        fresh = true;
        break;
      }
    }
    if (!fresh) break;
    CarrierEvent e;
    e.source = static_cast<uint8_t>(info & 0x1F);
    e.level = (info & 0x100u) != 0;
    e.seq = static_cast<uint16_t>(info >> 16);
    const uint32_t lo = io_.read32(creg::kEvtTsLo);
    const uint32_t hi = io_.read32(creg::kEvtTsHi);
    e.ticks = (static_cast<uint64_t>(hi) << 32) | lo;
    io_.write32(creg::kEvtPop, 1);
    haveSeq_ = true;
    lastSeq_ = e.seq;
    out.push_back(e);
    ++added;
  }
  return added;
}

}  // namespace vm
