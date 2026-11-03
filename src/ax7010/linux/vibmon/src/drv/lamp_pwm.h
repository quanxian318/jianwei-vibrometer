#pragma once
#include "hw/phys_window.h"

namespace vm {
// Small J301 lamp / J304 projector backend, distinct from full carrier_ctrl.
class LampPwm {
 public:
  bool open(uint64_t base);
  bool ready() const { return ready_; }
  bool set(bool on, double percent);
  bool setProjector(bool on);
  bool tick();  // heartbeat; false if PFAIL/watchdog requires output off
 private:
  PhysWindow io_;
  uint32_t command_ = 0;
  bool ready_ = false;
};
}  // namespace vm
