#include "measure/spectrum.h"

#include <algorithm>
#include <cmath>

namespace vm {

void fftInPlace(std::vector<std::complex<double>>& a) {
  const size_t n = a.size();
  for (size_t i = 1, j = 0; i < n; ++i) {
    size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (size_t len = 2; len <= n; len <<= 1) {
    const double ang = -2 * M_PI / static_cast<double>(len);
    const std::complex<double> wl(std::cos(ang), std::sin(ang));
    for (size_t i = 0; i < n; i += len) {
      std::complex<double> w(1, 0);
      for (size_t k = 0; k < len / 2; ++k) {
        const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}

SeriesStats seriesStats(const std::vector<double>& x) {
  SeriesStats s;
  if (x.empty()) return s;
  double sum = 0, sq = 0;
  s.min = s.max = x[0];
  for (double v : x) {
    sum += v;
    sq += v * v;
    s.min = std::min(s.min, v);
    s.max = std::max(s.max, v);
  }
  s.mean = sum / x.size();
  double var = sq / x.size() - s.mean * s.mean;
  s.rms = std::sqrt(var > 0 ? var : 0);  // RMS of the AC part
  s.peakToPeak = s.max - s.min;
  return s;
}

void removeLinearTrend(std::vector<double>& x) {
  const size_t n = x.size();
  if (n < 2) {
    if (n == 1) x[0] = 0;
    return;
  }
  double st = 0, sx = 0, stt = 0, stx = 0;
  for (size_t i = 0; i < n; ++i) {
    const double t = static_cast<double>(i);
    st += t;
    sx += x[i];
    stt += t * t;
    stx += t * x[i];
  }
  const double d = n * stt - st * st;
  const double b = d != 0 ? (n * stx - st * sx) / d : 0;
  const double a = (sx - b * st) / n;
  for (size_t i = 0; i < n; ++i) x[i] -= a + b * static_cast<double>(i);
}

std::vector<double> resampleUniform(const std::vector<double>& t, const std::vector<double>& x, double fs) {
  std::vector<double> out;
  if (t.size() < 2 || t.size() != x.size() || fs <= 0) return out;
  const double t0 = t.front(), t1 = t.back();
  const size_t n = static_cast<size_t>(std::floor((t1 - t0) * fs)) + 1;
  out.reserve(n);
  size_t j = 0;
  for (size_t i = 0; i < n; ++i) {
    const double ti = t0 + static_cast<double>(i) / fs;
    while (j + 2 < t.size() && t[j + 1] < ti) ++j;
    const double dt = t[j + 1] - t[j];
    const double a = dt > 0 ? (ti - t[j]) / dt : 0;
    out.push_back(x[j] + (x[j + 1] - x[j]) * std::max(0.0, std::min(1.0, a)));
  }
  return out;
}

Spectrum amplitudeSpectrum(const std::vector<double>& uniform, double fs, double fMin, double fMax) {
  Spectrum s;
  s.fs = fs;
  s.n = static_cast<int>(uniform.size());
  if (uniform.size() < 16 || fs <= 0) return s;
  std::vector<double> x = uniform;
  removeLinearTrend(x);
  const size_t n = x.size();
  size_t nfft = 1;
  while (nfft < n) nfft <<= 1;
  nfft <<= 1;  // 2x zero padding for a smoother display
  std::vector<std::complex<double>> buf(nfft, 0.0);
  double wsum = 0;
  for (size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2 * M_PI * static_cast<double>(i) / static_cast<double>(n - 1));
    buf[i] = x[i] * w;
    wsum += w;
  }
  fftInPlace(buf);
  const size_t half = nfft / 2;
  s.freq.resize(half + 1);
  s.amp.resize(half + 1);
  for (size_t k = 0; k <= half; ++k) {
    s.freq[k] = fs * static_cast<double>(k) / static_cast<double>(nfft);
    const double scale = (k == 0 || k == half) ? 1.0 : 2.0;
    s.amp[k] = scale * std::abs(buf[k]) / wsum;  // coherent-gain corrected peak amplitude
  }
  const double fHi = fMax > 0 ? fMax : 0.8 * fs / 2;
  size_t best = 0;
  for (size_t k = 1; k < half; ++k) {
    if (s.freq[k] < fMin || s.freq[k] > fHi) continue;
    if (best == 0 || s.amp[k] > s.amp[best]) best = k;
  }
  if (best == 0) return s;
  // Gaussian (log-parabolic) interpolation suits the Hann main lobe.
  const double a = std::log(std::max(s.amp[best - 1], 1e-300));
  const double b = std::log(std::max(s.amp[best], 1e-300));
  const double c = std::log(std::max(s.amp[best + 1], 1e-300));
  const double den = a - 2 * b + c;
  double delta = den < 0 ? 0.5 * (a - c) / den : 0.0;
  delta = std::max(-0.5, std::min(0.5, delta));
  s.peakHz = (static_cast<double>(best) + delta) * fs / static_cast<double>(nfft);
  s.peakAmp = std::exp(b - 0.25 * (a - c) * delta);
  s.valid = true;
  return s;
}

}  // namespace vm
