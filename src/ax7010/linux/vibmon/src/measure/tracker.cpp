#include "measure/tracker.h"

#include <cmath>
#include <cstdint>

namespace vm {

namespace {
const float kInvalid = -2.f;
}

bool NccTracker::init(const GrayFrame& ref, const Roi& roi, int searchRadius) {
  radius_ = searchRadius < 2 ? 2 : searchRadius;
  roi_ = roi;
  const int margin = radius_ + 3;
  if (roi_.w < 8 || roi_.h < 8) return false;
  if (roi_.x < margin) roi_.x = margin;
  if (roi_.y < margin) roi_.y = margin;
  if (roi_.x + roi_.w + margin > ref.width) roi_.x = ref.width - roi_.w - margin;
  if (roi_.y + roi_.h + margin > ref.height) roi_.y = ref.height - roi_.h - margin;
  if (roi_.x < margin || roi_.y < margin) return false;

  const size_t n = static_cast<size_t>(roi_.w) * roi_.h;
  tmpl_.resize(n);
  double sum = 0;
  for (int y = 0; y < roi_.h; ++y) {
    for (int x = 0; x < roi_.w; ++x) {
      float v = ref.at(roi_.x + x, roi_.y + y);
      tmpl_[static_cast<size_t>(y) * roi_.w + x] = v;
      sum += v;
    }
  }
  const float mean = static_cast<float>(sum / n);
  double sq = 0;
  for (auto& v : tmpl_) {
    v -= mean;
    sq += static_cast<double>(v) * v;
  }
  tNorm_ = static_cast<float>(std::sqrt(sq));
  tStd_ = std::sqrt(sq / n);
  cx_ = cy_ = 0;
  const int side = 2 * radius_ + 5;
  scores_.assign(static_cast<size_t>(side) * side, kInvalid);
  return tStd_ >= 2.0;  // a flat region cannot be tracked
}

NccTracker::Result NccTracker::track(const GrayFrame& f) {
  Result res;
  const int side = 2 * radius_ + 5;
  for (auto& s : scores_) s = kInvalid;
  const int w = roi_.w, h = roi_.h;
  const size_t n = static_cast<size_t>(w) * h;

  // Score of integer offset (u, v) relative to the reference ROI position.
  auto score = [&](int u, int v) -> float {
    const int gu = u - cx_ + radius_ + 2, gv = v - cy_ + radius_ + 2;
    if (gu < 0 || gv < 0 || gu >= side || gv >= side) return kInvalid;
    float& slot = scores_[static_cast<size_t>(gv) * side + gu];
    if (slot != kInvalid) return slot;
    const int x0 = roi_.x + u, y0 = roi_.y + v;
    if (x0 < 0 || y0 < 0 || x0 + w > f.width || y0 + h > f.height) return slot = -1.5f;
    int64_t s1 = 0, s2 = 0;
    double num = 0;
    for (int y = 0; y < h; ++y) {
      const uint8_t* row = &f.pixels[static_cast<size_t>(y0 + y) * f.width + x0];
      const float* t = &tmpl_[static_cast<size_t>(y) * w];
      float acc = 0;
      int32_t r1 = 0, r2 = 0;
      for (int x = 0; x < w; ++x) {
        const int p = row[x];
        acc += p * t[x];
        r1 += p;
        r2 += p * p;
      }
      num += acc;
      s1 += r1;
      s2 += r2;
    }
    const double var = static_cast<double>(s2) - static_cast<double>(s1) * s1 / n;
    slot = var > 1e-6 ? static_cast<float>(num / (tNorm_ * std::sqrt(var))) : 0.f;
    return slot;
  };

  // Coarse grid (step 2) over the whole search range, then a full 5x5 search.
  int bu = cx_, bv = cy_;
  float best = -3.f;
  for (int v = cy_ - radius_; v <= cy_ + radius_; v += 2) {
    for (int u = cx_ - radius_; u <= cx_ + radius_; u += 2) {
      float s = score(u, v);
      if (s > best) {
        best = s;
        bu = u;
        bv = v;
      }
    }
  }
  const int cu = bu, cv = bv;
  for (int v = cv - 2; v <= cv + 2; ++v) {
    for (int u = cu - 2; u <= cu + 2; ++u) {
      if (std::abs(u - cx_) > radius_ + 1 || std::abs(v - cy_) > radius_ + 1) continue;
      float s = score(u, v);
      if (s > best) {
        best = s;
        bu = u;
        bv = v;
      }
    }
  }
  if (best < -1.f) return res;

  auto refine = [](float sm, float s0, float sp) -> double {
    if (sm < -1.f || sp < -1.f) return 0.0;
    const double d = sm - 2.0 * s0 + sp;
    if (d >= 0) return 0.0;
    double off = 0.5 * (sm - sp) / d;
    return off > 0.5 ? 0.5 : (off < -0.5 ? -0.5 : off);
  };
  res.dx = bu + refine(score(bu - 1, bv), best, score(bu + 1, bv));
  res.dy = bv + refine(score(bu, bv - 1), best, score(bu, bv + 1));
  res.score = best;
  res.atEdge = std::abs(bu - cx_) >= radius_ || std::abs(bv - cy_) >= radius_;
  res.ok = true;

  // Saturation inside the matched window corrupts the correlation.
  int sat = 0;
  for (int y = 0; y < h; y += 2) {
    const uint8_t* row = &f.pixels[static_cast<size_t>(roi_.y + bv + y) * f.width + roi_.x + bu];
    for (int x = 0; x < w; x += 2) sat += (row[x] == 0 || row[x] == 255) ? 1 : 0;
  }
  res.saturated = sat * 4 > static_cast<int>(n) / 20;
  cx_ = bu;
  cy_ = bv;
  return res;
}

}  // namespace vm
