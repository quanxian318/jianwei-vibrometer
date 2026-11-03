#pragma once

#include <cstddef>
#include <cstdint>

#include "hw/uio.h"

namespace vm {

// A physical address range mapped uncached through /dev/mem: PL registers
// and DMA frame buffers of designs without UIO nodes (the team's camera
// design). Register accesses are single 32-bit loads/stores. Reserved DDR
// that the kernel still maps (reserved-memory without no-map) is mapped
// write-combined, so frame reads can burst; user space cannot invalidate the
// data cache on ARMv7, so cached access to DMA buffers is not an option.
class PhysWindow : public RegIo {
 public:
  ~PhysWindow() override;
  bool open(uint64_t phys, size_t size);
  void close();
  bool isOpen() const { return base_ != nullptr; }
  size_t size() const { return size_; }
  uint32_t read32(uint32_t offset) override;
  void write32(uint32_t offset, uint32_t value) override;
  // A read that survives a bus error, i.e. no AXI slave at the address (a
  // bitstream without the core): false instead of a SIGBUS for the process.
  bool tryRead32(uint32_t offset, uint32_t& value);
  const uint8_t* bytes() const { return base_; }
  uint8_t* data() { return base_; }

 private:
  void* map_ = nullptr;
  size_t mapSize_ = 0, size_ = 0;
  uint8_t* base_ = nullptr;
};

// True when [base, base + size) is in a device-tree reserved-memory region or
// outside the kernel's System RAM, i.e. no kernel allocation can land there.
bool reservedForDevices(uint64_t base, uint64_t size);

}  // namespace vm
