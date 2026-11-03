#pragma once

#include <cstdint>
#include <functional>

#include "app/config.h"
#include "app/state.h"
#include "drv/carrier_ctrl.h"

namespace vm {

// HL101..HL105 semantics (成品指南 §5) and K101/EC11 handling.
// LEDs are rendered at 10 Hz and only rewritten when the frame changes; the
// PL watchdog is kicked from here, so a stalled main loop shows the PL fault
// frame (HL101 red) and switches the lights off.
class PanelService {
 public:
  enum class Sys { kInit, kOk, kFault, kShutdown };
  enum class Net { kOff, kConnecting, kConnected, kFailed };
  enum class Meas { kIdle, kCapturing, kComputing, kConditionBad };
  enum class Flash { kNfc, kSuccess, kFailure };

  struct Callbacks {
    std::function<void()> keyShort;       // K101 short: alarm acknowledge / mute
    std::function<void()> keyLong;        // K101 >= long_press_ms: shutdown request
    std::function<void(int)> encoderSteps;  // EC11 detents (+ = clockwise as configured)
    std::function<void()> encoderPress;     // EC11 short press
    std::function<void()> encoderLong;      // EC11 >= long_press_ms (never also a short press)
  };

  PanelService(CarrierCtrl& carrier, const AppConfig::Panel& cfg) : carrier_(carrier), cfg_(cfg) {}
  void setCallbacks(Callbacks cb) { cb_ = std::move(cb); }
  void init();
  void onEvent(const CarrierEvent& e);
  void tick(int64_t nowMs);

  void setSystem(Sys s) { sys_ = s; }
  void setNetwork(Net n) { net_ = n; }
  void setMeasure(Meas m) { meas_ = m; }
  void setPrinting(bool on) { printing_ = on; }
  void flash(Flash f, int64_t nowMs);
  void setAlarm(AlarmLevel l) { alarm_ = l; }
  // Before Linux halts: stop the watchdog so the PL fault frame does not appear.
  void prepareShutdown();

 private:
  uint32_t scale(uint32_t rgb, int percent) const;
  void render(int64_t nowMs, uint32_t out[5]) const;

  CarrierCtrl& carrier_;
  const AppConfig::Panel& cfg_;
  Callbacks cb_;
  Sys sys_ = Sys::kInit;
  Net net_ = Net::kOff;
  Meas meas_ = Meas::kIdle;
  AlarmLevel alarm_ = AlarmLevel::kUnknown;
  bool printing_ = false;
  int64_t flashUntil_[3] = {0, 0, 0};
  uint32_t last_[5] = {0xFFFFFFFF, 0, 0, 0, 0};
  int64_t lastKickMs_ = 0;
  bool keyLongSeen_ = false;
  bool swLongSeen_ = false;
  int encAccum_ = 0;
  bool watchdogOn_ = false;
};

}  // namespace vm
