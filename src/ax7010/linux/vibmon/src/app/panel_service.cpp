#include "app/panel_service.h"

#include <cstring>

namespace vm {

namespace {
constexpr uint32_t kRed = 0xFF0000, kGreen = 0x00FF00, kBlue = 0x0000FF, kYellow = 0xFFB000, kOff = 0;

bool blink(int64_t nowMs, int periodMs, int onMs) { return (nowMs % periodMs) < onMs; }
}  // namespace

void PanelService::init() {
  carrier_.configurePanel(cfg_.debounceMs, cfg_.longPressMs, cfg_.encFilterUs);
  carrier_.setLedResetUs(350);
  const uint32_t fault[5] = {scale(kRed, 100), 0, 0, 0, 0};
  carrier_.setFaultFrame(fault);
  const uint32_t off[5] = {0, 0, 0, 0, 0};
  carrier_.setLeds(off);  // start dark: no colour from a previous run is reused
  carrier_.sendLeds();
  memset(last_, 0, sizeof(last_));
  if (cfg_.watchdogMs > 0) {
    carrier_.setWatchdog(static_cast<uint32_t>(cfg_.watchdogMs));
    carrier_.kickWatchdog();
    watchdogOn_ = true;
  }
}

uint32_t PanelService::scale(uint32_t rgb, int percent) const {
  const int k = cfg_.brightness * percent / 100;  // 0..255
  const uint32_t r = ((rgb >> 16) & 0xFF) * k / 255, g = ((rgb >> 8) & 0xFF) * k / 255, b = (rgb & 0xFF) * k / 255;
  return (r << 16) | (g << 8) | b;
}

void PanelService::flash(Flash f, int64_t nowMs) {
  static const int kDur[3] = {300, 1000, 3000};
  flashUntil_[static_cast<int>(f)] = nowMs + kDur[static_cast<int>(f)];
}

void PanelService::render(int64_t now, uint32_t out[5]) const {
  // HL101 system
  switch (sys_) {
    case Sys::kInit: out[0] = scale(kYellow, 100); break;
    case Sys::kOk: out[0] = blink(now, 2000, 300) ? scale(kGreen, 100) : kOff; break;
    case Sys::kFault: out[0] = scale(kRed, 100); break;
    case Sys::kShutdown: out[0] = scale(kBlue, 100); break;
  }
  // HL102 network
  switch (net_) {
    case Net::kOff: out[1] = kOff; break;
    case Net::kConnecting: out[1] = blink(now, 1000, 500) ? scale(kBlue, 100) : kOff; break;
    case Net::kConnected: out[1] = scale(kGreen, 100); break;
    case Net::kFailed: out[1] = scale(kYellow, 100); break;
  }
  // HL103 measurement
  switch (meas_) {
    case Meas::kIdle: out[2] = kOff; break;
    case Meas::kCapturing: out[2] = blink(now, 500, 250) ? scale(kGreen, 100) : kOff; break;
    case Meas::kComputing: out[2] = scale(kBlue, 100); break;
    case Meas::kConditionBad: out[2] = scale(kYellow, 100); break;
  }
  // HL104 peripherals: failure > in progress > short success / tag read
  if (now < flashUntil_[static_cast<int>(Flash::kFailure)]) {
    out[3] = blink(now, 250, 125) ? scale(kRed, 100) : kOff;
  } else if (printing_) {
    out[3] = blink(now, 500, 250) ? scale(kYellow, 100) : kOff;
  } else if (now < flashUntil_[static_cast<int>(Flash::kSuccess)]) {
    out[3] = scale(kGreen, 100);
  } else if (now < flashUntil_[static_cast<int>(Flash::kNfc)]) {
    out[3] = scale(kBlue, 100);
  } else {
    out[3] = kOff;
  }
  // HL105 alarm level (kept after muting until the condition clears)
  switch (alarm_) {
    case AlarmLevel::kUnknown: out[4] = kOff; break;
    case AlarmLevel::kNormal: out[4] = scale(kGreen, 35); break;
    case AlarmLevel::kWarn: out[4] = scale(kYellow, 100); break;
    case AlarmLevel::kAlarm: out[4] = scale(kRed, 100); break;
  }
  if (sys_ == Sys::kShutdown) out[1] = out[2] = out[3] = out[4] = kOff;
}

void PanelService::tick(int64_t nowMs) {
  if (watchdogOn_ && nowMs - lastKickMs_ >= 500) {
    carrier_.kickWatchdog();
    lastKickMs_ = nowMs;
  }
  uint32_t frame[5];
  render(nowMs, frame);
  if (memcmp(frame, last_, sizeof(frame)) != 0) {
    carrier_.setLeds(frame);
    carrier_.sendLeds();
    memcpy(last_, frame, sizeof(frame));
  }
}

void PanelService::prepareShutdown() {
  carrier_.setWatchdog(0);
  watchdogOn_ = false;
  sys_ = Sys::kShutdown;
}

void PanelService::onEvent(const CarrierEvent& e) {
  switch (e.source) {
    case kSrcKey:
      if (e.level) {
        keyLongSeen_ = false;
      } else if (!keyLongSeen_ && cb_.keyShort) {
        cb_.keyShort();  // a long press never also yields a short press
      }
      break;
    case kSrcKeyLong:
      if (e.level) {
        keyLongSeen_ = true;
        if (cb_.keyLong) cb_.keyLong();
      }
      break;
    case kSrcEncSw:
      if (e.level) {
        swLongSeen_ = false;
      } else if (!swLongSeen_ && cb_.encoderPress) {
        cb_.encoderPress();
      }
      break;
    case kSrcEncSwLong:
      if (e.level) {
        swLongSeen_ = true;
        if (cb_.encoderLong) cb_.encoderLong();
      }
      break;
    case kSrcEncStep: {
      encAccum_ += (e.level != cfg_.reverse) ? 1 : -1;
      const int per = cfg_.countsPerDetent > 0 ? cfg_.countsPerDetent : 2;
      const int detents = encAccum_ / per;
      if (detents != 0) {
        encAccum_ -= detents * per;
        if (cb_.encoderSteps) cb_.encoderSteps(detents);
      }
      break;
    }
    default:
      break;
  }
}

}  // namespace vm
