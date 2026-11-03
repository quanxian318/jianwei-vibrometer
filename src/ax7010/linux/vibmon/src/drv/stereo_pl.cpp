#include "drv/stereo_pl.h"

#include <fcntl.h>
#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <thread>

#include "drv/stereo_regs.h"
#include "hw/phys_window.h"
#include "hw/sysfs_find.h"
#include "util/log.h"

namespace vm {

namespace {

const char* kTag = "stereo_pl";

// Registers of one extra UIO map.
class MappedRegs : public RegIo {
 public:
  explicit MappedRegs(volatile uint32_t* p) : p_(p) {}
  uint32_t read32(uint32_t offset) override { return p_[offset / 4]; }
  void write32(uint32_t offset, uint32_t value) override { p_[offset / 4] = value; }

 private:
  volatile uint32_t* p_;
};

bool readSysfsHex(const std::string& path, uint64_t& v) {
  std::ifstream f(path);
  std::string s;
  if (!(f >> s)) return false;
  v = strtoull(s.c_str(), nullptr, 0);
  return true;
}

class UioStereoPlIo : public StereoPlIo {
 public:
  ~UioStereoPlIo() override {
    if (dma_) munmap(const_cast<uint32_t*>(dma_), dmaSize_);
    if (buf_) munmap(const_cast<uint32_t*>(buf_), bufSize_);
  }
  bool open(uint64_t coreBase, std::string& err) {
    const std::string dev = findUioByBase(coreBase);
    if (dev.empty()) {
      char b[64];
      snprintf(b, sizeof(b), "no UIO node at 0x%llx", static_cast<unsigned long long>(coreBase));
      err = b;
      return false;
    }
    const std::string sys = "/sys/class/uio/" + dev.substr(5) + "/maps/";
    uint64_t dmaSize = 0, bufAddr = 0, bufSize = 0;
    if (!readSysfsHex(sys + "map1/size", dmaSize) || !readSysfsHex(sys + "map2/addr", bufAddr) ||
        !readSysfsHex(sys + "map2/size", bufSize)) {
      err = dev + ": maps 1 (DMA) and 2 (buffer) missing; check stereo_generated.dtsi";
      return false;
    }
    if (!uio_.open(dev, 0x1000)) {
      err = "cannot open " + dev;
      return false;
    }
    const long page = sysconf(_SC_PAGESIZE);
    void* d = mmap(nullptr, dmaSize, PROT_READ | PROT_WRITE, MAP_SHARED, uio_.fd(), 1 * page);
    void* b = mmap(nullptr, bufSize, PROT_READ | PROT_WRITE, MAP_SHARED, uio_.fd(), 2 * page);
    if (d == MAP_FAILED || b == MAP_FAILED) {
      if (d != MAP_FAILED) munmap(d, dmaSize);
      if (b != MAP_FAILED) munmap(b, bufSize);
      err = dev + ": mmap of DMA registers or buffer failed";
      return false;
    }
    dma_ = static_cast<volatile uint32_t*>(d);
    buf_ = static_cast<volatile uint32_t*>(b);
    dmaSize_ = dmaSize;
    bufSize_ = bufSize;
    bufPhys_ = static_cast<uint32_t>(bufAddr);
    dmaRegs_.reset(new MappedRegs(dma_));
    LOGI(kTag, "%s: buffer 0x%08x (%zu bytes)", dev.c_str(), bufPhys_, bufSize_);
    return true;
  }
  RegIo& core() override { return uio_; }
  RegIo& dma() override { return *dmaRegs_; }
  volatile uint32_t* buffer() override { return buf_; }
  uint32_t bufferPhys() const override { return bufPhys_; }
  size_t bufferSize() const override { return bufSize_; }
  bool waitIrq(int timeoutMs) override {
    if (!uio_.enableIrq()) return false;
    pollfd p{uio_.fd(), POLLIN, 0};
    if (poll(&p, 1, timeoutMs) <= 0) return false;
    return uio_.ackIrq();
  }

 private:
  UioDevice uio_;
  volatile uint32_t* dma_ = nullptr;
  volatile uint32_t* buf_ = nullptr;
  size_t dmaSize_ = 0, bufSize_ = 0;
  uint32_t bufPhys_ = 0;
  std::unique_ptr<MappedRegs> dmaRegs_;
};

class PhysStereoPlIo : public StereoPlIo {
 public:
  bool open(uint64_t coreBase, uint64_t dmaBase, uint64_t bufBase, size_t bufSize, std::string& err) {
    // Register accesses to an unconfigured PL hang the bus.
    std::ifstream done("/sys/class/xdevcfg/xdevcfg/device/prog_done");
    int configured = 1;
    if (done && (done >> configured) && configured != 1) {
      err = "PL not configured";
      return false;
    }
    if (!reservedForDevices(bufBase, bufSize)) {
      err = "stereo DMA buffer is not reserved DDR";
      return false;
    }
    if (!core_.open(coreBase, 0x1000) || !dma_.open(dmaBase, 0x10000) || !buf_.open(bufBase, bufSize)) {
      err = "cannot map the stereo core, DMA or buffer through /dev/mem";
      return false;
    }
    bufPhys_ = static_cast<uint32_t>(bufBase);
    return true;
  }
  RegIo& core() override { return core_; }
  RegIo& dma() override { return dma_; }
  volatile uint32_t* buffer() override { return reinterpret_cast<volatile uint32_t*>(buf_.data()); }
  uint32_t bufferPhys() const override { return bufPhys_; }
  size_t bufferSize() const override { return buf_.size(); }
  bool waitIrq(int) override { return false; }

 private:
  PhysWindow core_, dma_, buf_;
  uint32_t bufPhys_ = 0;
};

}  // namespace

std::unique_ptr<StereoPlIo> openStereoUio(uint64_t coreBase, std::string& err) {
  std::unique_ptr<UioStereoPlIo> io(new UioStereoPlIo);
  if (!io->open(coreBase, err)) return nullptr;
  return std::unique_ptr<StereoPlIo>(io.release());
}

std::unique_ptr<StereoPlIo> openStereoPhys(uint64_t coreBase, uint64_t dmaBase, uint64_t bufferBase,
                                           size_t bufferSize, std::string& err) {
  std::unique_ptr<PhysStereoPlIo> io(new PhysStereoPlIo);
  if (!io->open(coreBase, dmaBase, bufferBase, bufferSize, err)) return nullptr;
  return std::unique_ptr<StereoPlIo>(io.release());
}

bool PlStereo::probe(std::string& err) {
  using namespace stereo_reg;
  RegIo& c = io_->core();
  if (c.read32(kId) != kIdValue) {
    err = "stereo core ID mismatch";
    return false;
  }
  if ((c.read32(kVersion) >> 16) != (kVersionValue >> 16)) {
    err = "stereo core major version mismatch";
    return false;
  }
  // The PS post-processing and the model fallback assume these parameters.
  const SgmParams p = plCoreParams();
  const uint32_t want = static_cast<uint32_t>(p.maxDisp) | (static_cast<uint32_t>(p.p1) << 8) |
                        (static_cast<uint32_t>(p.p2) << 16) | (static_cast<uint32_t>(p.uniquenessPct) << 24) |
                        (static_cast<uint32_t>(p.lrTolerance) << 28);
  const uint32_t cfg = c.read32(kConfig);
  if (cfg != want) {
    char b[96];
    snprintf(b, sizeof(b), "stereo core CONFIG 0x%08x, driver expects 0x%08x", cfg, want);
    err = b;
    return false;
  }
  maxW_ = static_cast<int>(c.read32(kMaxW));
  return true;
}

bool PlStereo::waitDone(int timeoutMs) {
  using namespace stereo_reg;
  const auto t0 = std::chrono::steady_clock::now();
  if (io_->waitIrq(timeoutMs) && (io_->core().read32(kStatus) & kStDone)) return true;
  // No interrupt (or a spurious one): poll the status register.
  while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(timeoutMs)) {
    if (io_->core().read32(kStatus) & kStDone) return true;
    std::this_thread::sleep_for(std::chrono::microseconds(500));
  }
  return (io_->core().read32(kStatus) & kStDone) != 0;
}

bool PlStereo::match(const StereoImage& l, const StereoImage& r, DisparityMap& raw, std::string& err) {
  using namespace stereo_reg;
  using namespace axidma;
  const int w = l.w, h = l.h;
  const size_t n = static_cast<size_t>(w) * h;
  if (r.w != w || r.h != h || w < 16 || h < 16 || (maxW_ > 0 && w > maxW_) || (w & 1)) {
    err = "unsupported image size for the PL core";
    return false;
  }
  if (kOutOffset + 2 * n > io_->bufferSize() || 2 * n > kOutOffset) {
    err = "DMA buffer too small";
    return false;
  }
  RegIo& c = io_->core();
  RegIo& d = io_->dma();
  volatile uint32_t* buf = io_->buffer();

  // Input: one 16-bit beat per pixel {left, right}; memory little endian ->
  // byte 2i = right, 2i+1 = left. Word-wise writes (uncached device memory).
  for (size_t i = 0; i < n; i += 2)
    buf[i / 2] = static_cast<uint32_t>(r.px[i]) | (static_cast<uint32_t>(l.px[i]) << 8) |
                 (static_cast<uint32_t>(r.px[i + 1]) << 16) | (static_cast<uint32_t>(l.px[i + 1]) << 24);

  // The buffer may be write-combined (/dev/mem): drain it before the DMA reads.
#if defined(__arm__)
  asm volatile("dsb sy" ::: "memory");
#endif

  // Core first (its input stream waits for START), then S2MM, then MM2S.
  c.write32(kStatus, kStDone | kStOverflow | kStBadSize);
  c.write32(kWidth, static_cast<uint32_t>(w));
  c.write32(kHeight, static_cast<uint32_t>(h));
  c.write32(kIrqEn, 1);
  for (uint32_t cr : {kMm2sCr, kS2mmCr}) d.write32(cr, kCrReset);
  for (int i = 0; i < 100 && ((d.read32(kMm2sCr) | d.read32(kS2mmCr)) & kCrReset); ++i)
    std::this_thread::sleep_for(std::chrono::microseconds(10));
  c.write32(kCtrl, kCtrlStart);
  if (c.read32(kStatus) & kStBadSize) {
    err = "core refused the frame size";
    return false;
  }
  d.write32(kS2mmCr, kCrRun);
  d.write32(kS2mmDa, io_->bufferPhys() + kOutOffset);
  d.write32(kS2mmLength, static_cast<uint32_t>(2 * n));
  d.write32(kMm2sCr, kCrRun);
  d.write32(kMm2sSa, io_->bufferPhys());
  d.write32(kMm2sLength, static_cast<uint32_t>(2 * n));

  // About 67 clocks per pixel; allow 4x at 100 MHz plus margin.
  const int timeoutMs = static_cast<int>(n * 67 * 4 / 100000) + 50;
  if (!waitDone(timeoutMs)) {
    err = "timeout waiting for the stereo core";
    c.write32(kCtrl, kCtrlSoftReset);
    return false;
  }
  // The core is done once its last output entered the FIFO; S2MM drains it.
  bool idle = false;
  for (int i = 0; i < 200 && !idle; ++i) {
    idle = (d.read32(kS2mmSr) & (kSrIdle | kSrHalted)) != 0;
    if (!idle) std::this_thread::sleep_for(std::chrono::microseconds(50));
  }
  const uint32_t st = c.read32(kStatus), s2 = d.read32(kS2mmSr), m2 = d.read32(kMm2sSr);
  lastCycles_ = c.read32(kCycles);
  c.write32(kStatus, kStDone | kStOverflow);
  if (!idle || (s2 & kSrErrors) || (m2 & kSrErrors)) {
    err = "DMA did not complete cleanly";
    return false;
  }
  if (st & kStOverflow) {
    err = "output FIFO overflow (S2MM stalled)";
    return false;
  }
  raw.w = w;
  raw.h = h;
  raw.filled.clear();
  raw.q4.resize(n);
  const volatile uint32_t* out = buf + kOutOffset / 4;
  for (size_t i = 0; i < n; i += 2) {
    const uint32_t v = out[i / 2];
    raw.q4[i] = static_cast<int16_t>(v & 0xffff);
    raw.q4[i + 1] = static_cast<int16_t>(v >> 16);
  }
  return true;
}

}  // namespace vm
