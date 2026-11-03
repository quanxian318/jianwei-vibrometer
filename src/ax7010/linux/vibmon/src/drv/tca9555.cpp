#include "drv/tca9555.h"

namespace vm {

namespace {
constexpr uint8_t kRegInput0 = 0x00, kRegOutput0 = 0x02, kRegPolarity0 = 0x04, kRegConfig0 = 0x06;
}

bool Tca9555::init(uint8_t config0, uint8_t config1) {
  std::lock_guard<std::mutex> lock(mutex_);
  initialized_ = false;
  const uint8_t out[3] = {kRegOutput0, 0x00, 0x00};
  const uint8_t pol[3] = {kRegPolarity0, 0x00, 0x00};
  const uint8_t cfg[3] = {kRegConfig0, config0, config1};
  if (!bus_.writeBytes(addr_, out, 3) || !bus_.writeBytes(addr_, pol, 3) || !bus_.writeBytes(addr_, cfg, 3)) {
    return false;
  }
  // Read back so a failed configuration is never mistaken for "loads off".
  uint8_t rb[2];
  if (!bus_.readRegs(addr_, kRegConfig0, rb, 2) || rb[0] != config0 || rb[1] != config1) return false;
  if (!bus_.readRegs(addr_, kRegOutput0, rb, 2) || rb[0] != 0 || rb[1] != 0) return false;
  out_[0] = out_[1] = 0;
  initialized_ = true;
  return true;
}

bool Tca9555::setOutputs(int port, uint8_t mask, uint8_t values, bool verify) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) return false;
  port &= 1;
  const uint8_t next = static_cast<uint8_t>((out_[port] & ~mask) | (values & mask));
  if (!bus_.writeReg8(addr_, static_cast<uint8_t>(kRegOutput0 + port), next)) return false;
  out_[port] = next;
  if (verify) {
    uint8_t output = 0, config = 0;
    if (!bus_.readRegs(addr_, static_cast<uint8_t>(kRegOutput0 + port), &output, 1) ||
        !bus_.readRegs(addr_, static_cast<uint8_t>(kRegConfig0 + port), &config, 1) ||
        (output & mask) != (next & mask) || (config & mask) != 0) return false;
  }
  return true;
}

bool Tca9555::readInputs(uint8_t& port0, uint8_t& port1) {
  std::lock_guard<std::mutex> lock(mutex_);
  uint8_t b[2];
  if (!bus_.readRegs(addr_, kRegInput0, b, 2)) return false;
  port0 = b[0];
  port1 = b[1];
  return true;
}

}  // namespace vm
