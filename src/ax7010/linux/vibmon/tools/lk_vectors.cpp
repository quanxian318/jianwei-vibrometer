// lk_vectors - golden vectors for the PL lk_accum engine
// (ax7010/pl/displacement/sim/tb_lk_accum.v) from the bit-exact model lkAccumulate().
//
//   lk_vectors <out.txt> <cases> [frames.gray w h roi_x roi_y roi_size radius]
//
// Without a clip the frames come from the synthetic source. The template
// coefficients are those LkTracker::init() takes from the first frame; each
// case copies the search window of a later frame into a window buffer and
// picks an integer/fractional offset inside it.
// File (whitespace separated): <cases>, then per case
//   w h stride rows ox oy ax ay (decimal)
//   stride*rows window bytes (hex)
//   w*h coefficients t gx gy (hex, 16-bit two's complement)
//   s1 s2 sx sy st (hex, 48-bit two's complement)
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <vector>

#include "measure/frame_source.h"
#include "measure/lk_core.h"
#include "measure/lk_tracker.h"

using namespace vm;

int main(int argc, char** argv) {
  if (argc != 3 && argc != 10) {
    fprintf(stderr, "usage: %s <out.txt> <cases> [frames.gray w h roi_x roi_y roi_size radius]\n", argv[0]);
    return 2;
  }
  const int cases = std::atoi(argv[2]);
  FILE* clip = nullptr;
  std::unique_ptr<FrameSource> synth;
  GrayFrame f;
  Roi roi;
  int radius = 6;
  if (argc == 10) {
    clip = fopen(argv[3], "rb");
    if (!clip) return 1;
    f.width = std::atoi(argv[4]);
    f.height = std::atoi(argv[5]);
    f.pixels.resize(static_cast<size_t>(f.width) * f.height);
    roi = Roi{std::atoi(argv[6]), std::atoi(argv[7]), std::atoi(argv[8]), std::atoi(argv[8])};
    radius = std::atoi(argv[9]);
  } else {
    SyntheticConfig sc;
    sc.realtime = false;
    synth = makeSyntheticSource(sc);
    synth->start();
    roi = Roi{304, 224, 32, 32};
  }
  auto next = [&]() -> bool {
    if (clip) return fread(f.pixels.data(), 1, f.pixels.size(), clip) == f.pixels.size();
    return synth->next(f, 0);
  };
  if (!next()) return 1;
  LkTracker tr;
  if (!tr.init(f, roi, radius)) {
    fprintf(stderr, "template not trackable\n");
    return 1;
  }
  const Roi r = tr.roi();
  const LkCoeffs& c = tr.coeffs();
  const int stride = c.w + 2 * radius + 3, rows = c.h + 2 * radius + 3;
  const int wx = r.x - radius - 1, wy = r.y - radius - 1;
  FILE* out = fopen(argv[1], "w");
  if (!out) return 1;
  fprintf(out, "%d\n", cases);
  std::mt19937 rng(20261021);
  std::vector<uint8_t> win(static_cast<size_t>(stride) * rows);
  int mismatches = 0;
  for (int k = 0; k < cases; ++k) {
    for (int skip = 0; skip < 7; ++skip)
      if (!next()) return 1;
    for (int y = 0; y < rows; ++y)
      for (int x = 0; x < stride; ++x) win[static_cast<size_t>(y) * stride + x] = f.at(wx + x, wy + y);
    const int ox = static_cast<int>(rng() % (2 * radius + 3)), oy = static_cast<int>(rng() % (2 * radius + 3));
    int ax = static_cast<int>(rng() % 256), ay = static_cast<int>(rng() % 256);
    if (k % 4 == 0) ax = ay = 0;  // integer-search pass
    if (k % 4 == 1) ax = 255, ay = 255;  // largest weights on the far neighbours
    const LkAccum a = lkAccumulate(&win[static_cast<size_t>(oy) * stride + ox], stride, c, ax, ay);
    const LkAccum b = lkAccumulate(&f.pixels[static_cast<size_t>(wy + oy) * f.width + wx + ox], f.width, c, ax, ay);
    if (a.s1 != b.s1 || a.s2 != b.s2 || a.sx != b.sx || a.sy != b.sy || a.st != b.st) ++mismatches;
    fprintf(out, "%d %d %d %d %d %d %d %d\n", c.w, c.h, stride, rows, ox, oy, ax, ay);
    for (size_t i = 0; i < win.size(); ++i) fprintf(out, "%02x%c", win[i], (i % 32 == 31) ? '\n' : ' ');
    fprintf(out, "\n");
    for (size_t i = 0; i < c.t.size(); ++i)
      fprintf(out, "%04x %04x %04x\n", static_cast<uint16_t>(c.t[i]), static_cast<uint16_t>(c.gx[i]),
              static_cast<uint16_t>(c.gy[i]));
    const uint64_t m = (1ULL << 48) - 1;
    fprintf(out, "%012llx %012llx %012llx %012llx %012llx\n", static_cast<unsigned long long>(a.s1 & m),
            static_cast<unsigned long long>(a.s2 & m), static_cast<unsigned long long>(a.sx & m),
            static_cast<unsigned long long>(a.sy & m), static_cast<unsigned long long>(a.st & m));
  }
  fclose(out);
  if (clip) fclose(clip);
  fprintf(stderr, "%d cases, window %dx%d, template %dx%d, frame/window mismatches %d\n", cases, stride, rows, c.w, c.h,
          mismatches);
  return mismatches ? 1 : 0;
}
