// Spectrum maths and the tracker + analysis chain on synthetic camera frames.
#include <cmath>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "measure/session.h"
#include "measure/spectrum.h"
#include "measure/tracker.h"
#include "test_util.h"

using namespace vm;

namespace {

void testSpectrumSine() {
  const double fs = 100, f = 7.3, a = 1.7;
  std::vector<double> x;
  for (int i = 0; i < 1000; ++i) x.push_back(0.4 + 0.01 * i + a * std::sin(2 * M_PI * f * i / fs + 0.3));
  Spectrum s = amplitudeSpectrum(x, fs, 0.5, 0);
  CHECK(s.valid);
  CHECK_NEAR(s.peakHz, f, 0.02);
  CHECK_NEAR(s.peakAmp, a, 0.03);
  std::vector<double> d = x;
  removeLinearTrend(d);
  SeriesStats st = seriesStats(d);
  CHECK_NEAR(st.rms, a / std::sqrt(2.0), 0.02);
}

void testResample() {
  std::vector<double> t = {0, 0.1, 0.25, 0.3, 0.5};
  std::vector<double> x = {0, 1, 2.5, 3, 5};  // x = 10 t
  std::vector<double> u = resampleUniform(t, x, 10);
  CHECK(u.size() == 6);
  for (size_t i = 0; i < u.size(); ++i) CHECK_NEAR(u[i], static_cast<double>(i), 1e-9);
}

MeasureResult captureSynthetic(const SyntheticConfig& sc, double seconds, double cx, double cy, MeasureConfig mc) {
  auto src = makeSyntheticSource(sc);
  src->start();
  GrayFrame f;
  src->next(f, 0);
  Roi roi;
  roi.w = mc.roiW;
  roi.h = mc.roiH;
  roi.x = static_cast<int>(cx * f.width) - roi.w / 2;
  roi.y = static_cast<int>(cy * f.height) - roi.h / 2;
  NccTracker tr;
  CHECK(tr.init(f, roi, mc.searchRadius));
  std::vector<CaptureSample> samples;
  CaptureSample s0;
  s0.tNs = f.timestampNs;
  s0.seq = f.sequence;
  s0.ok = true;
  s0.score = 1;
  samples.push_back(s0);
  const int64_t t0 = f.timestampNs;
  while ((f.timestampNs - t0) * 1e-9 < seconds) {
    src->next(f, 0);
    NccTracker::Result r = tr.track(f);
    CaptureSample s;
    s.tNs = f.timestampNs;
    s.seq = f.sequence;
    s.ok = r.ok;
    s.dx = r.dx;
    s.dy = r.dy;
    s.score = r.score;
    s.edge = r.atEdge;
    samples.push_back(s);
  }
  return analyzeCapture(samples, mc, sc.fps);
}

void testTrackingChain() {
  SyntheticConfig sc;
  sc.realtime = false;
  sc.fps = 60;
  sc.freqHz = 5;
  sc.amplitudePx = 2;
  sc.axis = 'y';
  sc.noise = 2;
  MeasureConfig mc;
  mc.roiW = mc.roiH = 48;
  mc.searchRadius = 6;
  mc.axis = 'y';
  MeasureResult r = captureSynthetic(sc, 8, 0.5, 0.5, mc);
  CHECK(r.valid);
  CHECK_NEAR(r.fpsMeasured, 60, 0.01);
  CHECK_NEAR(r.dominantHz, 5, 0.03);
  CHECK_NEAR(r.peakAmpPx, 2, 0.12);
  CHECK_NEAR(r.ppPx, 4, 0.4);
  CHECK_NEAR(r.rmsPx, 2 / std::sqrt(2.0), 0.1);
  CHECK(r.badFrames == 0);
  std::printf("  moving ROI: %.3f Hz, peak %.3f px, p-p %.3f px, rms %.3f px, min NCC %.3f\n", r.dominantHz,
              r.peakAmpPx, r.ppPx, r.rmsPx, r.minScore);

  // Missing frames must invalidate the metrology result rather than be invented by interpolation.
  sc.dropEvery = 7;
  MeasureResult d = captureSynthetic(sc, 8, 0.5, 0.5, mc);
  CHECK(!d.valid);
  CHECK(d.reason == "dropped_frames");
  CHECK(d.dropped > 50);
  CHECK(!d.t.empty());  // raw accepted displacement remains available for diagnosis

  // A ROI on the static background must stay near zero.
  sc.dropEvery = 0;
  MeasureResult z = captureSynthetic(sc, 4, 0.15, 0.15, mc);
  CHECK(z.ppPx < 0.3);
}

void testQualityRejected() {
  std::vector<CaptureSample> good(64);
  for (size_t i = 0; i < good.size(); ++i) {
    good[i].tNs = 1000000000LL + i * 10000000LL;
    good[i].seq = static_cast<uint32_t>(i);
    good[i].ok = true;
    good[i].score = 1;
    good[i].dy = std::sin(2 * M_PI * 5 * i / 100.0);
  }
  MeasureConfig cfg;
  CHECK(analyzeCapture(good, cfg, 100).valid);
  auto checkReason = [&](std::vector<CaptureSample> v, const char* reason) {
    auto r = analyzeCapture(v, cfg, 100);
    CHECK(!r.valid);
    CHECK(r.reason == reason);
  };
  for (int missing : {1, 2, 3}) {
    auto g = good;
    g.erase(g.begin() + 30, g.begin() + 30 + missing);
    const auto r = analyzeCapture(g, cfg, 100);
    CHECK(r.valid == (missing <= 2));
    CHECK(r.dropped == static_cast<unsigned>(missing));
    CHECK_NEAR(r.fpsMeasured, 100, .001);
    if (r.valid) CHECK_NEAR(r.dominantHz, 5, .15);
  }
  auto excessive = good;
  for (int k : {50, 40, 30, 20}) excessive.erase(excessive.begin()+k);
  checkReason(excessive, "dropped_frames");
  auto v = good; v[30].tNs = v[29].tNs; checkReason(v, "bad_timestamps");
  v = good; v[30].seq = v[29].seq; checkReason(v, "bad_timestamps");
  v = good; v[30].timestampValid = false; checkReason(v, "bad_timestamps");
  v = good; v[30].score = 0.1; checkReason(v, "tracking_lost");
  v = good; v[30].edge = true; checkReason(v, "search_edge");
  v = good; v[30].saturated = true; checkReason(v, "image_saturated");
  v = good; v[30].tNs += 2000000; checkReason(v, "irregular_sampling");
  v = good; v[30].dy = NAN; checkReason(v, "tracking_lost");
}

class StallableSource : public FrameSource {
 public:
  std::atomic<bool> stall{false};
  StallableSource() { SyntheticConfig c; c.realtime = false; src = makeSyntheticSource(c); }
  bool start() override { return src->start(); }
  void stop() override { src->stop(); }
  bool next(GrayFrame& f, int) override {
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    return !stall && src->next(f, 0);
  }
  std::string describe() const override { return "stallable-test"; }
  double nominalFps() const override { return 60; }
 private:
  std::unique_ptr<FrameSource> src;
};

template <typename F> bool waitFor(F f) {
  for (int i = 0; i < 500; ++i) {
    if (f()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

void testCameraStallAndCancel() {
  auto* source = new StallableSource;
  MeasurementEngine engine{std::unique_ptr<FrameSource>(source)};
  CHECK(engine.startCamera());
  MeasureConfig cfg; cfg.durationS = 60;
  std::atomic<int> callbacks{0};
  MeasureResult result;
  auto done = [&](const MeasureResult& r) { result = r; ++callbacks; };
  CHECK(engine.beginCapture(cfg, .5, .5, done));
  CHECK(!engine.beginCapture(cfg, .5, .5, done));  // pending starts cannot overwrite each other
  CHECK(waitFor([&]() { return engine.state() == MeasurementEngine::State::kCapturing; }));
  source->stall = true;
  CHECK(waitFor([&]() { return callbacks.load() == 1; }));
  CHECK(!result.valid && result.reason == "camera_timeout");
  CHECK(engine.state() == MeasurementEngine::State::kPreview);
  CHECK(!engine.live().cameraOk);
  CHECK(!engine.beginCapture(cfg, .5, .5, done));
  source->stall = false;
  CHECK(waitFor([&]() { return engine.live().cameraOk; }));
  CHECK(engine.beginCapture(cfg, .5, .5, done));
  CHECK(waitFor([&]() { return engine.state() == MeasurementEngine::State::kCapturing; }));
  source->stall = true;
  engine.cancel();
  CHECK(waitFor([&]() { return engine.state() == MeasurementEngine::State::kPreview; }));
  engine.stopCamera();
  CHECK(callbacks.load() == 1);  // cancel has no completion callback
}

void testLowTextureRejected() {
  GrayFrame f;
  f.width = 200;
  f.height = 200;
  f.pixels.assign(200 * 200, 128);
  NccTracker tr;
  Roi roi;
  roi.x = 80;
  roi.y = 80;
  roi.w = roi.h = 40;
  CHECK(!tr.init(f, roi, 6));
}

std::vector<CaptureSample> sineSamples(double f, double amp, double common, int n = 300, double fs = 30) {
  std::vector<CaptureSample> v(n);
  for (int i = 0; i < n; ++i) {
    v[i].tNs = 1000000000LL + static_cast<int64_t>(i * 1e9 / fs);
    v[i].seq = static_cast<uint32_t>(i);
    v[i].ok = true;
    v[i].score = 1;
    // Slow "camera" drift shared with the reference plus the target's own vibration.
    const double cam = common * std::sin(2 * M_PI * 0.7 * i / fs) + 0.02 * i;
    v[i].dy = cam + amp * std::sin(2 * M_PI * f * i / fs);
    v[i].dx = cam;
  }
  return v;
}

void testReferenceCompensation() {
  MeasureConfig cfg;
  auto target = sineSamples(11, 0.3, 5);
  auto ref = sineSamples(11, 0, 5);
  MeasureResult raw = analyzeWithReference(target, nullptr, cfg, 30);
  CHECK(raw.valid);
  CHECK_NEAR(raw.dominantHz, 0.7, 0.1);  // camera motion dominates the raw target
  MeasureResult rel = analyzeWithReference(target, &ref, cfg, 30);
  CHECK(rel.valid && rel.reference.used && rel.reference.valid);
  CHECK_NEAR(rel.dominantHz, 11, 0.05);
  CHECK_NEAR(rel.peakAmpPx, 0.3, 0.02);
  CHECK(rel.peakClear && rel.peakSnr > 20);
  CHECK_NEAR(rel.rawDominantHz, 0.7, 0.1);
  // A lost reference invalidates the compensated result but names the cause.
  ref[100].score = 0.1;
  MeasureResult bad = analyzeWithReference(target, &ref, cfg, 30);
  CHECK(!bad.valid);
  CHECK(bad.reason == "reference_tracking_lost");
  // Mismatched frames between target and reference are unusable.
  ref = sineSamples(11, 0, 5);
  ref[50].seq += 1000;
  CHECK(!analyzeWithReference(target, &ref, cfg, 30).valid);
}

void testPeakConfidence() {
  MeasureConfig cfg;
  std::vector<CaptureSample> v = sineSamples(7, 0, 0);
  unsigned seed = 1;
  for (auto& s : v) {
    seed = seed * 1103515245u + 12345u;
    s.dy = ((seed >> 8) % 1000) / 1000.0 - 0.5;  // white noise, no line
  }
  MeasureResult r = analyzeCapture(v, cfg, 30);
  CHECK(r.valid);
  CHECK(!r.peakClear);
  CHECK(r.peakSnr < cfg.minPeakSnr);
  std::vector<CaptureSample> u = sineSamples(7, 0.5, 0);
  CHECK(analyzeCapture(u, cfg, 30).peakClear);
  u[40].converged = false;
  MeasureResult nc = analyzeCapture(u, cfg, 30);
  CHECK(!nc.valid && nc.reason == "not_converged" && nc.unconvergedFrames == 1);
}

void testFieldSpectrum() {
  CHECK_NEAR(aliasHz(22.3, 30), 7.7, 1e-9);
  CHECK_NEAR(aliasHz(33.4, 30), 3.4, 1e-9);
  CHECK_NEAR(aliasHz(11.1, 30), 11.1, 1e-9);
  SyntheticConfig sc;
  sc.realtime = false;
  sc.fps = 30;
  sc.freqHz = 4.2;
  sc.amplitudePx = 1;
  auto src = makeSyntheticSource(sc);
  src->start();
  GrayFrame f;
  FieldSpectrum fs;
  for (int i = 0; i < 300; ++i) {
    CHECK(src->next(f, 0));
    if (i == 0) fs.reset(f.width, f.height);
    fs.add(f.pixels.data(), f.width, f.height);
  }
  FieldSpectrumResult r = fs.analyze(30, 0.5, 0);
  CHECK(r.valid);
  CHECK_NEAR(r.peakHz, 4.2, 0.1);
  std::printf("  field: %.3f Hz, fraction %.3f, grid %dx%d\n", r.peakHz, r.fraction, r.gridW, r.gridH);
  // A static scene has no line.
  sc.amplitudePx = 0;
  src = makeSyntheticSource(sc);
  src->start();
  for (int i = 0; i < 300; ++i) {
    src->next(f, 0);
    if (i == 0) fs.reset(f.width, f.height);
    fs.add(f.pixels.data(), f.width, f.height);
  }
  CHECK(!fs.analyze(30, 0.5, 0).valid);
}

class CaptureReadSource : public FrameSource {
 public:
  explicit CaptureReadSource(const SyntheticConfig& c) : src(makeSyntheticSource(c)) {}
  bool start() override { return src->start(); }
  void stop() override { src->stop(); }
  bool next(GrayFrame& f, int timeout) override {
    if (engine && engine->state() == MeasurementEngine::State::kCapturing && f.wantColor) ++captureColourReads;
    return src->next(f, timeout);
  }
  std::string describe() const override { return src->describe(); }
  double nominalFps() const override { return src->nominalFps(); }
  std::unique_ptr<FrameSource> src;
  MeasurementEngine* engine = nullptr;
  std::atomic<int> captureColourReads{0};
};

void testEngineLkWithReference() {
  SyntheticConfig sc;
  sc.realtime = false;
  sc.fps = 60;
  sc.freqHz = 5;
  sc.amplitudePx = 1.5;
  auto source = new CaptureReadSource(sc);
  MeasurementEngine engine{std::unique_ptr<FrameSource>(source)};
  source->engine = &engine;
  CHECK(engine.startCamera());
  MeasureConfig cfg;
  cfg.tracker = "lk";
  cfg.roiW = cfg.roiH = 32;
  cfg.durationS = 4;
  cfg.referenceEnabled = true;
  cfg.refCx = 0.12;
  cfg.refCy = 0.12;  // static background of the synthetic scene
  std::atomic<int> done{0};
  MeasureResult result;
  CHECK(engine.beginCapture(cfg, 0.5, 0.5, [&](const MeasureResult& r) { result = r; ++done; }));
  // Up to 3 min: on the Cortex-A9 the synthetic source alone needs ~0.1 s per 640x480 frame.
  for (int i = 0; i < 18000 && done.load() == 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
  CHECK(done.load() == 1);
  std::printf("  engine result: %s\n", result.valid ? "valid" : result.reason.c_str());
  engine.stopCamera();
  CHECK(source->captureColourReads == 0);  // full colour reads cannot interrupt metrology
  CHECK(result.valid);
  CHECK(result.tracker == "lk");
  CHECK(result.reference.used && result.reference.valid);
  CHECK(result.reference.ppPx < 0.05);
  CHECK_NEAR(result.dominantHz, 5, 0.05);
  CHECK_NEAR(result.peakAmpPx, 1.5, 0.08);
  CHECK(result.peakClear);
  CHECK(result.field.valid);
  CHECK_NEAR(result.field.peakHz, 5, 0.3);
  std::printf("  engine lk+ref: %.3f Hz, peak %.3f px, ref p-p %.4f px, snr %.0f, field %.2f Hz\n", result.dominantHz,
              result.peakAmpPx, result.reference.ppPx, result.peakSnr, result.field.peakHz);
}

void testStartDuringColorOnlyRead() {
  struct Source : FrameSource {
    std::unique_ptr<FrameSource> inner;
    std::function<void()> startDuringRead;
    bool triggered = false;
    Source() {
      SyntheticConfig c;
      c.width = 160; c.height = 120; c.realtime = false;
      c.fps = 60; c.freqHz = 5; c.amplitudePx = 1; c.noise = 0;
      inner = makeSyntheticSource(c);
    }
    bool start() override { return inner->start(); }
    void stop() override { inner->stop(); }
    std::string describe() const override { return "colour-only-start-race"; }
    double nominalFps() const override { return 60; }
    bool next(GrayFrame& f, int) override {
      if (!inner->next(f, 0)) return false;
      if (f.colorOnly) {
        f.bgr.resize(f.pixels.size() * 3);
        for (size_t i = 0; i < f.pixels.size(); ++i)
          f.bgr[3*i] = f.bgr[3*i+1] = f.bgr[3*i+2] = f.pixels[i];
        f.pixels.clear();
        if (!triggered) { triggered = true; startDuringRead(); }
      }
      return true;
    }
  };
  auto* src = new Source;
  MeasurementEngine engine{std::unique_ptr<FrameSource>(src)};
  engine.setDisplayNeeds(true, false);
  MeasureConfig cfg;
  cfg.tracker = "lk"; cfg.roiW = cfg.roiH = 24;
  cfg.durationS = 4; cfg.fieldSpectrum = false;
  std::atomic<int> done{0};
  MeasureResult result;
  src->startDuringRead = [&] {
    CHECK(engine.beginCapture(cfg, 0.5, 0.5, [&](const MeasureResult& r) { result = r; ++done; }));
  };
  CHECK(engine.startCamera());
  for (int i = 0; i < 3000 && !done.load(); ++i)
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  engine.stopCamera();
  CHECK(src->triggered && done.load() == 1);
  CHECK(result.valid);
  CHECK_NEAR(result.dominantHz, 5, 0.1);
}

}  // namespace

int main() {
  testReferenceCompensation();
  testPeakConfidence();
  testFieldSpectrum();
  testEngineLkWithReference();
  testStartDuringColorOnlyRead();
  testSpectrumSine();
  testResample();
  testTrackingChain();
  testLowTextureRejected();
  testQualityRejected();
  testCameraStallAndCancel();
  return testSummary("measure");
}
