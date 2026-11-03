#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "measure/frame_source.h"
#include "measure/tracker.h"

namespace vm {

// Multi-point plate vibration with the team's measurement core
// (third_party/axvibration, from AX7010_Vibration_Linux_20261021): fixed
// 25 x 25 templates on a grid inside the measuring box, at least three
// patches on static background removing camera translation, and 128-frame
// windows (detrended Hann FFT, blind peak, sine fit) gated point by point
// against a motor-off noise baseline. Amplitudes are projected image pixels:
// no mm, no alarm thresholds.
struct MultiPointConfig {
  int maxPoints = 12;     // grid points in the measuring box (the core takes 60)
  int pointPitchPx = 32;  // grid spacing of the measuring points
  int refPitchPx = 32;    // reference group: 2 x 2 patches this far apart
  int gridCols = 0, gridRows = 0;  // optional preferred grid; 0 selects automatic layout
  double baselineS = 15;  // motor-off baseline capture
  std::string baselineDir = "/home/root/vibmon/baselines";
};

struct MultiPointLayout {
  int width = 0, height = 0;
  std::vector<std::array<float, 2>> points, refs;  // template centres, image pixels
  std::string error;                               // result reason when unusable, else empty
};

// Grid points in box and four references around (refCx, refCy) (normalised).
MultiPointLayout multiPointLayout(int width, int height, const Roi& box, double refCx, double refCy,
                                  const MultiPointConfig& cfg);
// The core's configuration hash: a baseline belongs to exactly one layout.
uint32_t multiPointHash(const MultiPointLayout& l);
// Newest baseline in dir for this layout that the core accepts; empty if none.
// allowedPaths restricts lookup to baselines explicitly accepted in this run.
std::string findMultiPointBaseline(const std::string& dir, const MultiPointLayout& l, int* windows = nullptr,
                                  const std::set<std::string>* allowedPaths = nullptr);
// "<dir>/<hash>-<tag>.vbb"
std::string multiPointBaselinePath(const std::string& dir, uint32_t hash, const std::string& tag);

// The core's window status names: SIGNAL, NO_SIGNAL, INVALID_WINDOW,
// BASELINE_MISMATCH, BASELINE, WARMUP.
const char* multiPointStatusName(int status);

// One analysed window (vr_analyze).
struct MultiPointWindow {
  // 1 signal, 0 below the baseline, -1 window rejected (gap, timing, lost
  // tracking), -2 no baseline or sampling rate differs from it, 2 baseline
  // window collected, -3 none yet.
  int status = -3;
  double frequencyHz = 0, fs = 0;
  int validSpectra = 0, passed = 0;
  std::vector<float> rmsPx, gateRatio;  // per point
  std::vector<uint8_t> valid, gate;
  uint32_t sequence = 0;
  int64_t tNs = 0;
};

class MultiPointTracker {
 public:
  MultiPointTracker();
  ~MultiPointTracker();
  // collect: gather a baseline; otherwise baselinePath must hold one for layout.
  bool start(const MultiPointLayout& layout, bool collect, const std::string& baselinePath, std::string& err);
  // Once per captured frame, in order. True when a window was analysed.
  // Prepare templates before the first timed sample; startup work is not a sample.
  bool prime(const GrayFrame& frame);
  bool push(const GrayFrame& f);
  const MultiPointWindow& lastWindow() const { return window_; }
  // Last frame: displacement of point i with the reference motion removed.
  bool pointValid(int i) const;
  float pointU(int i) const;
  float pointV(int i) const;
  bool referenceValid() const;
  // Parts of the next frame push() reads: around every template where it was
  // last tracked (the rest of the frame need not be converted).
  std::vector<Roi> regions() const;
  uint32_t frames() const;
  int baselineWindows() const;
  bool saveBaseline(const std::string& newPath) const;  // refuses to overwrite

 private:
  void blurAround(const GrayFrame& f);
  struct Impl;
  std::unique_ptr<Impl> impl_;
  MultiPointWindow window_;
};

}  // namespace vm
