#include "measure/lk_core.h"

namespace vm {

LkAccum lkAccumulate(const uint8_t* win, int stride, const LkCoeffs& c, int ax, int ay) {
  LkAccum a;
  const int bx = 256 - ax, by = 256 - ay;
  for (int y = 0; y < c.h; ++y) {
    const uint8_t* r0 = win + static_cast<long>(y) * stride;
    const uint8_t* r1 = r0 + stride;
    const int base = y * c.w;
    for (int x = 0; x < c.w; ++x) {
      const uint32_t top = static_cast<uint32_t>(bx * r0[x] + ax * r0[x + 1]);
      const uint32_t bot = static_cast<uint32_t>(bx * r1[x] + ax * r1[x + 1]);
      const int64_t iw = static_cast<int64_t>((by * top + ay * bot + 128) >> 8);
      a.s1 += iw;
      a.s2 += iw * iw;
      a.sx += c.gx[base + x] * iw;
      a.sy += c.gy[base + x] * iw;
      a.st += c.t[base + x] * iw;
    }
  }
  return a;
}

}  // namespace vm
