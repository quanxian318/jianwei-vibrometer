#pragma once

#include <cstdint>
#include <string>

// Locates Linux device nodes of the PL IP cores by their AXI base address
// (device-tree "reg"), so the configuration never hard-codes /dev numbers.
namespace vm {

std::string findI2cDevByBase(uint64_t base);           // "/dev/i2c-N"
std::string findTtyByBase(uint64_t base);              // "/dev/ttySN"
std::string findSpidevByBase(uint64_t base, int cs);   // "/dev/spidevB.C"
std::string findUioByBase(uint64_t base);              // "/dev/uioN"

// First address cell of a device-tree node's "reg" (Zynq-7000: 32-bit cells).
bool readOfNodeBase(const std::string& ofNodeDir, uint64_t& base);

}  // namespace vm
