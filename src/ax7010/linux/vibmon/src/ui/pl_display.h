#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "hw/phys_window.h"
#include "ui/fb_display.h"

namespace vm {

// HDMI output of the team's camera design (AX7010_Sdoira_PL_V1), driven from
// user space because the ALINX kernel's display nodes describe a different PL:
// axi_dynclk makes the pixel and 5x serial clocks, the v_tc generator the
// 1024x600@60 CVT timing of the 7" panel, and an AXI VDMA MM2S channel reads
// 24-bit pixels from three frame stores in reserved DDR: byte 0 goes out as
// blue and byte 2 as red (rgb2dvi channel 0 / 2), the order the cameras store.
// present() fills a store that is not shown and parks the reader on it, so
// frames never tear.
struct PlDisplayConfig {
  uint32_t vdmaBase = 0x43000000, vtcBase = 0x43C00000, dynclkBase = 0x43C10000;
  // Store k at bufferBase + k * bufferStride; above the six camera stores in
  // the reserved 0x1E000000..0x1FFFFFFF region.
  uint32_t bufferBase = 0x1F200000, bufferStride = 2u << 20;
};

class PlDisplay : public Display {
 public:
  static constexpr int kWidth = 1024, kHeight = 600, kStores = 3;

  ~PlDisplay() override;
  bool open(const PlDisplayConfig& cfg, std::string& err);
  int width() const override { return kWidth; }
  int height() const override { return kHeight; }
  void present(const Canvas& c) override;
  void presentRegion(const Canvas& c, int x, int y, int w, int h) override;

 private:
  bool startClock(std::string& err);
  void startTiming();
  bool startDma(std::string& err);
  void copyAndPresent(const Canvas& c, int x, int y, int w, int h);

  PlDisplayConfig cfg_;
  PhysWindow vdma_, vtc_, clk_, mem_;
  std::vector<uint8_t> row_;
  int shown_ = 0;
  uint64_t generation_ = 1, storeGeneration_[kStores] = {};
};

}  // namespace vm
