#pragma once

#include <cstdint>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace vm {

inline bool depthRedrawNeeded(bool newMap, bool interactive, int64_t sinceLastMs) {
  return newMap || interactive || sinceLastMs >= 1000;
}

// Display-only range state: update once per new depth map, not once per UI
// redraw. Auto selects once and then locks: moving foreground must not
// recolour the entire unchanged background. Tap Auto again to reselect.
struct DepthDisplayRange {
  double lo = 0, hi = 0;
  int64_t stamp = -1;
  uint64_t version = 0;
  void reset() { stamp = -1; lo = hi = 0; }
  void update(double nearMm, double farMm, int64_t mapStamp, uint64_t calibrationVersion) {
    if (stamp == mapStamp && version == calibrationVersion) return;
    if (stamp < 0 || version != calibrationVersion || mapStamp < stamp) {
      lo = nearMm; hi = std::max(nearMm + 100, farMm);
    }
    stamp = mapStamp; version = calibrationVersion;
  }
};

// Depth map of the stereo pipeline (AN5642 left/right cameras). The stereo
// module publishes it through Application::publishDepth(); the UI shows it.
struct DepthMap {
  int w = 0, h = 0;
  std::vector<uint16_t> mm;   // depth per pixel in millimetres, 0 = invalid
  std::vector<uint8_t> leftBgr;  // same-pair rectified colour, 3 bytes/pixel B,G,R
  std::vector<uint8_t> left;  // rectified left image of the same size (optional)
  std::string backend;        // "PS" or "PL"
  double fps = 0, procMs = 0;
  int64_t monoNs = 0;         // capture time of the left frame (CLOCK_MONOTONIC)
  // Stereo provenance: depth = focalPx * baselineMm / disparity.
  std::string calibrationId;
  uint64_t calibrationVersion = 0;  // StereoService::calibrationVersion() of that calibration
  double focalPx = 0, baselineMm = 0;
  double pairSkewMs = 0;      // |t_left - t_right| of the frames used (software pairing)
  double validFraction = 0;
  std::vector<int16_t> dispQ4;  // disparity in 1/16 px, -1 invalid (same size)
  std::vector<uint8_t> filled;  // 1 = hole-filled for display, not measured (may be empty)
  // A SEPARATE view, never substituted for mm or dispQ4 by any measurement.
  std::vector<uint16_t> displayMm;
  std::vector<uint8_t> displayEstimated;

  // UI-only regional reading when the exact pixel is missing: require six
  // measured neighbours with a single coherent depth layer. Display estimates
  // never contribute; boundaries between different surfaces remain unknown.
  uint16_t nearbyMeasured(double nx, double ny) const {
    if (w <= 0 || h <= 0 || mm.size() != static_cast<size_t>(w)*h) return 0;
    const int cx = static_cast<int>(nx*w), cy = static_cast<int>(ny*h);
    if (cx<0 || cy<0 || cx>=w || cy>=h) return 0;
    std::vector<uint16_t> values;
    for (int y=std::max(0,cy-3);y<=std::min(h-1,cy+3);++y)
      for (int x=std::max(0,cx-3);x<=std::min(w-1,cx+3);++x) {
        const size_t i=static_cast<size_t>(y)*w+x;
        if(mm[i] && (filled.empty() || !filled[i])) values.push_back(mm[i]);
      }
    if(values.size()<6) return 0;
    std::sort(values.begin(),values.end());
    const uint16_t z=values[values.size()/2];
    if(values[values.size()*9/10]-values[values.size()/10] > std::max(40,int(z)/10)) return 0;
    return z;
  }

  // Measured depth at normalised image coordinates; 0 when invalid, hole-filled or outside.
  uint16_t at(double nx, double ny) const {
    if (w <= 0 || h <= 0 || mm.size() != static_cast<size_t>(w) * h) return 0;
    const int x = static_cast<int>(nx * w), y = static_cast<int>(ny * h);
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    const size_t i = static_cast<size_t>(y) * w + x;
    return (!filled.empty() && filled[i]) ? 0 : mm[i];
  }
};

}  // namespace vm
