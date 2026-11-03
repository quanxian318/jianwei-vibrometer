#include "measure/acoustic.h"

#include <algorithm>
#include <cmath>
#include <complex>

#include "measure/spectrum.h"

namespace vm {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFull = 32768.0;
constexpr double kEnvelopeRate = 1250;  // envelope and direct paths after decimation
constexpr double kSettleS = 0.25;       // filter start-up dropped from both paths

// Second-order section, transposed direct form II.
struct Biquad {
  double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  double step(double x) {
    const double y = b0 * x + z1;
    z1 = b1 * x - a1 * y + z2;
    z2 = b2 * x - a2 * y;
    return y;
  }
};

// Butterworth sections (Q = 1/sqrt 2); two in series give 24 dB/octave.
Biquad section(double f0, double fs, bool high) {
  const double w = 2 * kPi * f0 / fs, c = std::cos(w), alpha = std::sin(w) / std::sqrt(2.0);
  const double a0 = 1 + alpha;
  Biquad q;
  q.b0 = (high ? (1 + c) : (1 - c)) / 2 / a0;
  q.b1 = (high ? -(1 + c) : (1 - c)) / a0;
  q.b2 = q.b0;
  q.a1 = -2 * c / a0;
  q.a2 = (1 - alpha) / a0;
  return q;
}

struct Chain {
  std::vector<Biquad> s;
  void add(double f0, double fs, bool high) { s.push_back(section(f0, fs, high)); }
  double step(double x) {
    for (Biquad& q : s) x = q.step(x);
    return x;
  }
};

// Power spectrum of x (mean removed, Hann window, zero padded to at least
// pad x its length): p[k] at f[k] = k fs / N.
void powerSpectrum(const std::vector<double>& x, double fs, int pad, std::vector<double>& f, std::vector<double>& p) {
  f.clear();
  p.clear();
  const size_t n = x.size();
  if (n < 16) return;
  double mean = 0;
  for (double v : x) mean += v;
  mean /= n;
  size_t len = 1;
  while (len < n * static_cast<size_t>(pad)) len <<= 1;
  std::vector<std::complex<double>> a(len);
  for (size_t i = 0; i < n; ++i) {
    const double w = 0.5 - 0.5 * std::cos(2 * kPi * i / (n - 1));
    a[i] = (x[i] - mean) * w;
  }
  fftInPlace(a);
  f.resize(len / 2 + 1);
  p.resize(len / 2 + 1);
  for (size_t k = 0; k <= len / 2; ++k) {
    f[k] = k * fs / len;
    p[k] = std::norm(a[k]);
  }
}

// Strongest separate peaks in [lo, hi]: power over the band median at least
// minDb (not an absolute SNR or a statistical confidence), log-parabolic
// interpolation, peaks closer than spacing to a stronger one are skipped.
std::vector<AcousticPeak> findPeaks(const std::vector<double>& f, const std::vector<double>& p, double lo, double hi,
                                    double minDb, double spacing, size_t maxPeaks) {
  std::vector<size_t> band;
  for (size_t k = 1; k + 1 < f.size(); ++k)
    if (f[k] >= lo && f[k] <= hi) band.push_back(k);
  std::vector<AcousticPeak> out;
  if (band.size() < 5) return out;
  std::vector<double> sorted;
  for (size_t k : band) sorted.push_back(p[k]);
  std::nth_element(sorted.begin(), sorted.begin() + sorted.size() / 2, sorted.end());
  const double median = std::max(sorted[sorted.size() / 2], 1e-30);
  std::sort(band.begin(), band.end(), [&](size_t a, size_t b) { return p[a] > p[b]; });
  const double df = f[1] - f[0];
  for (size_t k : band) {
    const double prominence = 10 * std::log10(std::max(p[k], 1e-30) / median);
    if (prominence < minDb) break;
    // Only local maxima start a peak (the rest belong to a stronger one's lobe).
    if (p[k] < p[k - 1] || p[k] < p[k + 1]) continue;
    const double a = std::log(std::max(p[k - 1], 1e-30)), b = std::log(std::max(p[k], 1e-30)),
                 c = std::log(std::max(p[k + 1], 1e-30));
    const double div = a - 2 * b + c;
    const double delta = std::fabs(div) > 1e-12 ? std::max(-0.5, std::min(0.5, 0.5 * (a - c) / div)) : 0.0;
    const double hz = f[k] + delta * df;
    bool near = false;
    for (const AcousticPeak& q : out) near = near || std::fabs(q.hz - hz) < spacing;
    if (near) continue;
    out.push_back(AcousticPeak{hz, prominence});
    if (out.size() >= maxPeaks) break;
  }
  return out;
}

// Prominence a peak needs: the configured floor, raised with the number of
// independent bins searched (the band power of noise is exponentially
// distributed per bin) so that pure noise passes in about 1 % of windows.
double peakThreshold(const AcousticConfig& cfg, double spanS, size_t tests) {
  const double bins = std::max(1.0, (cfg.bandHighHz - cfg.bandLowHz) * spanS) * std::max<size_t>(1, tests);
  return std::max(cfg.minProminenceDb, 10 * std::log10(std::log(100 * bins) / std::log(2.0)));
}

// Mean of each block of d samples after the start-up is dropped.
std::vector<double> blockMean(const std::vector<double>& x, size_t d, size_t skip) {
  std::vector<double> out;
  for (size_t i = skip; i + d <= x.size(); i += d) {
    double s = 0;
    for (size_t j = 0; j < d; ++j) s += x[i + j];
    out.push_back(s / d);
  }
  return out;
}

void compare(AcousticAnalysis& r) {
  if (r.envelopePeaks.empty()) {
    r.relation = "no_peak";
    return;
  }
  r.acousticHz = r.envelopePeaks[0].hz;
  if (r.visualHz <= 0) {
    r.relation = "no_visual";
    return;
  }
  const double v = r.visualHz;
  r.toleranceHz = std::max({0.2, 2.0 / r.durationS, 0.02 * v});
  const size_t top = std::min<size_t>(3, r.envelopePeaks.size());
  for (size_t i = 0; i < top; ++i) {
    if (std::fabs(r.envelopePeaks[i].hz - v) <= r.toleranceHz) {
      r.relation = "agree";
      r.acousticHz = r.envelopePeaks[i].hz;
      r.diffHz = r.acousticHz - v;
      return;
    }
  }
  for (size_t i = 0; i < top; ++i) {
    const double a = r.envelopePeaks[i].hz;
    for (int k = 2; k <= 4; ++k) {
      if (std::fabs(a - k * v) <= k * r.toleranceHz || std::fabs(k * a - v) <= r.toleranceHz) {
        r.relation = "harmonic";
        r.harmonic = std::fabs(a - k * v) <= k * r.toleranceHz ? k : -k;
        r.acousticHz = a;
        r.diffHz = a - v;
        return;
      }
    }
  }
  r.relation = "differ";
  r.diffHz = r.acousticHz - v;
}

}  // namespace

AcousticAnalysis analyzeAcoustic(const int16_t* pcm, size_t n, double fs, double visualHz, const AcousticConfig& cfg) {
  AcousticAnalysis r;
  r.sampleRate = fs;
  r.visualHz = visualHz;
  r.durationS = fs > 0 ? n / fs : 0;
  if (n < 64 || fs <= 0 || r.durationS < cfg.minDurationS) {
    r.reason = "too_short";
    return r;
  }

  // Level, offset and clipping.
  double sum = 0, peak = 0;
  size_t clipped = 0;
  for (size_t i = 0; i < n; ++i) {
    sum += pcm[i];
    clipped += pcm[i] >= 32767 || pcm[i] <= -32768;
  }
  const double mean = sum / n;
  double sq = 0;
  std::vector<double> x(n);
  for (size_t i = 0; i < n; ++i) {
    x[i] = pcm[i] - mean;
    sq += x[i] * x[i];
    peak = std::max(peak, std::fabs(static_cast<double>(pcm[i])));
  }
  const double rms = std::sqrt(sq / n);
  r.dcFraction = mean / kFull;
  r.clipFraction = static_cast<double>(clipped) / n;
  r.rmsDbfs = rms > 0 ? 20 * std::log10(rms / (kFull / std::sqrt(2.0))) : -200;
  r.peakDbfs = peak > 0 ? 20 * std::log10(peak / kFull) : -200;
  // A stuck line (no microphone, the wrong half of the clock) gives a
  // constant full-scale PCM value.
  if (std::fabs(r.dcFraction) > 0.9) {
    r.reason = "stuck";
    return r;
  }
  if (r.rmsDbfs < -100) {
    r.reason = "no_signal";
    return r;
  }
  if (r.clipFraction > 0.01) {
    r.reason = "clipped";
    return r;
  }

  const size_t dec = std::max<size_t>(1, static_cast<size_t>(std::lround(fs / kEnvelopeRate)));
  const double rate = fs / dec;
  const size_t skip = static_cast<size_t>(kSettleS * fs);
  const double lp = std::min(2.0 * cfg.bandHighHz, 0.4 * rate);

  // Envelope of each carrier band: band-pass, full-wave rectification,
  // low-pass, block mean. The band whose strongest peak stands out most wins.
  {
    std::vector<double> f, p;
    double best = -1;
    for (const auto& cand : cfg.carrierBands) {
      const double lo = cand[0], hi = std::min(cand[1], 0.45 * fs);
      if (lo <= 0 || hi <= 1.5 * lo) continue;
      Chain band, smooth;
      band.add(lo, fs, true);
      band.add(lo, fs, true);
      band.add(hi, fs, false);
      band.add(hi, fs, false);
      smooth.add(lp, fs, false);
      smooth.add(lp, fs, false);
      std::vector<double> e(n);
      for (size_t i = 0; i < n; ++i) e[i] = smooth.step(std::fabs(band.step(x[i])));
      const std::vector<double> env = blockMean(e, dec, skip);
      double em = 0, ev = 0;
      for (double v : env) em += v;
      em /= std::max<size_t>(1, env.size());
      for (double v : env) ev += (v - em) * (v - em);
      AcousticAnalysis::Band b;
      b.lowHz = lo;
      b.highHz = hi;
      b.modulation = em > 0 ? std::sqrt(ev / std::max<size_t>(1, env.size())) / em : 0;
      std::vector<double> bf, bp;
      powerSpectrum(env, rate, 4, bf, bp);
      std::vector<AcousticPeak> pk;
      // A steady tone or hiss has no usable envelope.
      if (b.modulation >= 0.01) {
        pk = findPeaks(bf, bp, cfg.bandLowHz, cfg.bandHighHz, peakThreshold(cfg, env.size() / rate, cfg.carrierBands.size()),
                       std::max(1.0, 2.5 * rate / env.size()), 5);
      }
      if (!pk.empty()) b.top = pk[0];
      r.bands.push_back(b);
      // No peak anywhere: show the widest band.
      const double score = pk.empty() ? (hi - lo) * 1e-9 : pk[0].prominenceDb;
      if (score > best) {
        best = score;
        r.envelopePeaks = pk;
        r.modulation = b.modulation;
        r.carrierLowHz = lo;
        r.carrierHighHz = hi;
        f.swap(bf);
        p.swap(bp);
      }
    }
    // Display copy: 0.1 Hz buckets from half the band's lower edge (slow level
    // drift below it would set the scale), amplitude relative to the largest.
    double top = 0;
    for (size_t k = 1; k < f.size() && f[k] <= cfg.bandHighHz; ++k)
      if (f[k] >= cfg.bandLowHz * 0.5) top = std::max(top, p[k]);
    if (top > 0) {
      for (double b = std::floor(cfg.bandLowHz * 5) / 10; b < cfg.bandHighHz; b += 0.1) {
        double m = 0;
        for (size_t k = static_cast<size_t>(b / (f[1] - f[0])); k < f.size() && f[k] < b + 0.1; ++k) m = std::max(m, p[k]);
        r.envFreq.push_back(b + 0.05);
        r.envAmp.push_back(std::sqrt(m / top));
      }
    }
  }

  // Direct low-frequency sound: low-pass, block mean, spectrum.
  {
    Chain smooth;
    smooth.add(lp, fs, false);
    smooth.add(lp, fs, false);
    std::vector<double> y(n);
    for (size_t i = 0; i < n; ++i) y[i] = smooth.step(x[i]);
    const std::vector<double> low = blockMean(y, dec, skip);
    std::vector<double> f, p;
    powerSpectrum(low, rate, 4, f, p);
    r.directPeaks = findPeaks(f, p, cfg.bandLowHz, cfg.bandHighHz, peakThreshold(cfg, low.size() / rate, 1),
                              std::max(1.0, 2.5 / (low.size() / rate)), 5);
  }

  // Strongest tone 100 Hz..10 kHz: Welch average of Hann segments.
  {
    size_t seg = 8192;
    while (seg > 256 && seg > n) seg >>= 1;
    std::vector<double> acc(seg / 2 + 1, 0.0);
    std::vector<std::complex<double>> a(seg);
    int count = 0;
    for (size_t start = 0; start + seg <= n; start += seg / 2, ++count) {
      for (size_t i = 0; i < seg; ++i) a[i] = x[start + i] * (0.5 - 0.5 * std::cos(2 * kPi * i / (seg - 1)));
      fftInPlace(a);
      for (size_t k = 0; k <= seg / 2; ++k) acc[k] += std::norm(a[k]);
    }
    std::vector<double> f(seg / 2 + 1);
    for (size_t k = 0; k <= seg / 2; ++k) f[k] = k * fs / seg;
    if (count > 0) {
      const std::vector<AcousticPeak> t =
          findPeaks(f, acc, 100.0, std::min(10000.0, 0.45 * fs), cfg.minProminenceDb, 3 * fs / seg, 1);
      if (!t.empty()) r.tone = t[0];
    }
  }

  r.valid = true;
  compare(r);
  return r;
}

}  // namespace vm
