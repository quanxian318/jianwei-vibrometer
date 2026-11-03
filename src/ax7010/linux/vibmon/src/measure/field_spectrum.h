#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vm {

// Whole-field spectral line detection on pixel brightness ("dominant frequency map" of the
// FPGA vision plan, done once per capture on the PS).
//
// Each capture frame is block-averaged to a small grid. After the capture
// every grid cell's brightness series is detrended, Hann windowed and
// transformed; each spectrum is whitened by its own running median (+-1 Hz),
// so broadband camera shake and slow drift do not dominate. A frequency
// scores the fraction of active cells that show a narrow line there (> 4x the
// local median). A rotating part, a vibrating edge or its moving shadow gives
// a line shared by many cells, independent of whether a displacement ROI can
// see the motion.
//
// It is a frequency indicator only: brightness changes have no displacement
// unit, and flicker of lighting also produces lines (check against the
// mains/PWM frequencies and their aliases). Frequencies above fs/2 appear at
// their alias; annotate lines as possible harmonics with aliasHz().
struct FieldLine {
  double hz = 0;
  double fraction = 0;  // of active cells with a line at this frequency
  int harmonicOf = 0;   // k > 1: alias of k x the strongest line; 0 = none found
};

struct FieldSpectrumResult {
  bool valid = false;   // enough frames and a line clearly above the band's typical score
  std::string reason;
  double peakHz = 0, fraction = 0, medianFraction = 0, resolutionHz = 0;
  int gridW = 0, gridH = 0, frames = 0;
  std::vector<FieldLine> lines;  // strongest first, at most 5
};

// Apparent frequency of f after sampling at fs (folded into 0..fs/2).
double aliasHz(double f, double fs);

class FieldSpectrum {
 public:
  void reset(int frameW, int frameH, int gridW = 64);
  void add(const uint8_t* pixels, int width, int height);
  // fs: measured frame rate of the stored frames (uniform sampling required);
  // the line search covers fMin..fMax (fMax <= 0: 0.8 x Nyquist).
  FieldSpectrumResult analyze(double fs, double fMin, double fMax) const;
  int frames() const { return frames_; }

 private:
  int fw_ = 0, fh_ = 0, gw_ = 0, gh_ = 0, block_ = 1, frames_ = 0;
  std::vector<float> data_;  // frames x cells
};

}  // namespace vm
