// Compute-only A9 benchmark. Synthetic data, never a mechanical measurement.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "measure/multipoint.h"

int main(int argc, char** argv) {
  const int n = argc > 1 ? std::atoi(argv[1]) : 12;
  const int frames = argc > 2 ? std::atoi(argv[2]) : 160;
  const bool moving = argc > 3 && std::atoi(argv[3]) != 0;
  if (n < 1 || n > 60 || frames < 128 || frames > 1000) return 2;
  vm::MultiPointLayout layout;
  layout.width = 1280; layout.height = 720;
  for (int i = 0; i < n; ++i)
    layout.points.push_back({float(360 + (i % 10) * 56), float(160 + (i / 10) * 56)});
  layout.refs = {{{100, 560}}, {{132, 560}}, {{100, 592}}, {{132, 592}}};
  vm::GrayFrame f;
  f.width = layout.width; f.height = layout.height;
  f.pixels.resize(f.width * f.height);
  uint32_t state = 0x12345678;
  for (auto& p : f.pixels) { state = state * 1664525u + 1013904223u; p = uint8_t(state >> 24); }
  const auto base = f.pixels;
  f.timestampValid = true;
  vm::MultiPointTracker tracker;
  std::string err;
  if (!tracker.start(layout, true, "", err) || !tracker.prime(f)) return 3;
  std::vector<double> ms;
  int valid = 0;
  for (int k = 0; k < frames; ++k) {
    if (moving) {
      // Known synthetic camera translation: both points and references move.
      // Generate before timing; the benchmark measures tracking, not a renderer.
      const float dx = .3f * std::sin(6.28318530718f * 10.33f * k / 45);
      const float dy = .3f * std::cos(6.28318530718f * 10.33f * k / 45);
      for (int y = 1; y < f.height - 1; ++y) for (int x = 1; x < f.width - 1; ++x) {
        const float px = x - dx, py = y - dy;
        const int ix = int(px), iy = int(py);
        const float ax = px - ix, ay = py - iy;
        const size_t at = size_t(iy) * f.width + ix;
        const float v = (base[at]*(1-ax) + base[at+1]*ax)*(1-ay) +
                        (base[at+f.width]*(1-ax) + base[at+f.width+1]*ax)*ay;
        f.pixels[size_t(y)*f.width+x] = uint8_t(v + .5f);
      }
    }
    f.sequence = k + 1;
    f.timestampNs = 1000000000LL + int64_t(k) * 1000000000LL / 45;
    const auto begin = std::chrono::steady_clock::now();
    tracker.push(f);
    const auto end = std::chrono::steady_clock::now();
    ms.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
    valid = 0;
    for (int i = 0; i < n; ++i) valid += tracker.pointValid(i);
  }
  double total = 0; for (double x : ms) total += x;
  std::sort(ms.begin(), ms.end());
  std::printf("{\"synthetic_compute_only\":true,\"moving\":%s,\"points\":%d,\"references\":4,\"frames\":%d,"
              "\"mean_ms\":%.3f,\"p95_ms\":%.3f,\"max_ms\":%.3f,\"last_valid\":%d,\"baseline_windows\":%d}\n",
              moving ? "true" : "false", n, frames, total / frames, ms[size_t(frames * .95)], ms.back(), valid, tracker.baselineWindows());
  return valid == n ? 0 : 4;
}
