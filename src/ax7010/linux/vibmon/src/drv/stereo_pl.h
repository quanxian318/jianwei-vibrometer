#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "hw/uio.h"
#include "stereo/stereo_match.h"

namespace vm {

// Access to the PL stereo matcher: core registers, AXI DMA registers and the
// reserved DDR window both sides see (input at offset 0, output at kOutOffset).
class StereoPlIo {
 public:
  virtual ~StereoPlIo() = default;
  virtual RegIo& core() = 0;
  virtual RegIo& dma() = 0;
  virtual volatile uint32_t* buffer() = 0;  // uncached, 32-bit accesses only
  virtual uint32_t bufferPhys() const = 0;
  virtual size_t bufferSize() const = 0;
  // Waits for the core's DONE interrupt; false on timeout or without IRQ support.
  virtual bool waitIrq(int timeoutMs) = 0;
};

// UIO node from stereo_generated.dtsi: map0 core, map1 DMA, map2 buffer.
std::unique_ptr<StereoPlIo> openStereoUio(uint64_t coreBase, std::string& err);
// Designs without UIO nodes (the team's camera PL): registers and a reserved
// DDR window through /dev/mem; DONE is polled.
std::unique_ptr<StereoPlIo> openStereoPhys(uint64_t coreBase, uint64_t dmaBase, uint64_t bufferBase,
                                           size_t bufferSize, std::string& err);

// Driver of ax7010/pl/stereo (stereo_axi + AXI DMA). match() returns the raw
// disparity of the PL core, identical to sgmRaw() with plCoreParams(); the
// PS post-processing is applied by the caller (StereoService).
class PlStereo {
 public:
  static constexpr uint32_t kOutOffset = 0x80000;

  explicit PlStereo(std::unique_ptr<StereoPlIo> io) : io_(std::move(io)) {}
  // Checks ID, version and that the core's fixed parameters match plCoreParams().
  bool probe(std::string& err);
  bool match(const StereoImage& left, const StereoImage& right, DisparityMap& raw, std::string& err);
  uint32_t lastCycles() const { return lastCycles_; }
  int maxWidth() const { return maxW_; }

 private:
  bool waitDone(int timeoutMs);
  std::unique_ptr<StereoPlIo> io_;
  uint32_t lastCycles_ = 0;
  int maxW_ = 0;
};

}  // namespace vm
