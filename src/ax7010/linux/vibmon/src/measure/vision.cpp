#include "measure/vision.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>

#include "util/clock.h"

namespace vm {

namespace vision {

namespace {
inline int clampIdx(int i, int n) { return i < 0 ? 0 : (i >= n ? n - 1 : i); }
inline int16_t sat16(int32_t v) { return static_cast<int16_t>(v > 32767 ? 32767 : (v < -32768 ? -32768 : v)); }
}  // namespace

void pyrReduce(const uint8_t* in, int w, int h, std::vector<uint8_t>& out, int& ow, int& oh) {
  ow = (w + 1) / 2;
  oh = (h + 1) / 2;
  std::vector<uint16_t> tmp(static_cast<size_t>(h) * ow);
  for (int y = 0; y < h; ++y) {
    const uint8_t* r = in + static_cast<size_t>(y) * w;
    uint16_t* t = &tmp[static_cast<size_t>(y) * ow];
    for (int ox = 0; ox < ow; ++ox) {
      const int x = 2 * ox;
      t[ox] = static_cast<uint16_t>(r[clampIdx(x - 2, w)] + 4 * r[clampIdx(x - 1, w)] + 6 * r[x] +
                                    4 * r[clampIdx(x + 1, w)] + r[clampIdx(x + 2, w)]);
    }
  }
  out.resize(static_cast<size_t>(ow) * oh);
  for (int oy = 0; oy < oh; ++oy) {
    const int y = 2 * oy;
    const uint16_t* a = &tmp[static_cast<size_t>(clampIdx(y - 2, h)) * ow];
    const uint16_t* b = &tmp[static_cast<size_t>(clampIdx(y - 1, h)) * ow];
    const uint16_t* c = &tmp[static_cast<size_t>(y) * ow];
    const uint16_t* d = &tmp[static_cast<size_t>(clampIdx(y + 1, h)) * ow];
    const uint16_t* e = &tmp[static_cast<size_t>(clampIdx(y + 2, h)) * ow];
    uint8_t* o = &out[static_cast<size_t>(oy) * ow];
    for (int ox = 0; ox < ow; ++ox) {
      const uint32_t v = a[ox] + 4u * b[ox] + 6u * c[ox] + 4u * d[ox] + e[ox];
      o[ox] = static_cast<uint8_t>((v + 128) >> 8);
    }
  }
}

void pyrExpand(const int16_t* in, int w, int h, int ow, int oh, std::vector<int16_t>& out) {
  // Horizontal pass on the input rows, x8 scale.
  std::vector<int32_t> tmp(static_cast<size_t>(h) * ow);
  for (int y = 0; y < h; ++y) {
    const int16_t* r = in + static_cast<size_t>(y) * w;
    int32_t* t = &tmp[static_cast<size_t>(y) * ow];
    for (int x = 0; x < ow; ++x) {
      const int i = x >> 1;
      if ((x & 1) == 0) {
        t[x] = r[clampIdx(i - 1, w)] + 6 * r[clampIdx(i, w)] + r[clampIdx(i + 1, w)];
      } else {
        t[x] = 4 * (r[clampIdx(i, w)] + r[clampIdx(i + 1, w)]);
      }
    }
  }
  // Vertical pass, x8 scale; total x64.
  out.resize(static_cast<size_t>(ow) * oh);
  for (int y = 0; y < oh; ++y) {
    const int i = y >> 1;
    const int32_t* a = &tmp[static_cast<size_t>(clampIdx(i - 1, h)) * ow];
    const int32_t* b = &tmp[static_cast<size_t>(clampIdx(i, h)) * ow];
    const int32_t* c = &tmp[static_cast<size_t>(clampIdx(i + 1, h)) * ow];
    int16_t* o = &out[static_cast<size_t>(y) * ow];
    for (int x = 0; x < ow; ++x) {
      const int32_t v = (y & 1) == 0 ? a[x] + 6 * b[x] + c[x] : 4 * (b[x] + c[x]);
      o[x] = sat16((v + 32) >> 6);
    }
  }
}

int lowpassCoef(double fHz, double fs) {
  if (fs <= 0) return 1;
  const double r = 1.0 - std::exp(-2.0 * M_PI * fHz / fs);
  return std::max(1, std::min(65535, static_cast<int>(std::lround(r * 65536.0))));
}

void bandStep(const int16_t* lap, size_t n, int coefH, int coefL, BandState& st, std::vector<int16_t>& band) {
  if (st.lpH.size() != n) {
    st.lpH.assign(n, 0);
    st.lpL.assign(n, 0);
    st.amp.assign(n, 0);
  }
  band.resize(n);
  for (size_t i = 0; i < n; ++i) {
    const int32_t x = static_cast<int32_t>(lap[i]) * (1 << kStateFrac);
    int32_t hv = st.lpH[i], lv = st.lpL[i];
    hv += static_cast<int32_t>((static_cast<int64_t>(coefH) * (x - hv) + 32768) >> 16);
    lv += static_cast<int32_t>((static_cast<int64_t>(coefL) * (x - lv) + 32768) >> 16);
    st.lpH[i] = sat16(hv);
    st.lpL[i] = sat16(lv);
    const int16_t b = sat16(static_cast<int32_t>(st.lpH[i]) - st.lpL[i]);
    band[i] = b;
    const int32_t a = st.amp[i];
    const int32_t ab = b < 0 ? -static_cast<int32_t>(b) : b;
    st.amp[i] = static_cast<uint16_t>(a + ((ab - a) >> kEnergyShift));
  }
}

int workingLevels(int w, int levels) {
  if (levels > 0) return levels;
  int n = 0;
  while (w > 200) {
    w = (w + 1) / 2;
    ++n;
  }
  return n;
}

void core(const uint8_t* gray, int w, int h, int levels, int coefH, int coefL, int gain, BandState& st, CoreOut& out) {
  std::vector<uint8_t> g0(gray, gray + static_cast<size_t>(w) * h), tmp;
  int gw = w, gh = h;
  const int n = workingLevels(w, levels);
  for (int lv = 0; lv < n; ++lv) {
    int ow, oh;
    pyrReduce(g0.data(), gw, gh, tmp, ow, oh);
    g0.swap(tmp);
    gw = ow;
    gh = oh;
  }
  std::vector<uint8_t> g1, g2;
  int w1, h1, w2, h2;
  pyrReduce(g0.data(), gw, gh, g1, w1, h1);
  pyrReduce(g1.data(), w1, h1, g2, w2, h2);
  std::vector<int16_t> g2s(g2.begin(), g2.end()), e2;
  pyrExpand(g2s.data(), w2, h2, w1, h1, e2);
  out.lap.resize(g1.size());
  for (size_t i = 0; i < g1.size(); ++i) out.lap[i] = static_cast<int16_t>(g1[i] - e2[i]);
  if (st.lpH.size() != out.lap.size()) st = BandState();
  bandStep(out.lap.data(), out.lap.size(), coefH, coefL, st, out.band);
  std::vector<int16_t> delta(out.band.size()), up;
  for (size_t i = 0; i < out.band.size(); ++i) {
    const int32_t d = (gain * static_cast<int32_t>(out.band[i]) + 32) >> kStateFrac;
    delta[i] = static_cast<int16_t>(std::max(-kDeltaLimit, std::min(kDeltaLimit, d)));
  }
  pyrExpand(delta.data(), w1, h1, gw, gh, up);
  out.mag.resize(g0.size());
  for (size_t i = 0; i < g0.size(); ++i) out.mag[i] = static_cast<uint8_t>(std::max(0, std::min(255, g0[i] + up[i])));
  out.g0.swap(g0);
  out.amp = st.amp;
  out.w0 = gw;
  out.h0 = gh;
  out.w1 = w1;
  out.h1 = h1;
}

}  // namespace vision

using namespace vision;

VisionPipeline::~VisionPipeline() { stop(); }

void VisionPipeline::start() {
  if (running_) return;
  running_ = true;
  thread_ = std::thread(&VisionPipeline::run, this);
}

void VisionPipeline::stop() {
  if (!running_) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    running_ = false;
  }
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
}

void VisionPipeline::configure(const VisionConfig& cfg) {
  std::lock_guard<std::mutex> lock(mutex_);
  cfg_ = cfg;
}

void VisionPipeline::setPaused(bool paused) { paused_ = paused; }

bool VisionPipeline::wantsFrame() {
  std::lock_guard<std::mutex> lock(mutex_);
  return running_ && cfg_.enabled && !paused_ && !havePending_;
}

void VisionPipeline::offer(const uint8_t* gray, int w, int h, int64_t tNs) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!cfg_.enabled || paused_) return;
    pending_.assign(gray, gray + static_cast<size_t>(w) * h);  // the newest frame replaces an unprocessed one
    pw_ = w;
    ph_ = h;
    pendingNs_ = tNs;
    havePending_ = true;
  }
  cv_.notify_one();
}

VisionLive VisionPipeline::live() {
  std::lock_guard<std::mutex> lock(mutex_);
  VisionLive v = out_;
  if (!cfg_.enabled) v.w = 0;
  v.paused = paused_;
  return v;
}

void VisionPipeline::run() {
  setThreadNice(5);  // display processing yields to tracking
  std::vector<uint8_t> frame;
  while (true) {
    int w = 0, h = 0;
    int64_t t = 0;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return !running_ || havePending_; });
      if (!running_) return;
      frame.swap(pending_);
      w = pw_;
      h = ph_;
      t = pendingNs_;
      havePending_ = false;
    }
    process(frame.data(), w, h, t);
  }
}

void VisionPipeline::process(const uint8_t* gray, int w, int h, int64_t tNs) {
  const int64_t t0 = monoNs();
  VisionConfig cfg;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    cfg = cfg_;
  }
  // Band (L1) size for this input: n REDUCE steps to G0 plus one.
  const int n = workingLevels(w, cfg.levels);
  int w1 = w, h1 = h;
  for (int i = 0; i <= n; ++i) {
    w1 = (w1 + 1) / 2;
    h1 = (h1 + 1) / 2;
  }
  // Sampling rate from the processed frames (skipped frames lower it). After
  // a pause (output not shown, a capture) the bands start over.
  const bool resumed = lastNs_ > 0 && tNs - lastNs_ > 1000000000LL;
  if (resumed) {
    fps_ = 0;
  } else if (lastNs_ > 0 && tNs > lastNs_) {
    const double inst = 1e9 / static_cast<double>(tNs - lastNs_);
    fps_ = fps_ > 0 ? fps_ + 0.1 * (inst - fps_) : inst;
  }
  lastNs_ = tNs;
  const double fs = fps_ > 0 ? fps_ : 30.0;
  if (resumed || w1 != bw_ || h1 != bh_ || cfg.lowHz != lastLow_ || cfg.highHz != lastHigh_) {
    band_ = BandState();
    bw_ = w1;
    bh_ = h1;
    lastLow_ = cfg.lowHz;
    lastHigh_ = cfg.highHz;
    framesSinceReset_ = 0;
  }
  const int cH = lowpassCoef(std::min(cfg.highHz, fs * 0.45), fs);
  const int cL = lowpassCoef(std::min(cfg.lowHz, fs * 0.45), fs);
  CoreOut co;
  core(gray, w, h, cfg.levels, cH, cL, cfg.gain, band_, co);
  ++framesSinceReset_;

  // Band amplitude for display: scaled to the frame maximum, at least 2 gray levels full scale.
  uint16_t amax = 0;
  size_t imax = 0;
  for (size_t i = 0; i < band_.amp.size(); ++i) {
    if (band_.amp[i] > amax) {
      amax = band_.amp[i];
      imax = i;
    }
  }
  const uint32_t full = std::max<uint32_t>(amax, 2u << kStateFrac);
  std::vector<uint8_t> energy(band_.amp.size());
  for (size_t i = 0; i < energy.size(); ++i) energy[i] = static_cast<uint8_t>(std::min<uint32_t>(255, band_.amp[i] * 255u / full));

  const bool settled = framesSinceReset_ >= std::max(10, static_cast<int>(std::ceil(3.0 * 65536.0 / cL)));
  const double ms = (monoNs() - t0) * 1e-6;
  std::lock_guard<std::mutex> lock(mutex_);
  out_.w = co.w0;
  out_.h = co.h0;
  out_.source.swap(co.g0);
  out_.magnified.swap(co.mag);
  out_.ew = co.w1;
  out_.eh = co.h1;
  out_.energy.swap(energy);
  out_.backend = "PS";
  out_.procMs = out_.procMs > 0 ? out_.procMs + 0.1 * (ms - out_.procMs) : ms;
  out_.fps = fs;
  out_.settled = settled;
  out_.peakX = amax > (1u << kStateFrac) / 2 ? static_cast<int>(imax % w1) * 2 : -1;
  out_.peakY = amax > (1u << kStateFrac) / 2 ? static_cast<int>(imax / w1) * 2 : -1;
}

}  // namespace vm
