#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace vm {

// Rectified 8-bit gray image (left or right camera, same size).
struct StereoImage {
  int w = 0, h = 0;
  std::vector<uint8_t> px;
  uint8_t at(int x, int y) const { return px[static_cast<size_t>(y) * w + x]; }
};

// Disparity of the left image in 1/16 px (Q4), kInvalidDisparity where no
// trustworthy match exists. Invalid pixels are never filled in.
constexpr int16_t kInvalidDisparity = -1;
struct DisparityMap {
  int w = 0, h = 0;
  std::vector<int16_t> q4;
  // 1 where q4 was filled in by hole filling (display only, not measured).
  // Empty when no filling ran. Metrology must use measured pixels only.
  std::vector<uint8_t> filled;
  int valid() const;  // measured pixels
  bool measured(size_t i) const { return q4[i] >= 0 && (filled.empty() || !filled[i]); }
};

// Census + semi-global matching (Hirschmueller 2008) with the checks needed
// for a measurement device: uniqueness, left-right consistency, sub-pixel
// parabola, 3x3 median and speckle removal.
//
// Structure is chosen for a later PL port: census and Hamming cost are
// per-pixel streaming operations; with paths = 4 only the four "forward"
// directions (left->right, top-left, top, top-right) are used, which a raster
// scan can aggregate with one row buffer per direction. 8 paths add the four
// reverse directions (two-pass, PS only).
struct SgmParams {
  int maxDisp = 64;      // disparities 0..maxDisp-1
  int census = 7;        // 5: 5x5 (24 bit), 7: 7x7 (48 bit), 9: center-symmetric 9x7 (31 bit)
  int paths = 4;         // 0 (no aggregation), 2 (left->right, top), 4 (forward), 8 (all)
  int p1 = 7, p2 = 86;   // smoothness penalties (cost units: Hamming bits)
  bool adaptiveP2 = true;  // P2 / (1 + |dI|/16): allow jumps at intensity edges
  int uniquenessPct = 5;   // best must be this % below the best non-neighbour
  bool lrCheck = true;
  int lrTolerance = 1;     // px
  bool median = true;      // 3x3 median over valid neighbours
  int speckleSize = 60;    // regions smaller than this (px) are removed; 0 = off
  int speckleDiffQ4 = 16;  // neighbours within 1 px join a region
  int subpixel = 1;        // 0: parabola, 1: equiangular (V) fit
  int minContrast = 4;     // 5x5 gray-level range below this: no texture, invalid
  // 1D Lucas-Kanade refinement of every valid disparity on the rectified
  // images (window refineRadius, gain/offset normalised, <= 5 iterations,
  // must stay within 1 px of the SGM value). Removes the pixel locking of
  // cost-curve interpolation; same arithmetic as the PL lk_accum displacement engine.
  bool refineLk = true;
  int refineRadius = 3;
  // 0: keep the SGM value when refinement fails (default);
  // 1: a pixel whose refinement runs more than 1 px away from the SGM value
  //    (window across a depth edge: foreground fattening) becomes invalid;
  // 2: any failed refinement (also weak texture / no convergence) invalidates.
  // On Middlebury 1 and 2 remove 30-35 % of the measured pixels for about one
  // point less of wrong matches, so they are off by default.
  int refineReject = 0;
  // Hole filling for display: invalid runs of at most fillMaxRun px along a
  // row between two measured pixels are interpolated when both ends agree
  // within 1 px, otherwise given the smaller (background) disparity, as an
  // occlusion next to a depth step; then column runs still empty between two
  // measured pixels that agree within 1 px are interpolated. Marked in
  // DisparityMap::filled.
  bool fillHoles = true;
  int fillMaxRun = 24;
};

// Refines disparity d0 (px) of left pixel (x, y). Returns 0 on convergence,
// 1 if it cannot refine (border, weak texture, no convergence in 5 steps),
// 2 if the estimate runs more than 1 px away from d0.
int refineDisparityLk(const StereoImage& left, const StereoImage& right, int x, int y, double d0, int radius,
                      double& d);

DisparityMap matchSgm(const StereoImage& left, const StereoImage& right, const SgmParams& p);

// Fixed parameters of the PL stereo core (ax7010/pl/stereo, stereo_axi.v CONFIG
// register): D 64, center-symmetric 9x7 census, 2 paths, P1 7, P2 86
// (adaptive), uniqueness 5 %, LR tolerance 1. Post-processing fields keep
// the defaults of SgmParams.
SgmParams plCoreParams(const SgmParams& post = SgmParams());
// What the PL core returns for a rectified pair (bit-exact model): census + sgmRaw.
DisparityMap plCoreModel(const StereoImage& left, const StereoImage& right);

// Reuse the PL's top/left paths on vertically flipped images to check the
// opposite vertical direction. Disparities still refer to the left camera.
StereoImage flipStereoRows(const StereoImage& image);
DisparityMap fuseVerticalPasses(const DisparityMap& forward, const DisparityMap& flipped, int toleranceQ4 = 16);

// Reuse the fixed 64-disparity PL kernel for a second, shifted search band.
StereoImage shiftStereoRight(const StereoImage& image, int offset);
DisparityMap offsetDisparities(DisparityMap map, int offset);
// Conflicting bands must have a clear normalised patch-correlation winner; ambiguous pixels stay invalid.
DisparityMap mergeDisparityRanges(const StereoImage& left, const StereoImage& right,
                                   const DisparityMap& low, const DisparityMap& high);

// The two stages of matchSgm, split where the PL/PS boundary lies:
//  - censusTransform: descriptors (PS today; returns bits and border width).
//  - sgmRaw: cost, path aggregation, winner-take-all, uniqueness, sub-pixel
//    fit and (if p.lrCheck) left-right check. With paths = 2, lrCheck = false
//    this is exactly what the PL core ax7010/pl/stereo/rtl/sgm_core.v computes.
//  - stereoPostProcess: texture gate, LK refinement, median, speckle (PS).
std::vector<uint64_t> censusTransform(const StereoImage& im, int mode, int& bits, int& border);
DisparityMap sgmRaw(const std::vector<uint64_t>& cl, const std::vector<uint64_t>& cr, const StereoImage& left,
                    int bits, int border, const SgmParams& p);
void stereoPostProcess(const StereoImage& left, const StereoImage& right, int border, const SgmParams& p,
                       DisparityMap& d);

// Software model of the team's PL BM path (sdoira RTL as adapted on
// 2026-10-16 plus the V1.2 PS spatial-support filter), for comparison:
// X-Sobel clipped to [-32, 31], 7x7 SAD, winner-take-all, uniqueness
// min1/min2 <= 921/1024 (second minimum not adjacent to the first),
// equiangular sub-pixel fit, then keep a pixel when >= 4 of its 3x3
// neighbourhood agree within 2 px. Temporal filtering is not modelled.
struct BmParams {
  int maxDisp = 64;
  int window = 7;
  int uniqQ10 = 921;
  bool spatialSupport = true;
};
DisparityMap matchBmBaseline(const StereoImage& left, const StereoImage& right, const BmParams& p);

}  // namespace vm
