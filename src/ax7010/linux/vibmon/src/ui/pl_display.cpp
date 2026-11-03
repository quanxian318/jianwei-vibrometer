#include "ui/pl_display.h"

#include <cmath>
#include <algorithm>
#include <cstring>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {

#include "ui/dynclk_tables.inc"

// Panel timing as `cvt 1024 600 60`: 49.00 MHz, 1024 1072 1168 1312,
// 600 603 613 624, -hsync +vsync. Positions count from 0 (hmax/vmax = total - 1).
struct Mode {
  int width, hps, hpe, hmax, hpol, height, vps, vpe, vmax, vpol;
  double mhz;
};
constexpr Mode kMode{1024, 1072, 1168, 1311, 0, 600, 603, 613, 623, 1, 49.0};

// axi_dynclk (Digilent)
constexpr uint32_t kClkCtrl = 0x00, kClkStatus = 0x04, kClkL = 0x08, kClkFbL = 0x0C, kClkFbHClkH = 0x10,
                   kClkDiv = 0x14, kClkLockL = 0x18, kClkFltrLockH = 0x1C;
// v_tc generator (PG016, xvtc_hw.h)
constexpr uint32_t kVtcCtl = 0x000, kVtcGAsize = 0x060, kVtcGEnc = 0x068, kVtcGPol = 0x06C, kVtcGHsize = 0x070,
                   kVtcGVsize = 0x074, kVtcGHsync = 0x078, kVtcGVbHoff = 0x07C, kVtcGVsync = 0x080,
                   kVtcGVsHoff = 0x084, kVtcGVbHoffF1 = 0x088, kVtcGVsyncF1 = 0x08C, kVtcGVsHoffF1 = 0x090,
                   kVtcGAsizeF1 = 0x094;
constexpr uint32_t kVtcSwEnable = 0x1, kVtcRegUpdate = 0x2, kVtcGenEnable = 0x4, kVtcAllSources = 0x03FDEF00,
                   kVtcGeneratorSources = 0x03F7EF00;
// AXI VDMA MM2S (PG020)
constexpr uint32_t kMm2sCr = 0x00, kMm2sSr = 0x04, kMm2sFrmStore = 0x18, kParkPtr = 0x28, kMm2sVsize = 0x50,
                   kMm2sHsize = 0x54, kMm2sStride = 0x58, kMm2sAddr0 = 0x5C;

uint32_t clkDivider(uint32_t divide) {
  if (divide == 1) return 0x1041;
  const uint32_t high = divide / 2, low = high + (divide & 1);
  return ((divide & 1) << 13) | (0x03F & low) | (0xFC0 & (high << 6));
}

uint32_t clkCount(uint32_t divide) {
  const uint32_t d = clkDivider(divide);
  return (0xFFF & d) | ((d << 10) & 0x00C00000);
}

bool waitBits(PhysWindow& w, uint32_t offset, uint32_t mask, uint32_t value) {
  const int64_t t0 = monoMs();
  while ((w.read32(offset) & mask) != value) {
    if (monoMs() - t0 > 1000) return false;
    sleepMs(1);
  }
  return true;
}

}  // namespace

PlDisplay::~PlDisplay() {
  if (vdma_.isOpen()) vdma_.write32(kMm2sCr, 0);
}

bool PlDisplay::open(const PlDisplayConfig& cfg, std::string& err) {
  cfg_ = cfg;
  if (!reservedForDevices(cfg.bufferBase, static_cast<uint64_t>(kStores) * cfg.bufferStride)) {
    err = "framebuffer DDR is not reserved (prepare image.ub)";
    return false;
  }
  if (!vdma_.open(cfg.vdmaBase, 0x1000) || !vtc_.open(cfg.vtcBase, 0x1000) || !clk_.open(cfg.dynclkBase, 0x1000) ||
      !mem_.open(cfg.bufferBase, static_cast<size_t>(kStores) * cfg.bufferStride)) {
    err = "cannot map the video registers or framebuffer through /dev/mem";
    return false;
  }
  for (int k = 0; k < kStores; ++k) memset(mem_.data() + k * cfg.bufferStride, 0, kWidth * kHeight * 3);
  if (!startClock(err)) return false;
  startTiming();
  if (!startDma(err)) return false;
  row_.resize(kWidth * 3);
  LOGI("ui", "PL video %dx%d@60 at 0x%08X", kWidth, kHeight, cfg.bufferBase);
  return true;
}

bool PlDisplay::startClock(std::string& err) {
  // Best MMCM setting for the 5x serial clock from the 100 MHz reference,
  // VCO 600..1200 MHz (search of the Digilent display driver).
  const double target = kMode.mhz * 5;
  double bestErr = 1e9;
  uint32_t bestDiv = 1, bestFb = 6, bestClkDiv = 1;
  for (uint32_t div = 1; div <= 10; ++div) {
    const uint32_t maxFb = div * 12 < 64 ? div * 12 : 64;
    const double mult = (100.0 / div) / target;
    for (uint32_t fb = div * 6; fb <= maxFb; ++fb) {
      const uint32_t clkDiv = static_cast<uint32_t>(mult * fb + 0.5);
      if (clkDiv < 1 || clkDiv > 128) continue;
      const double e = std::fabs((100.0 / div) / clkDiv * fb - target);
      if (e < bestErr) {
        bestErr = e;
        bestDiv = div;
        bestFb = fb;
        bestClkDiv = clkDiv;
      }
    }
  }
  const uint64_t lock = kLockLookup[bestFb - 1];
  clk_.write32(kClkL, clkCount(bestClkDiv));
  clk_.write32(kClkFbL, clkCount(bestFb));
  clk_.write32(kClkFbHClkH, 0);
  clk_.write32(kClkDiv, clkDivider(bestDiv));
  clk_.write32(kClkLockL, static_cast<uint32_t>(lock & 0xFFFFFFFF));
  clk_.write32(kClkFltrLockH,
               static_cast<uint32_t>((lock >> 32) & 0xFF) | ((kFilterLookupLow[bestFb - 1] << 16) & 0x03FF0000));
  clk_.write32(kClkCtrl, 0);
  if (!waitBits(clk_, kClkStatus, 1, 0)) {
    err = "pixel clock does not stop";
    return false;
  }
  clk_.write32(kClkCtrl, 1);
  if (!waitBits(clk_, kClkStatus, 1, 1)) {
    err = "pixel clock does not lock";
    return false;
  }
  return true;
}

void PlDisplay::startTiming() {
  // Register values of XVtc_SetGeneratorTiming() for this mode (origin mode 1).
  const Mode& m = kMode;
  const uint32_t hTotal = m.hmax + 1, vTotal = m.vmax + 1;
  const uint32_t vSyncStart = m.vps - 1, vSyncEnd = m.vpe - 1;
  uint32_t pol = vtc_.read32(kVtcGPol) & ~0x7Fu;
  pol |= 0x70;                    // active chroma, active video, field id high
  if (m.vpol) pol |= 0x01 | 0x04;  // vblank, vsync
  if (m.hpol) pol |= 0x02 | 0x08;  // hblank, hsync
  vtc_.write32(kVtcCtl, vtc_.read32(kVtcCtl) | kVtcRegUpdate);
  vtc_.write32(kVtcGPol, pol);
  vtc_.write32(kVtcGHsize, hTotal);
  vtc_.write32(kVtcGVsize, vTotal | (vTotal << 16));
  vtc_.write32(kVtcGAsize, m.width | (m.height << 16));
  vtc_.write32(kVtcGAsizeF1, m.height << 16);
  vtc_.write32(kVtcGHsync, m.hps | (m.hpe << 16));
  vtc_.write32(kVtcGVsync, vSyncStart | (vSyncEnd << 16));
  vtc_.write32(kVtcGVsyncF1, vSyncStart | (vSyncEnd << 16));
  vtc_.write32(kVtcGEnc, vtc_.read32(kVtcGEnc) & ~0x140u);  // progressive, chroma parity 0
  for (uint32_t off : {kVtcGVbHoff, kVtcGVbHoffF1}) vtc_.write32(off, m.width | (m.width << 16));
  for (uint32_t off : {kVtcGVsHoff, kVtcGVsHoffF1}) vtc_.write32(off, m.hps | (m.hps << 16));
  const uint32_t ctl = (vtc_.read32(kVtcCtl) & ~kVtcAllSources) | kVtcGeneratorSources;
  vtc_.write32(kVtcCtl, ctl | kVtcRegUpdate | kVtcGenEnable | kVtcSwEnable);
}

bool PlDisplay::startDma(std::string& err) {
  vdma_.write32(kMm2sCr, 1u << 2);  // reset
  if (!waitBits(vdma_, kMm2sCr, 1u << 2, 0)) {
    err = "display VDMA reset does not complete";
    return false;
  }
  vdma_.write32(kMm2sFrmStore, kStores);
  vdma_.write32(kMm2sSr, 0x7FF0);
  for (int k = 0; k < kStores; ++k) vdma_.write32(kMm2sAddr0 + 4 * k, cfg_.bufferBase + k * cfg_.bufferStride);
  vdma_.write32(kMm2sHsize, kWidth * 3);
  vdma_.write32(kMm2sStride, kWidth * 3);
  vdma_.write32(kParkPtr, (vdma_.read32(kParkPtr) & ~31u) | static_cast<uint32_t>(shown_));
  vdma_.write32(kMm2sCr, 1);  // run, park mode (no circular)
  vdma_.write32(kMm2sVsize, kHeight);  // starts the channel
  return true;
}

void PlDisplay::present(const Canvas& c) {
  ++generation_;  // controls or another page changed outside the camera card
  copyAndPresent(c, 0, 0, kWidth, kHeight);
}

void PlDisplay::presentRegion(const Canvas& c, int x, int y, int w, int h) {
  copyAndPresent(c, x, y, w, h);
}

void PlDisplay::copyAndPresent(const Canvas& c, int x, int y, int width, int height) {
  // A requested park slot is not necessarily the slot being scanned yet.
  // Exclude BOTH until VDMA acknowledges the switch; CPU-side rotation alone
  // can wrap around and overwrite an image still being sent to HDMI.
  const uint32_t park = vdma_.read32(kParkPtr);
  const int scanning = (park >> 16) & 31;
  const int requested = park & 31;
  int next = (shown_ + 1) % kStores;
  for (int i = 0; i < kStores; ++i) {
    const int candidate = (shown_ + 1 + i) % kStores;
    if (candidate != scanning && candidate != requested) { next = candidate; break; }
  }
  uint8_t* store = mem_.data() + next * cfg_.bufferStride;
  // Every store must receive the latest full-page state before region copies
  // can reuse it. Otherwise triple-buffer rotation resurrects old controls.
  if (storeGeneration_[next] != generation_) {
    x = y = 0;
    width = kWidth;
    height = kHeight;
  }
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min({c.width(), kWidth, x + width});
  const int y1 = std::min({c.height(), kHeight, y + height});
  const int w = std::max(0, x1 - x0);
  for (int y = y0; y < y1; ++y) {
    // 0x00RRGGBB in memory is B, G, R, 0: drop every fourth byte.
    const uint8_t* src = reinterpret_cast<const uint8_t*>(c.data() + static_cast<size_t>(y) * c.width() + x0);
    uint8_t* dst = row_.data();
    int x = 0;
#if defined(__ARM_NEON)
    for (; x + 16 <= w; x += 16) {
      const uint8x16x4_t p = vld4q_u8(src + 4 * x);
      vst3q_u8(dst + 3 * x, uint8x16x3_t{{p.val[0], p.val[1], p.val[2]}});
    }
#endif
    for (; x < w; ++x) memcpy(dst + 3 * x, src + 4 * x, 3);
    memcpy(store + (static_cast<size_t>(y) * kWidth + x0) * 3, row_.data(), static_cast<size_t>(w) * 3);
  }
  storeGeneration_[next] = generation_;
  // Reserved DDR is write-combined, whereas VDMA registers are device MMIO.
  // Finish all pixel writes before publishing the new park slot to hardware.
#if defined(__arm__)
  asm volatile("dsb sy" ::: "memory");
#else
  __sync_synchronize();
#endif
  // The reader switches at the next frame start, so the shown store is never written.
  vdma_.write32(kParkPtr, (vdma_.read32(kParkPtr) & ~31u) | static_cast<uint32_t>(next));
  shown_ = next;
}

}  // namespace vm
