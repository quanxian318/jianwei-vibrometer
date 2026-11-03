#include "measure/field_spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "measure/spectrum.h"

namespace vm {

double aliasHz(double f, double fs) {
  if (fs <= 0) return f;
  double r = std::fmod(std::fabs(f), fs);
  return r > fs / 2 ? fs - r : r;
}

void FieldSpectrum::reset(int frameW, int frameH, int gridW) {
  fw_ = frameW;
  fh_ = frameH;
  block_ = std::max(1, frameW / std::max(1, gridW));
  gw_ = frameW / block_;
  gh_ = frameH / block_;
  frames_ = 0;
  data_.clear();
}

void FieldSpectrum::add(const uint8_t* pixels, int width, int height) {
  if (width != fw_ || height != fh_ || gw_ <= 0 || gh_ <= 0) return;
  const size_t cells = static_cast<size_t>(gw_) * gh_;
  const size_t base = data_.size();
  data_.resize(base + cells, 0.f);
  float* out = &data_[base];
  std::vector<uint32_t> acc(gw_);
  const float scale = 1.f / (block_ * block_);
  for (int gy = 0; gy < gh_; ++gy) {
    std::fill(acc.begin(), acc.end(), 0u);
    for (int y = gy * block_; y < (gy + 1) * block_; ++y) {
      const uint8_t* row = pixels + static_cast<size_t>(y) * width;
      for (int gx = 0; gx < gw_; ++gx) {
        uint32_t s = 0;
        for (int x = gx * block_; x < (gx + 1) * block_; ++x) s += row[x];
        acc[gx] += s;
      }
    }
    for (int gx = 0; gx < gw_; ++gx) out[static_cast<size_t>(gy) * gw_ + gx] = acc[gx] * scale;
  }
  ++frames_;
}

FieldSpectrumResult FieldSpectrum::analyze(double fs, double fMin, double fMax) const {
  FieldSpectrumResult r;
  r.gridW = gw_;
  r.gridH = gh_;
  r.frames = frames_;
  const int n = frames_;
  if (n < 64 || fs <= 0) {
    r.reason = "too_few_frames";
    return r;
  }
  size_t nfft = 1;
  while (nfft < static_cast<size_t>(n)) nfft <<= 1;
  const size_t half = nfft / 2;
  const double df = fs / nfft;
  r.resolutionHz = fs / n;
  const double fHi = fMax > 0 ? fMax : 0.8 * fs / 2;
  const size_t k0 = std::max<size_t>(1, static_cast<size_t>(std::ceil(fMin / df)));
  const size_t k1 = std::min(half - 1, static_cast<size_t>(std::floor(fHi / df)));
  if (k1 <= k0 + 4) {
    r.reason = "band_too_narrow";
    return r;
  }
  const size_t cells = static_cast<size_t>(gw_) * gh_;
  std::vector<double> win(n);
  for (int i = 0; i < n; ++i) win[i] = 0.5 - 0.5 * std::cos(2 * M_PI * i / (n - 1));
  std::vector<float> amp(cells * (half + 1));
  std::vector<double> activity(cells, 0), x(n);
  std::vector<std::complex<double>> buf(nfft);
  for (size_t c = 0; c < cells; ++c) {
    for (int i = 0; i < n; ++i) x[i] = data_[static_cast<size_t>(i) * cells + c];
    removeLinearTrend(x);
    std::fill(buf.begin(), buf.end(), 0.0);
    for (int i = 0; i < n; ++i) buf[i] = x[i] * win[i];
    fftInPlace(buf);
    float* a = &amp[c * (half + 1)];
    for (size_t k = 0; k <= half; ++k) a[k] = static_cast<float>(std::abs(buf[k]));
    for (size_t k = k0; k <= k1; ++k) activity[c] += a[k];
  }
  std::vector<double> sorted = activity;
  std::nth_element(sorted.begin(), sorted.begin() + cells / 2, sorted.end());
  const double actMedian = sorted[cells / 2];
  const int K = std::max(3, static_cast<int>(std::lround(1.0 / df)));  // +-1 Hz median window
  std::vector<double> frac(half + 1, 0.0);
  std::vector<float> w;
  size_t active = 0;
  for (size_t c = 0; c < cells; ++c) {
    if (activity[c] < actMedian || activity[c] <= 0) continue;
    ++active;
    const float* a = &amp[c * (half + 1)];
    for (size_t k = k0; k <= k1; ++k) {
      if (a[k] < a[k - 1] || a[k] < a[k + 1]) continue;  // local maxima only
      const size_t lo = k > static_cast<size_t>(K) ? k - K : 1, hi = std::min(half, k + K);
      w.assign(a + lo, a + hi + 1);
      std::nth_element(w.begin(), w.begin() + w.size() / 2, w.end());
      if (a[k] > 4 * w[w.size() / 2]) frac[k] += 1;
    }
  }
  if (active == 0) {
    r.reason = "no_activity";
    return r;
  }
  for (auto& v : frac) v /= active;
  // Lines: local maxima of the fraction curve, merged within the resolution.
  std::vector<size_t> peaks;
  for (size_t k = k0; k <= k1; ++k)
    if (frac[k] > 0 && frac[k] >= frac[k - 1] && frac[k] >= frac[k + 1]) peaks.push_back(k);
  std::sort(peaks.begin(), peaks.end(), [&](size_t a, size_t b) { return frac[a] > frac[b]; });
  const double sep = 1.5 * r.resolutionHz;
  for (size_t k : peaks) {
    // Fraction counts a line in one of a few neighbouring padded bins: sum them.
    double f = 0, wsum = 0;
    const size_t span = static_cast<size_t>(std::ceil(0.5 * r.resolutionHz / df));
    for (size_t j = (k > span ? k - span : 0); j <= std::min(half, k + span); ++j) {
      f += frac[j] * j * df;
      wsum += frac[j];
    }
    FieldLine line;
    line.hz = wsum > 0 ? f / wsum : k * df;
    line.fraction = wsum;
    bool near = false;
    for (const auto& l : r.lines) near = near || std::fabs(l.hz - line.hz) < sep;
    if (!near) r.lines.push_back(line);
    if (r.lines.size() == 5) break;
  }
  std::vector<double> band(frac.begin() + k0, frac.begin() + k1 + 1);
  std::nth_element(band.begin(), band.begin() + band.size() / 2, band.end());
  r.medianFraction = band[band.size() / 2];
  if (r.lines.empty()) {
    r.reason = "no_line";
    return r;
  }
  std::sort(r.lines.begin(), r.lines.end(), [](const FieldLine& a, const FieldLine& b) { return a.fraction > b.fraction; });
  r.peakHz = r.lines[0].hz;
  r.fraction = r.lines[0].fraction;
  for (size_t i = 1; i < r.lines.size(); ++i)
    for (int k = 2; k <= 5 && !r.lines[i].harmonicOf; ++k)
      if (std::fabs(aliasHz(k * r.peakHz, fs) - r.lines[i].hz) < sep) r.lines[i].harmonicOf = k;
  // A line must cover a clear share of the active field and dominate every
  // line that is not one of its (aliased) harmonics.
  double second = 0;
  for (size_t i = 1; i < r.lines.size(); ++i)
    if (!r.lines[i].harmonicOf) second = std::max(second, r.lines[i].fraction);
  r.valid = r.fraction >= 0.02 && r.fraction >= 1.5 * second;
  if (!r.valid) r.reason = r.fraction < 0.02 ? "no_clear_line" : "ambiguous_lines";
  return r;
}

}  // namespace vm
