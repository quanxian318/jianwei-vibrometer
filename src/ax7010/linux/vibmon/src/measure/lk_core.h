#pragma once

#include <cstdint>
#include <vector>

namespace vm {

// Bit-exact model of the PL displacement engine lk_accum
// (ax7010/pl/displacement/rtl/lk_accum.v). Everything the engine computes is
// integer; the PS turns the five sums into a ZNCC score or an LK update.
//
// Per template pixel i (row major, w x h):
//   I_w   = bilinear sample of the window at fractional offset (ax, ay) / 256,
//           ((256-ay)*((256-ax)*p00 + ax*p10) + ay*((256-ax)*p01 + ax*p11) + 128) >> 8,
//           unsigned Q8.8, 0..65280
//   s1 += I_w        s2 += I_w * I_w
//   sx += gx[i]*I_w  sy += gy[i]*I_w  st += t[i]*I_w
// gx, gy, t are int16 coefficients in Q.kLkCoefShift. With w*h <= 4096 every
// sum fits a signed 48-bit accumulator (DSP48E1 P register).
constexpr int kLkCoefShift = 6;
constexpr int kLkFracBits = 8;      // sub-pixel step 1/256 px
constexpr int kLkMaxPixels = 4096;  // 64 x 64

struct LkCoeffs {
  int w = 0, h = 0;
  std::vector<int16_t> t, gx, gy;  // template (zero-mean) and projected gradients
};

struct LkAccum {
  int64_t s1 = 0, s2 = 0, sx = 0, sy = 0, st = 0;
};

// win points at the window's top-left pixel; (w + 1) x (h + 1) pixels must be readable.
// ax, ay in 0..255.
LkAccum lkAccumulate(const uint8_t* win, int stride, const LkCoeffs& c, int ax, int ay);

inline int16_t lkQuantize(double v) {
  const double s = v * (1 << kLkCoefShift);
  const double r = s < 0 ? -static_cast<double>(static_cast<int64_t>(-s + 0.5)) : static_cast<double>(static_cast<int64_t>(s + 0.5));
  return static_cast<int16_t>(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
}

}  // namespace vm
