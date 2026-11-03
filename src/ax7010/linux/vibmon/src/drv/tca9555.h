#pragma once

#include <cstdint>
#include <mutex>

#include "hw/i2c_bus.h"

namespace vm {

// U801 TCA9555 (0x20). Output bits are changed only through the shadow
// copy under one lock, so concurrent services never overwrite each other.
class Tca9555 {
 public:
  explicit Tca9555(I2cBus& bus, uint8_t addr = 0x20) : bus_(bus), addr_(addr) {}

  // Writes outputs to 0 first, polarity to normal, then the direction
  // registers (1 = input). Carrier: config0 0x0F, config1 0xD0.
  bool init(uint8_t config0, uint8_t config1);
  bool setOutputs(int port, uint8_t mask, uint8_t values, bool verify = false);
  bool setBit(int port, int bit, bool value) {
    return setOutputs(port, static_cast<uint8_t>(1u << bit), value ? static_cast<uint8_t>(1u << bit) : 0);
  }
  // Reading both input registers also releases /INT (EXP_INT_N).
  bool readInputs(uint8_t& port0, uint8_t& port1);
  uint8_t shadow(int port) const { return out_[port & 1]; }
  bool initialized() const { return initialized_; }

 private:
  I2cBus& bus_;
  uint8_t addr_;
  std::mutex mutex_;
  uint8_t out_[2] = {0, 0};
  bool initialized_ = false;
};

// Carrier bit assignment (成品指南 §12.2).
namespace tca {
constexpr int kLightOkPort = 0, kLightOkBit = 0;
constexpr int kTofDisablePort = 0, kTofDisableBit = 4;
constexpr int kEspResetPort = 0, kEspResetBit = 5;
constexpr int kPrnPwrPort = 0, kPrnPwrBit = 6;
constexpr int kPrnComPort = 0, kPrnComBit = 7;
constexpr int kAlmPort = 1, kAlmRedBit = 0, kAlmYellowBit = 1, kAlmGreenBit = 2, kBuzzBit = 3, kFanBit = 5;
constexpr int kInaAlertPort = 1, kInaAlertBit = 6;
constexpr uint8_t kConfig0 = 0x0F;  // P0.4..P0.7 outputs
constexpr uint8_t kConfig1 = 0xD0;  // P1.0..P1.3, P1.5 outputs; P1.4/6/7 inputs
}  // namespace tca

}  // namespace vm
