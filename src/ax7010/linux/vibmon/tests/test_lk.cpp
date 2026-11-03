// Inverse-compositional LK tracker against the NCC baseline on exactly shifted synthetic frames.
#include <cmath>
#include <random>
#include <vector>

#include "measure/lk_tracker.h"
#include "measure/tracker.h"
#include "test_util.h"

using namespace vm;

namespace {

// Band-limited texture: sum of random 2D sinusoids, so any sub-pixel shift is exact.
struct Texture {
  struct Wave { double kx, ky, ph, a; };
  std::vector<Wave> waves;
  explicit Texture(unsigned seed, bool stripes = false) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> u(0, 1);
    for (int i = 0; i < 40; ++i) {
      const double k = 0.08 + 0.5 * u(rng), th = stripes ? 0.0 : 2 * M_PI * u(rng);
      waves.push_back({k * std::cos(th), k * std::sin(th), 2 * M_PI * u(rng), 1.0 / (1 + 6 * k)});
    }
  }
  double at(double x, double y) const {
    double s = 0;
    for (const auto& w : waves) s += w.a * std::sin(w.kx * x + w.ky * y + w.ph);
    return s;
  }
};

GrayFrame render(const Texture& t, double dx, double dy, double gain = 1, double offset = 0, double noise = 0,
                 unsigned seed = 1) {
  GrayFrame f;
  f.width = 160;
  f.height = 120;
  f.pixels.resize(static_cast<size_t>(f.width) * f.height);
  std::mt19937 rng(seed);
  std::normal_distribution<double> n(0, noise > 0 ? noise : 1);
  for (int y = 0; y < f.height; ++y)
    for (int x = 0; x < f.width; ++x) {
      double v = 128 + offset + gain * 22 * t.at(x - dx, y - dy) + (noise > 0 ? n(rng) : 0);
      f.pixels[static_cast<size_t>(y) * f.width + x] = static_cast<uint8_t>(std::lround(std::max(0.0, std::min(255.0, v))));
    }
  return f;
}

template <typename T> void sweep(const char* name, int roiSize, double noise, double& rms, double& maxErr) {
  Texture tex(7);
  Roi roi{64, 44, roiSize, roiSize};
  double se = 0;
  int n = 0;
  maxErr = 0;
  T tr;
  CHECK(tr.init(render(tex, 0, 0), roi, 6));
  for (int k = 0; k <= 40; ++k) {  // slow ramp so frame-to-frame search is exercised
    const double dx = 0.137 * k - 1.1, dy = 2.0 * std::sin(0.31 * k);
    auto r = tr.track(render(tex, dx, dy, 1, 0, noise, 100 + k));
    CHECK(r.ok);
    const double e = std::hypot(r.dx - dx, r.dy - dy);
    se += e * e;
    maxErr = std::max(maxErr, e);
    ++n;
  }
  rms = std::sqrt(se / n);
  std::printf("  %-4s roi %d noise %.1f: rms %.4f px, max %.4f px\n", name, roiSize, noise, rms, maxErr);
}

void testAccuracy() {
  double rmsN, maxN, rmsL, maxL;
  sweep<NccTracker>("ncc", 32, 0, rmsN, maxN);
  sweep<LkTracker>("lk", 32, 0, rmsL, maxL);
  CHECK(rmsL < 0.02);
  CHECK(rmsL < 0.5 * rmsN);  // the parabolic NCC peak has a systematic sub-pixel bias
  sweep<NccTracker>("ncc", 32, 2, rmsN, maxN);
  sweep<LkTracker>("lk", 32, 2, rmsL, maxL);
  CHECK(rmsL < 0.05);
}

void testBrightnessChange() {
  Texture tex(11);
  LkTracker tr;
  CHECK(tr.init(render(tex, 0, 0), Roi{64, 44, 32, 32}, 6));
  auto r = tr.track(render(tex, 0.4, -0.7, 1.25, -15));  // exposure/gain step plus offset
  CHECK(r.ok && r.converged);
  CHECK_NEAR(r.dx, 0.4, 0.02);
  CHECK_NEAR(r.dy, -0.7, 0.02);
  CHECK(r.score > 0.99);
}

void testDegenerateRejected() {
  Texture stripes(5, true);  // all waves along x: a set of vertical edges
  LkTracker tr;
  CHECK(!tr.init(render(stripes, 0, 0), Roi{64, 44, 32, 32}, 6));
  CHECK(tr.initStatus() == LkTracker::InitStatus::kAperture || tr.initStatus() == LkTracker::InitStatus::kWeakGradient);
  GrayFrame flat = render(stripes, 0, 0, 0);
  CHECK(!tr.init(flat, Roi{64, 44, 32, 32}, 6));
  CHECK(tr.initStatus() == LkTracker::InitStatus::kFlat);
  Texture tex(7);
  CHECK(!tr.init(render(tex, 0, 0), Roi{40, 20, 80, 64}, 6));  // more pixels than the engine holds
  CHECK(tr.initStatus() == LkTracker::InitStatus::kTooLarge);
}

void testEdgeAndLoss() {
  Texture tex(3);
  LkTracker tr;
  CHECK(tr.init(render(tex, 0, 0), Roi{64, 44, 32, 32}, 4));
  auto r = tr.track(render(tex, 7, 0));  // jump larger than the search radius
  CHECK(!r.ok || r.atEdge || r.score < 0.6);
  Texture other(99);
  LkTracker t2;
  CHECK(t2.init(render(tex, 0, 0), Roi{64, 44, 32, 32}, 4));
  auto q = t2.track(render(other, 0, 0));  // scene replaced: low score
  CHECK(q.score < 0.6);
}

}  // namespace

int main() {
  testAccuracy();
  testBrightnessChange();
  testDegenerateRejected();
  testEdgeAndLoss();
  return testSummary("lk");
}
