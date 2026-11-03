// vision_ref - runs the reference vision core (src/measure/vision.cpp) on a
// recorded frame sequence, for bit-exact comparison with the PC model
// (pc_sim/vision_model.py) and the PL implementation (ax7010/pl/vision).
//
//   vision_ref <in.vis> <out.vout>
//
// in.vis  (little endian): "VIS1", u16 w, h, frames, u8 levels, u8 0,
//         u16 coefH, coefL, gain, then frames x (w*h) gray bytes.
// out.vout: "VOUT", u16 w0, h0, w1, h1, frames, then per frame:
//         g0 (w0*h0 u8), magnified (w0*h0 u8), band (w1*h1 i16), amp (w1*h1 u16).
#include <cstdio>
#include <cstring>
#include <vector>

#include "measure/vision.h"

namespace {
bool readU16(FILE* f, uint16_t& v) {
  uint8_t b[2];
  if (fread(b, 1, 2, f) != 2) return false;
  v = static_cast<uint16_t>(b[0] | (b[1] << 8));
  return true;
}
void writeU16(FILE* f, uint16_t v) {
  const uint8_t b[2] = {static_cast<uint8_t>(v), static_cast<uint8_t>(v >> 8)};
  fwrite(b, 1, 2, f);
}
}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s <in.vis> <out.vout>\n", argv[0]);
    return 2;
  }
  FILE* in = fopen(argv[1], "rb");
  if (!in) {
    perror(argv[1]);
    return 1;
  }
  char magic[4];
  uint16_t w, h, n, coefH, coefL, gain;
  uint8_t lv[2];
  if (fread(magic, 1, 4, in) != 4 || memcmp(magic, "VIS1", 4) != 0 || !readU16(in, w) || !readU16(in, h) ||
      !readU16(in, n) || fread(lv, 1, 2, in) != 2 || !readU16(in, coefH) || !readU16(in, coefL) || !readU16(in, gain)) {
    fprintf(stderr, "%s: bad header\n", argv[1]);
    return 1;
  }
  FILE* out = fopen(argv[2], "wb");
  if (!out) {
    perror(argv[2]);
    return 1;
  }
  std::vector<uint8_t> frame(static_cast<size_t>(w) * h);
  vm::vision::BandState st;
  vm::vision::CoreOut co;
  for (uint16_t i = 0; i < n; ++i) {
    if (fread(frame.data(), 1, frame.size(), in) != frame.size()) {
      fprintf(stderr, "%s: truncated at frame %u\n", argv[1], i);
      return 1;
    }
    vm::vision::core(frame.data(), w, h, lv[0], coefH, coefL, gain, st, co);
    if (i == 0) {
      fwrite("VOUT", 1, 4, out);
      writeU16(out, static_cast<uint16_t>(co.w0));
      writeU16(out, static_cast<uint16_t>(co.h0));
      writeU16(out, static_cast<uint16_t>(co.w1));
      writeU16(out, static_cast<uint16_t>(co.h1));
      writeU16(out, n);
    }
    fwrite(co.g0.data(), 1, co.g0.size(), out);
    fwrite(co.mag.data(), 1, co.mag.size(), out);
    for (int16_t v : co.band) writeU16(out, static_cast<uint16_t>(v));
    for (uint16_t v : co.amp) writeU16(out, v);
  }
  fclose(in);
  fclose(out);
  return 0;
}
