// Vision core: hand-checked pyramid values, filter behaviour and the
// threaded PS pipeline.
#include <cmath>
#include <cstdio>
#include <ctime>
#include <vector>

#include "measure/vision.h"
#include "test_util.h"

using namespace vm;
using namespace vm::vision;

int main() {
  // REDUCE of an impulse: centre weight 6*6/256, edge weight 1*6/256.
  {
    std::vector<uint8_t> img(8 * 8, 0), out;
    img[4 * 8 + 4] = 255;
    int ow, oh;
    pyrReduce(img.data(), 8, 8, out, ow, oh);
    CHECK(ow == 4 && oh == 4);
    CHECK(out[2 * 4 + 2] == 36);  // (255*36 + 128) >> 8
    CHECK(out[1 * 4 + 2] == 6);   // (255*6 + 128) >> 8
    CHECK(out[1 * 4 + 1] == 1);   // (255*1 + 128) >> 8
    CHECK(out[0] == 0);
  }
  // REDUCE and EXPAND keep a constant image; odd sizes round up.
  {
    std::vector<uint8_t> img(7 * 5, 100), out;
    int ow, oh;
    pyrReduce(img.data(), 7, 5, out, ow, oh);
    CHECK(ow == 4 && oh == 3);
    bool flat = true;
    for (uint8_t v : out) flat = flat && v == 100;
    CHECK(flat);
    std::vector<int16_t> s(4 * 3, 100), up;
    pyrExpand(s.data(), 4, 3, 7, 5, up);
    flat = up.size() == 35;
    for (int16_t v : up) flat = flat && v == 100;
    CHECK(flat);
    // Negative values round towards minus infinity: (-1*64 + 32) >> 6 = -1 for a -1 field.
    std::vector<int16_t> neg(4, -1), nup;
    pyrExpand(neg.data(), 2, 2, 4, 4, nup);
    CHECK(nup[0] == -1 && nup[15] == -1);
  }
  // Coefficients.
  CHECK(lowpassCoef(5, 60) == static_cast<int>(std::lround(65536 * (1 - std::exp(-2 * M_PI * 5 / 60)))));
  CHECK(lowpassCoef(1000, 1) == 65535);
  // Band-pass: a constant input settles to zero, a 5 Hz input passes, 0.2 Hz and 25 Hz are attenuated.
  auto bandAmp = [](double f, double fs, double lo, double hi) {
    BandState st;
    std::vector<int16_t> band, x(1);
    const int cH = lowpassCoef(hi, fs), cL = lowpassCoef(lo, fs);
    double peak = 0;
    for (int i = 0; i < 1200; ++i) {
      x[0] = static_cast<int16_t>(std::lround(40 * std::sin(2 * M_PI * f * i / fs)));
      bandStep(x.data(), 1, cH, cL, st, band);
      if (i > 900) peak = std::max(peak, std::fabs(band[0] / 64.0));
    }
    return peak / 40;
  };
  {
    BandState st;
    std::vector<int16_t> band, x(1, 77);
    for (int i = 0; i < 2000; ++i) bandStep(x.data(), 1, lowpassCoef(8, 60), lowpassCoef(2, 60), st, band);
    CHECK(std::abs(band[0]) <= 3);  // fixed-point dead band of the low-passes: a few Q6 LSB
    const double pass = bandAmp(5, 60, 2, 8), low = bandAmp(0.2, 60, 2, 8), high = bandAmp(25, 60, 2, 8);
    printf("  band gain: 0.2 Hz %.3f, 5 Hz %.3f, 25 Hz %.3f\n", low, pass, high);
    CHECK(pass > 0.4 && pass < 1.0);
    CHECK(low < pass * 0.25 && high < pass * 0.6);
  }
  // Core outputs: sizes and a magnified moving edge.
  {
    std::vector<uint8_t> f(640 * 480);
    BandState st;
    CoreOut co;
    double diff = 0;
    for (int i = 0; i < 120; ++i) {
      const double y0 = 240 + 1.5 * std::sin(2 * M_PI * 4 * i / 60.0);
      for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) f[y * 640 + x] = static_cast<uint8_t>(y < y0 ? 60 : 180);
      }
      core(f.data(), 640, 480, 0, lowpassCoef(8, 60), lowpassCoef(2, 60), 20, st, co);
      if (i > 80) {
        for (size_t k = 0; k < co.g0.size(); ++k) diff = std::max(diff, std::fabs(co.mag[k] - co.g0[k] * 1.0));
      }
    }
    CHECK(co.w0 == 160 && co.h0 == 120 && co.w1 == 80 && co.h1 == 60);
    CHECK(diff > 10);  // the moving edge is visibly magnified
    CHECK(workingLevels(1280, 0) == 3 && workingLevels(640, 0) == 2 && workingLevels(640, 1) == 1);
  }
  // Threaded pipeline: frames offered faster than real time are processed or skipped, never queued.
  {
    VisionPipeline vp;
    vp.start();
    std::vector<uint8_t> f(320 * 240, 128);
    for (int i = 0; i < 50; ++i) vp.offer(f.data(), 320, 240, static_cast<int64_t>(i) * 16666667);
    for (int k = 0; k < 100 && vp.live().w == 0; ++k) {
      struct timespec ts = {0, 10000000};
      nanosleep(&ts, nullptr);
    }
    const VisionLive v = vp.live();
    CHECK(v.w == 160 && v.h == 120 && v.backend == "PS");
    vp.setPaused(true);
    CHECK(vp.live().paused);
    vp.stop();
  }
  return testSummary("vision");
}
