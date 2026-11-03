// PL stereo driver (drv/stereo_pl) against an emulated stereo_axi + AXI DMA,
// and the register map against the RTL (pl/stereo/rtl/stereo_axi.v).
#include <cmath>
#include <cstdio>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "drv/stereo_pl.h"
#include "drv/stereo_regs.h"
#include "stereo/stereo_service.h"
#include "test_util.h"

using namespace vm;

namespace {

// Emulation of the block design: register semantics of stereo_axi.v and an
// AXI DMA in simple mode moving data between the buffer and the core model.
struct FakeHw {
  bool overflow = false, hang = false, corrupt = false;
  uint32_t config = 0;
  std::vector<uint32_t> mem = std::vector<uint32_t>(0x100000 / 4, 0);
  static constexpr uint32_t kPhys = 0x1F000000;
  std::map<uint32_t, uint32_t> core, dma;
  bool started = false;
  int frames = 0;
  std::vector<std::string> log;  // register write order

  FakeHw() {
    const SgmParams p = plCoreParams();
    config = static_cast<uint32_t>(p.maxDisp) | (p.p1 << 8) | (p.p2 << 16) | (p.uniquenessPct << 24) |
             (p.lrTolerance << 28);
  }
  void run() {
    using namespace axidma;
    const uint32_t w = core[stereo_reg::kWidth], h = core[stereo_reg::kHeight];
    const size_t n = static_cast<size_t>(w) * h;
    if (!started || !(dma[kS2mmCr] & kCrRun) || dma[kS2mmLength] != 2 * n || dma[kMm2sLength] != 2 * n) return;
    StereoImage l, r;
    l.w = r.w = static_cast<int>(w);
    l.h = r.h = static_cast<int>(h);
    l.px.resize(n);
    r.px.resize(n);
    const uint32_t* in = &mem[(dma[kMm2sSa] - kPhys) / 4];
    for (size_t i = 0; i < n; ++i) {
      const uint32_t v = in[i / 2] >> (16 * (i & 1));
      r.px[i] = static_cast<uint8_t>(v);
      l.px[i] = static_cast<uint8_t>(v >> 8);
    }
    DisparityMap d = plCoreModel(l, r);
    if (corrupt) d.q4[n / 2] = static_cast<int16_t>(d.q4[n / 2] + 1);
    uint32_t* out = &mem[(dma[kS2mmDa] - kPhys) / 4];
    for (size_t i = 0; i < n; i += 2)
      out[i / 2] = static_cast<uint16_t>(d.q4[i]) | (static_cast<uint32_t>(static_cast<uint16_t>(d.q4[i + 1])) << 16);
    if (hang) return;
    core[stereo_reg::kStatus] |= stereo_reg::kStDone | (overflow ? stereo_reg::kStOverflow : 0);
    core[stereo_reg::kCycles] = static_cast<uint32_t>(n * 67 + 180);
    core[stereo_reg::kFrames] = static_cast<uint32_t>(++frames);
    dma[kMm2sSr] = dma[kS2mmSr] = kSrIdle;
    started = false;
  }
};

class FakeCore : public RegIo {
 public:
  explicit FakeCore(FakeHw& hw) : hw_(hw) {}
  uint32_t read32(uint32_t o) override {
    using namespace stereo_reg;
    switch (o) {
      case kId: return kIdValue;
      case kVersion: return kVersionValue;
      case kConfig: return hw_.config;
      case kMaxW: return 320;
      default: return hw_.core[o];
    }
  }
  void write32(uint32_t o, uint32_t v) override {
    using namespace stereo_reg;
    char b[32];
    snprintf(b, sizeof(b), "core:%03x", o);
    hw_.log.push_back(b);
    if (o == kStatus) {
      hw_.core[o] &= ~(v & (kStDone | kStOverflow | kStBadSize));
    } else if (o == kCtrl) {
      if (v & kCtrlStart) {
        const uint32_t w = hw_.core[kWidth], h = hw_.core[kHeight];
        if (w >= 16 && w <= 320 && h >= 16) hw_.started = true;
        else hw_.core[kStatus] |= kStBadSize;
      }
    } else {
      hw_.core[o] = v;
    }
  }

 private:
  FakeHw& hw_;
};

class FakeDma : public RegIo {
 public:
  explicit FakeDma(FakeHw& hw) : hw_(hw) {}
  uint32_t read32(uint32_t o) override {
    if ((o == axidma::kMm2sCr || o == axidma::kS2mmCr)) return hw_.dma[o] & ~axidma::kCrReset;  // reset completes at once
    return hw_.dma[o];
  }
  void write32(uint32_t o, uint32_t v) override {
    char b[32];
    snprintf(b, sizeof(b), "dma:%02x", o);
    hw_.log.push_back(b);
    hw_.dma[o] = v;
    if (o == axidma::kMm2sLength) hw_.run();  // MM2S length starts the transfer
  }

 private:
  FakeHw& hw_;
};

class FakeIo : public StereoPlIo {
 public:
  explicit FakeIo(std::shared_ptr<FakeHw> hw) : hw_(hw), core_(*hw), dma_(*hw) {}
  RegIo& core() override { return core_; }
  RegIo& dma() override { return dma_; }
  volatile uint32_t* buffer() override { return hw_->mem.data(); }
  uint32_t bufferPhys() const override { return FakeHw::kPhys; }
  size_t bufferSize() const override { return hw_->mem.size() * 4; }
  bool waitIrq(int) override { return (hw_->core[stereo_reg::kStatus] & stereo_reg::kStDone) != 0; }

 private:
  std::shared_ptr<FakeHw> hw_;
  FakeCore core_;
  FakeDma dma_;
};

StereoImage texture(int w, int h, double shift, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<double> u(0, 1);
  double kx[20], ky[20], ph[20];
  for (int i = 0; i < 20; ++i) {
    const double k = 0.15 + 0.8 * u(rng), th = 2 * M_PI * u(rng);
    kx[i] = k * std::cos(th);
    ky[i] = k * std::sin(th);
    ph[i] = 2 * M_PI * u(rng);
  }
  StereoImage im;
  im.w = w;
  im.h = h;
  im.px.resize(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      double s = 0;
      for (int i = 0; i < 20; ++i) s += std::sin(kx[i] * (x + shift) + ky[i] * y + ph[i]);
      im.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, 128 + 20 * s)));
    }
  return im;
}

void testDriverMatchesModel() {
  auto hw = std::make_shared<FakeHw>();
  PlStereo pl(std::unique_ptr<StereoPlIo>(new FakeIo(hw)));
  std::string err;
  CHECK(pl.probe(err));
  CHECK(pl.maxWidth() == 320);
  const StereoImage l = texture(160, 90, 0, 3), r = texture(160, 90, 14.5, 3);
  DisparityMap raw;
  CHECK(pl.match(l, r, raw, err));
  const DisparityMap ref = plCoreModel(l, r);
  CHECK(raw.q4 == ref.q4);
  CHECK(raw.valid() > 160 * 90 / 2);
  CHECK(pl.lastCycles() == 160 * 90 * 67 + 180);
  // Programming order: core size/irq, DMA resets, START, then S2MM before MM2S.
  auto pos = [&](const char* s) {
    for (size_t i = 0; i < hw->log.size(); ++i)
      if (hw->log[i] == s) return static_cast<int>(i);
    return -1;
  };
  CHECK(pos("core:010") >= 0 && pos("core:010") < pos("core:018"));
  CHECK(pos("core:018") < pos("dma:58"));
  CHECK(pos("dma:58") < pos("dma:28"));
  std::printf("  driver: %dx%d bit-exact with the model, %d valid\n", raw.w, raw.h, raw.valid());
}

void testDriverErrors() {
  std::string err;
  const StereoImage l = texture(64, 32, 0, 5), r = texture(64, 32, 6, 5);
  DisparityMap raw;
  {
    auto hw = std::make_shared<FakeHw>();
    hw->config ^= 1;  // a core built with other parameters
    PlStereo pl(std::unique_ptr<StereoPlIo>(new FakeIo(hw)));
    CHECK(!pl.probe(err));
  }
  {
    auto hw = std::make_shared<FakeHw>();
    hw->overflow = true;
    PlStereo pl(std::unique_ptr<StereoPlIo>(new FakeIo(hw)));
    CHECK(pl.probe(err));
    CHECK(!pl.match(l, r, raw, err));
    CHECK(err.find("overflow") != std::string::npos);
  }
  {
    auto hw = std::make_shared<FakeHw>();
    hw->hang = true;
    PlStereo pl(std::unique_ptr<StereoPlIo>(new FakeIo(hw)));
    CHECK(pl.probe(err));
    CHECK(!pl.match(l, r, raw, err));
    CHECK(err.find("timeout") != std::string::npos);
  }
  {
    auto hw = std::make_shared<FakeHw>();
    PlStereo pl(std::unique_ptr<StereoPlIo>(new FakeIo(hw)));
    CHECK(pl.probe(err));
    const StereoImage wide = texture(400, 20, 0, 1);
    CHECK(!pl.match(wide, wide, raw, err));  // wider than MAXW
    const StereoImage odd = texture(65, 20, 0, 1);
    CHECK(!pl.match(odd, odd, raw, err));    // odd width (two pixels per DMA word)
  }
}

// stereo_regs.h against the localparams of stereo_axi.v.
void testRegisterMapMatchesRtl() {
  const std::string here = __FILE__;
  const std::string path = here.substr(0, here.rfind('/') + 1) + "../../../pl/stereo/rtl/stereo_axi.v";
  std::ifstream f(path);
#if defined(__arm__)
  if (!f.good()) {  // the source tree is normally not on the target board
    std::printf("  register map check skipped: %s not found\n", path.c_str());
    return;
  }
#endif
  CHECK(f.good());
  std::stringstream ss;
  ss << f.rdbuf();
  const std::string v = ss.str();
  auto param = [&](const std::string& name) -> long {
    std::smatch m;
    const std::regex re("\\b" + name + "\\s*=\\s*(?:\\d+'h)?([0-9A-Fa-f]+)");
    if (!std::regex_search(v, m, re)) return -1;
    const bool hex = m[0].str().find("'h") != std::string::npos;
    return std::stol(m[1].str(), nullptr, hex ? 16 : 10);
  };
  using namespace stereo_reg;
  CHECK(param("REG_ID") == kId);
  CHECK(param("REG_VERSION") == kVersion);
  CHECK(param("REG_CONFIG") == kConfig);
  CHECK(param("REG_MAXW") == kMaxW);
  CHECK(param("REG_WIDTH") == kWidth);
  CHECK(param("REG_HEIGHT") == kHeight);
  CHECK(param("REG_CTRL") == kCtrl);
  CHECK(param("REG_STATUS") == kStatus);
  CHECK(param("REG_IRQ_EN") == kIrqEn);
  CHECK(param("REG_CYCLES") == kCycles);
  CHECK(param("REG_FRAMES") == kFrames);
  CHECK(param("ID_VALUE") == static_cast<long>(kIdValue));
  CHECK(param("VERSION_VALUE") == static_cast<long>(kVersionValue));
  const SgmParams p = plCoreParams();
  CHECK(param("D") == p.maxDisp);
  CHECK(param("P1") == p.p1);
  CHECK(param("P2") == p.p2);
  CHECK(param("UNIQ") == p.uniquenessPct);
  CHECK(param("LRTOL") == p.lrTolerance);
}

GrayFrame frame(const StereoImage& im, int64_t t) {
  GrayFrame f;
  f.width = im.w;
  f.height = im.h;
  f.pixels = im.px;
  f.timestampNs = t;
  return f;
}

void testServiceUsesPl() {
  StereoConfig sc;
  sc.calibrationFile = "/nonexistent.json";
  sc.workWidth = 160;
  sc.workHeight = 90;
  sc.plVerifyEvery = 1;
  const StereoImage l = texture(160, 90, 0, 9), r = texture(160, 90, 12, 9);
  {
    auto hw = std::make_shared<FakeHw>();
    std::unique_ptr<PlStereo> pl(new PlStereo(std::unique_ptr<StereoPlIo>(new FakeIo(hw))));
    std::string err;
    CHECK(pl->probe(err));
    StereoService svc(sc, nullptr, nullptr, std::move(pl));
    auto d = svc.process(frame(l, 0), frame(r, 0));
    CHECK(d->backend == "PL");
    const StereoService::Stats st = svc.stats();
    const uint64_t passes = sc.plVerticalCheck ? 2 : 1;
    CHECK(st.plFrames == passes && st.plVerified == passes && st.plMismatchFrames == 0 && st.plFailures == 0);
  }
  {
    auto hw = std::make_shared<FakeHw>();
    hw->corrupt = true;  // the on-board check must notice a PL/model difference
    std::unique_ptr<PlStereo> pl(new PlStereo(std::unique_ptr<StereoPlIo>(new FakeIo(hw))));
    std::string err;
    pl->probe(err);
    StereoService svc(sc, nullptr, nullptr, std::move(pl));
    svc.process(frame(l, 0), frame(r, 0));
    CHECK(svc.stats().plMismatchFrames == (sc.plVerticalCheck ? 2u : 1u));
  }
  {
    auto hw = std::make_shared<FakeHw>();
    hw->hang = true;  // a failing PL frame falls back to the model and is counted
    std::unique_ptr<PlStereo> pl(new PlStereo(std::unique_ptr<StereoPlIo>(new FakeIo(hw))));
    std::string err;
    pl->probe(err);
    StereoService svc(sc, nullptr, nullptr, std::move(pl));
    auto d = svc.process(frame(l, 0), frame(r, 0));
    CHECK(d->backend == "PL-model");
    CHECK(svc.stats().plFailures == (sc.plVerticalCheck ? 2u : 1u));
  }
  {
    StereoService svc(sc, nullptr, nullptr);  // no device
    CHECK(svc.stats().plError.find("pl_device_missing") == 0);
    CHECK(svc.process(frame(l, 0), frame(r, 0))->backend == "PL-model");
  }
}

}  // namespace

int main() {
  testDriverMatchesModel();
  testDriverErrors();
  testRegisterMapMatchesRtl();
  testServiceUsesPl();
  return testSummary("stereo_pl");
}
