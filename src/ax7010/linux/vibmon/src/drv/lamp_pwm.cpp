#include "drv/lamp_pwm.h"
#include <algorithm>
#include <cmath>

namespace vm {
bool LampPwm::open(uint64_t base) {
  uint32_t id = 0;
  if (!io_.open(base, 4096) || !io_.tryRead32(8, id) ||
      (id & ~15u) != 0x4c503200u) { io_.close(); return false; }
  ready_ = true;
  command_ = 0;
  io_.write32(0, command_);  // enable is always off at application startup
  tick();
  return true;
}

bool LampPwm::tick() {
  if (!ready_) return false;
  const uint32_t status = io_.read32(8);
  const bool healthy = (status & 3u) == 3u;
  if (!healthy) command_ &= ~3u;  // neither output automatically relights
  command_ ^= 1u << 16;
  io_.write32(0, command_);
  return healthy;
}

bool LampPwm::set(bool on, double percent) {
  if (!ready_) return false;
  if (on && (io_.read32(8) & 3u) != 3u) return false;
  const uint32_t pct = static_cast<uint32_t>(std::lround(std::max(0.0, std::min(100.0, percent))));
  command_ = (command_ & ((1u << 16) | 2u)) | (pct << 8) | (on ? 1u : 0u);
  io_.write32(0, command_);
  return io_.read32(0) == command_;
}

bool LampPwm::setProjector(bool on) {
  if (!ready_) return false;
  if (on && (io_.read32(8) & 3u) != 3u) return false;
  command_ = (command_ & ~2u) | (on ? 2u : 0u);
  io_.write32(0, command_);
  return io_.read32(0) == command_;
}
}  // namespace vm
