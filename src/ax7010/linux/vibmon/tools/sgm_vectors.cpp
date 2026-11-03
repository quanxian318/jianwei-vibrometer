// sgm_vectors - golden vectors for the PL stereo core
// (ax7010/pl/stereo/sim/tb_sgm_core.v) from sgmRaw(): center-symmetric 9x7
// census, 2 paths, uniqueness, equiangular sub-pixel, left-right check.
//
//   sgm_vectors <out.txt> synthetic <w> <h>
//   sgm_vectors <out.txt> <left.pgm> <right.pgm> <x0> <y0> <w> <h>   (crop of a rectified pair)
// File: "w h" then w*h lines "cl cr gray_l gray_r" (hex), then w*h expected q4 (hex, 16-bit two's complement).
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#include "stereo/stereo_match.h"

using namespace vm;

static bool readPgm(const char* path, StereoImage& im) {
  FILE* f = fopen(path, "rb");
  if (!f) return false;
  int maxv = 0;
  char magic[3] = {0};
  bool ok = fscanf(f, "%2s %d %d %d", magic, &im.w, &im.h, &maxv) == 4 && std::strcmp(magic, "P5") == 0 &&
            maxv == 255 && fgetc(f) != EOF;
  if (ok) {
    im.px.resize(static_cast<size_t>(im.w) * im.h);
    ok = fread(im.px.data(), 1, im.px.size(), f) == im.px.size();
  }
  fclose(f);
  return ok;
}

static StereoImage crop(const StereoImage& s, int x0, int y0, int w, int h) {
  StereoImage o;
  o.w = w;
  o.h = h;
  o.px.resize(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) o.px[static_cast<size_t>(y) * w + x] = s.at(x0 + x, y0 + y);
  return o;
}

int main(int argc, char** argv) {
  StereoImage l, r;
  if (argc == 5 && std::string(argv[2]) == "synthetic") {
    const int w = std::atoi(argv[3]), h = std::atoi(argv[4]);
    l.w = r.w = w;
    l.h = r.h = h;
    l.px.resize(static_cast<size_t>(w) * h);
    r.px.resize(l.px.size());
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> u(0, 1);
    double kx[24], ky[24], ph[24];
    for (int i = 0; i < 24; ++i) {
      const double k = 0.15 + 0.8 * u(rng), th = 2 * M_PI * u(rng);
      kx[i] = k * std::cos(th);
      ky[i] = k * std::sin(th);
      ph[i] = 2 * M_PI * u(rng);
    }
    auto tex = [&](double x, double y) {
      double s = 0;
      for (int i = 0; i < 24; ++i) s += std::sin(kx[i] * x + ky[i] * y + ph[i]) / (1 + 3 * std::hypot(kx[i], ky[i]));
      return 128 + 35 * s;
    };
    for (int y = 0; y < h; ++y)
      for (int x = 0; x < w; ++x) {
        const double d = x < w / 2 ? 9.5 : 21.25;  // two planes, a depth step and occlusion
        l.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::lround(tex(x, y)));
        r.px[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::lround(0.85 * tex(x + d, y) + 10));
      }
  } else if (argc == 8) {
    StereoImage fl, fr;
    if (!readPgm(argv[2], fl) || !readPgm(argv[3], fr)) return 1;
    const int x0 = std::atoi(argv[4]), y0 = std::atoi(argv[5]), w = std::atoi(argv[6]), h = std::atoi(argv[7]);
    l = crop(fl, x0, y0, w, h);
    r = crop(fr, x0, y0, w, h);
  } else {
    fprintf(stderr, "usage: %s <out.txt> synthetic <w> <h> | <out.txt> <left.pgm> <right.pgm> <x0> <y0> <w> <h>\n", argv[0]);
    return 2;
  }
  SgmParams p;
  p.census = 9;
  p.paths = 2;
  p.lrCheck = true;
  int bits = 0, border = 0;
  const auto cl = censusTransform(l, p.census, bits, border);
  const auto cr = censusTransform(r, p.census, bits, border);
  const DisparityMap d = sgmRaw(cl, cr, l, bits, border, p);
  FILE* o = fopen(argv[1], "w");
  if (!o) return 1;
  fprintf(o, "%d %d\n", l.w, l.h);
  for (size_t i = 0; i < cl.size(); ++i)
    fprintf(o, "%08x %08x %02x %02x\n", static_cast<unsigned>(cl[i]), static_cast<unsigned>(cr[i]), l.px[i], r.px[i]);
  for (size_t i = 0; i < d.q4.size(); ++i) fprintf(o, "%04x\n", static_cast<uint16_t>(d.q4[i]));
  fclose(o);
  fprintf(stderr, "%dx%d, %d valid of %zu\n", l.w, l.h, d.valid(), d.q4.size());
  return 0;
}
