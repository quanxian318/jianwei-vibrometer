#pragma once

#include <complex>
#include <vector>

namespace vm {

struct SeriesStats {
  double mean = 0, rms = 0, min = 0, max = 0, peakToPeak = 0;
};

// Single-sided amplitude spectrum: amp[k] is the peak amplitude (same unit as
// the signal) of a sinusoid at freq[k], Hann window with coherent-gain
// correction, linear trend removed first.
struct Spectrum {
  double fs = 0;          // sampling rate of the uniform series
  int n = 0;              // samples used (before zero padding)
  std::vector<double> freq, amp;
  double peakHz = 0;      // log-parabolic interpolation around the largest bin
  double peakAmp = 0;     // interpolated peak amplitude
  bool valid = false;
};

void fftInPlace(std::vector<std::complex<double>>& a);  // radix-2, size must be a power of two
SeriesStats seriesStats(const std::vector<double>& x);
void removeLinearTrend(std::vector<double>& x);

// Resamples irregular samples (t seconds, ascending) onto a uniform grid at fs by linear interpolation.
std::vector<double> resampleUniform(const std::vector<double>& t, const std::vector<double>& x, double fs);

// fMin/fMax limit the peak search (fMax <= 0 means 0.8 x Nyquist).
Spectrum amplitudeSpectrum(const std::vector<double>& uniform, double fs, double fMin, double fMax);

}  // namespace vm
