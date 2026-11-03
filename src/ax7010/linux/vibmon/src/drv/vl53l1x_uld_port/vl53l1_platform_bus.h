#pragma once

#include <cstdint>

namespace vm {
class I2cBus;
}

// Associates a ULD "dev" (7-bit address) with the I2C bus it lives on.
void vl53l1PlatformAttach(uint16_t dev, vm::I2cBus* bus);
