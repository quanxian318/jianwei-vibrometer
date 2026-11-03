#pragma once

#include <vector>

#include "measure/frame_source.h"
#include "measure/lk_core.h"
#include "measure/tracker.h"

namespace vm {

// Translation-only inverse-compositional Lucas-Kanade tracker (candidate PL
// displacement core, route proposal 10_*, section 4.1).
//
// Split between PL and PS: every per-pixel operation goes through
// lkAccumulate() (bit-exact model of the PL lk_accum engine: bilinear warp +
// five integer multiply-accumulates); the scalar work per pass (ZNCC, 2x2
// update) is done here and stays on the PS.
//
// Per record the template is fixed. init() quantises the zero-mean template
// and its gradients, projected out of span{1, T} so that brightness offset and
// gain changes (auto exposure, light PWM) do not bias the update, and builds
// the 2x2 Hessian from the quantised values. track() does
//   1. integer initial position: ZNCC on a step-2 grid over +-radius around
//      the previous match, then a full 5x5 search (same pattern as NccTracker),
//   2. at most maxIterations sub-pixel updates on a 1/256 px grid.
// Quality outputs follow the proposal's quality_gate: weak texture and
// single-edge degeneracy (init), convergence, ZNCC and residual after
// alignment, saturation, search border and leaving the integer basin.
class LkTracker {
 public:
  struct Options {
    int maxIterations = 5;
    double convergePx = 0.01;      // |delta p| below this ends the iterations
    double minTextureStd = 2.0;    // template gray-level std, as NccTracker
    double minGradEnergy = 4.0;    // smallest Hessian eigenvalue per pixel, gray^2/px^2
    double minIsotropy = 0.05;     // lambda_min / lambda_max; below = single edge
    double maxDisplacementPx = 0;  // |d| from the reference beyond this counts as edge; 0 = off
  };

  struct Result : TrackResult {
    bool converged = false;
    int iterations = 0;
    double residual = 0;  // RMS of the gain/offset-normalised residual, gray levels
    double lastStep = 0;  // |delta p| of the final iteration, pixels
    bool walked = false;  // iterations left the +-1.5 px basin of the integer start
    int passes = 0;       // lk_accum passes used for this frame (PL cycle budget)
  };

  enum class InitStatus { kOk, kOutside, kTooLarge, kFlat, kWeakGradient, kAperture };

  LkTracker() = default;
  explicit LkTracker(const Options& o) : opt_(o) {}

  bool init(const GrayFrame& ref, const Roi& roi, int searchRadius);
  InitStatus initStatus() const { return status_; }
  Result track(const GrayFrame& frame);

  const Roi& roi() const { return roi_; }
  const LkCoeffs& coeffs() const { return c_; }
  double templateContrast() const { return tStd_; }
  // Displacement standard deviation per unit image noise (px per gray level),
  // from the inverse Hessian. Multiply by the image noise std for a lower bound.
  double sigmaPerNoiseX() const { return sigX_; }
  double sigmaPerNoiseY() const { return sigY_; }
  double isotropy() const { return isotropy_; }

 private:
  // One engine pass at displacement (u + ax/256, v + ay/256) from the ROI origin.
  bool pass(const GrayFrame& f, int u, int v, int ax, int ay, LkAccum& a) const;
  double zncc(const LkAccum& a, double* normI) const;

  Options opt_;
  Roi roi_;
  int radius_ = 8;
  InitStatus status_ = InitStatus::kOutside;
  LkCoeffs c_;
  int64_t sumT_ = 0, sumGx_ = 0, sumGy_ = 0;  // sums of the quantised coefficients
  double tNorm_ = 0, tStd_ = 0;               // of the quantised template, gray levels
  double cx_ = 0, cy_ = 0;                    // sum sd * T of the quantised values (not exactly 0)
  double hinv_[3] = {0, 0, 0};                // inverse Hessian: xx, xy, yy
  double sigX_ = 0, sigY_ = 0, isotropy_ = 0;
  double px_ = 0, py_ = 0;                    // previous displacement
};

}  // namespace vm
