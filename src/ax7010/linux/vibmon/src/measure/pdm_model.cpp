#include "measure/pdm_model.h"

#include <algorithm>
#include <vector>

namespace vm {

namespace {

constexpr uint32_t kMask26 = (1u << 26) - 1;
const int32_t kFir[63] = {
#include "measure/pdm_fir_coef.inc"
};

int32_t signed26(uint32_t v) { return static_cast<int32_t>(v << 6) >> 6; }

// CIC output for word m: sum over k of c[k] x[32 m + 27 - k], c = taps of
// (1 + z^-1 + ... + z^-31)^5 (156 taps; the 27 is the integrator pipeline).
// Byte j of word m - d contributes table[d][j][byte].
struct CicTables {
  int32_t t[5][4][256];
  CicTables() {
    std::vector<int64_t> c(1, 1);
    for (int stage = 0; stage < 5; ++stage) {
      std::vector<int64_t> n(c.size() + 31, 0);
      for (size_t i = 0; i < c.size(); ++i)
        for (int j = 0; j < 32; ++j) n[i + j] += c[i];
      c.swap(n);
    }
    for (int d = 0; d < 5; ++d) {
      for (int j = 0; j < 4; ++j) {
        for (int v = 0; v < 256; ++v) {
          int64_t s = 0;
          for (int b = 0; b < 8; ++b) {
            const int k = 32 * d + 27 - (8 * j + b);
            if ((v >> b) & 1 && k >= 0 && k < static_cast<int>(c.size())) s += c[static_cast<size_t>(k)];
          }
          t[d][j][v] = static_cast<int32_t>(s);
        }
      }
    }
  }
};

const CicTables& cicTables() {
  static const CicTables tables;
  return tables;
}

// FIR /2 shared by both decimators: true when this CIC sample starts an output.
bool firStep(int32_t s24, int32_t* line, int& wp, bool& half, int16_t& pcm, bool& saturated) {
  const int newest = wp;
  line[wp] = s24;
  wp = (wp + 1) & 63;
  const bool go = half;
  half = !half;
  if (!go) return false;
  int64_t acc = 0;
  for (int k = 0; k < 63; ++k) acc += static_cast<int64_t>(kFir[k]) * line[(newest - k) & 63];
  const int64_t wide = (acc + (int64_t(1) << 23)) >> 24;
  saturated = wide > 32767 || wide < -32768;
  pcm = static_cast<int16_t>(std::max<int64_t>(-32768, std::min<int64_t>(32767, wide)));
  return true;
}

}  // namespace

void PdmDecimator::reset() {
  std::fill(integ_, integ_ + 5, 0u);
  std::fill(comb_, comb_ + 5, 0u);
  std::fill(line_, line_ + 64, 0);
  bits_ = 0;
  wp_ = 0;
  half_ = false;
}

bool PdmDecimator::push(int bit, int16_t& pcm, bool& saturated) {
  uint32_t next[5];
  next[0] = (integ_[0] + static_cast<uint32_t>(bit & 1)) & kMask26;
  for (int s = 1; s < 5; ++s) next[s] = (integ_[s] + integ_[s - 1]) & kMask26;
  std::copy(next, next + 5, integ_);
  if (++bits_ < 32) return false;
  bits_ = 0;

  uint32_t v = integ_[4];
  for (int s = 0; s < 5; ++s) {
    const uint32_t d = (v - comb_[s]) & kMask26;
    comb_[s] = v;
    v = d;
  }
  // 0..2^25 ones-count scale -> signed around mid-scale -> 24 bits (floor).
  const int32_t s24 = signed26((v - (1u << 24)) & kMask26) >> 2;
  return firStep(s24, line_, wp_, half_, pcm, saturated);
}

void PdmWordDecimator::reset() {
  std::fill(hist_, hist_ + 4, 0u);
  std::fill(line_, line_ + 64, 0);
  wp_ = 0;
  half_ = false;
}

bool PdmWordDecimator::push(uint32_t word, int16_t& pcm, bool& saturated) {
  const CicTables& ct = cicTables();
  int32_t y = 0;
  const uint32_t w[5] = {word, hist_[0], hist_[1], hist_[2], hist_[3]};
  for (int d = 0; d < 5; ++d) {
    const uint32_t x = w[d];
    y += ct.t[d][0][x & 0xFF] + ct.t[d][1][(x >> 8) & 0xFF] + ct.t[d][2][(x >> 16) & 0xFF] + ct.t[d][3][x >> 24];
  }
  hist_[3] = hist_[2];
  hist_[2] = hist_[1];
  hist_[1] = hist_[0];
  hist_[0] = word;
  return firStep((y - (1 << 24)) >> 2, line_, wp_, half_, pcm, saturated);
}

int SigmaDelta2::step(double x) {
  const double fb = last_ ? 1.0 : -1.0;
  s1_ += x - fb;
  s2_ += s1_ - fb;
  last_ = s2_ >= 0 ? 1 : 0;
  return last_;
}

}  // namespace vm
