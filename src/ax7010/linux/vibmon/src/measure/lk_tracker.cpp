#include "measure/lk_tracker.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace vm {

namespace {
const double kCoef = 1 << kLkCoefShift;
const double kPix = 1 << kLkFracBits;
}  // namespace

bool LkTracker::init(const GrayFrame& ref, const Roi& roi, int searchRadius) {
  radius_ = searchRadius < 2 ? 2 : searchRadius;
  roi_ = roi;
  px_ = py_ = 0;
  status_ = InitStatus::kOutside;
  const int margin = radius_ + 3;
  if (roi_.w * roi_.h > kLkMaxPixels) {
    status_ = InitStatus::kTooLarge;
    return false;
  }
  if (roi_.w < 8 || roi_.h < 8) return false;
  if (roi_.x < margin) roi_.x = margin;
  if (roi_.y < margin) roi_.y = margin;
  if (roi_.x + roi_.w + margin > ref.width) roi_.x = ref.width - roi_.w - margin;
  if (roi_.y + roi_.h + margin > ref.height) roi_.y = ref.height - roi_.h - margin;
  if (roi_.x < margin || roi_.y < margin) return false;

  const int w = roi_.w, h = roi_.h;
  const size_t n = static_cast<size_t>(w) * h;
  std::vector<double> t(n), gx(n), gy(n);
  double sum = 0;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y) * w + x;
      const int ix = roi_.x + x, iy = roi_.y + y;
      t[i] = ref.at(ix, iy);
      gx[i] = 0.5 * (ref.at(ix + 1, iy) - ref.at(ix - 1, iy));
      gy[i] = 0.5 * (ref.at(ix, iy + 1) - ref.at(ix, iy - 1));
      sum += t[i];
    }
  const double mean = sum / n;
  double sq = 0;
  for (auto& v : t) {
    v -= mean;
    sq += v * v;
  }
  tStd_ = std::sqrt(sq / n);
  if (tStd_ < opt_.minTextureStd) {
    status_ = InitStatus::kFlat;
    return false;
  }
  // Project the gradients out of span{1, T} (1 and T are orthogonal, T zero-mean).
  for (std::vector<double>* g : {&gx, &gy}) {
    double m = 0, dt = 0;
    for (size_t i = 0; i < n; ++i) {
      m += (*g)[i];
      dt += (*g)[i] * t[i];
    }
    m /= n;
    dt /= sq;
    for (size_t i = 0; i < n; ++i) (*g)[i] -= m + dt * t[i];
  }
  // Quantise to the engine's coefficient format; everything below uses the quantised values.
  c_.w = w;
  c_.h = h;
  c_.t.resize(n);
  c_.gx.resize(n);
  c_.gy.resize(n);
  sumT_ = sumGx_ = sumGy_ = 0;
  for (size_t i = 0; i < n; ++i) {
    c_.t[i] = lkQuantize(t[i]);
    c_.gx[i] = lkQuantize(gx[i]);
    c_.gy[i] = lkQuantize(gy[i]);
    sumT_ += c_.t[i];
    sumGx_ += c_.gx[i];
    sumGy_ += c_.gy[i];
  }
  const double tMean = sumT_ / kCoef / n;
  double tn = 0, hxx = 0, hxy = 0, hyy = 0;
  cx_ = cy_ = 0;
  for (size_t i = 0; i < n; ++i) {
    const double tq = c_.t[i] / kCoef - tMean, sx = c_.gx[i] / kCoef, sy = c_.gy[i] / kCoef;
    tn += tq * tq;
    hxx += sx * sx;
    hxy += sx * sy;
    hyy += sy * sy;
    cx_ += sx * tq;
    cy_ += sy * tq;
  }
  tNorm_ = std::sqrt(tn);
  const double tr = hxx + hyy, det = hxx * hyy - hxy * hxy;
  const double disc = std::sqrt(std::max(0.0, 0.25 * tr * tr - det));
  const double lmax = 0.5 * tr + disc, lmin = 0.5 * tr - disc;
  isotropy_ = lmax > 0 ? lmin / lmax : 0;
  if (det <= 0 || lmin / n < opt_.minGradEnergy) {
    status_ = InitStatus::kWeakGradient;
    return false;
  }
  hinv_[0] = hyy / det;
  hinv_[1] = -hxy / det;
  hinv_[2] = hxx / det;
  sigX_ = std::sqrt(hinv_[0]);
  sigY_ = std::sqrt(hinv_[2]);
  if (isotropy_ < opt_.minIsotropy) {
    status_ = InitStatus::kAperture;  // single edge: 2D displacement not identifiable
    return false;
  }
  status_ = InitStatus::kOk;
  return true;
}

bool LkTracker::pass(const GrayFrame& f, int u, int v, int ax, int ay, LkAccum& a) const {
  const int x0 = roi_.x + u, y0 = roi_.y + v;
  if (x0 < 0 || y0 < 0 || x0 + roi_.w + 1 > f.width || y0 + roi_.h + 1 > f.height) return false;
  a = lkAccumulate(&f.pixels[static_cast<size_t>(y0) * f.width + x0], f.width, c_, ax, ay);
  return true;
}

double LkTracker::zncc(const LkAccum& a, double* normI) const {
  const double n = static_cast<double>(c_.w) * c_.h;
  const double var = static_cast<double>(static_cast<int64_t>(n) * a.s2 - a.s1 * a.s1) / n;  // Q16
  const double norm = var > 0 ? std::sqrt(var) / kPix : 0;
  if (normI) *normI = norm;
  if (norm < 1e-6 || tNorm_ <= 0) return 0;
  const double cov = (static_cast<double>(a.st) - static_cast<double>(a.s1) * sumT_ / n) / (kCoef * kPix);
  return cov / (tNorm_ * norm);
}

LkTracker::Result LkTracker::track(const GrayFrame& f) {
  Result res;
  if (status_ != InitStatus::kOk) return res;
  const double n = static_cast<double>(c_.w) * c_.h;

  // 1. Integer initial position (ZNCC, coarse grid then 5x5), same pattern as NccTracker.
  const int cu = static_cast<int>(std::lround(px_)), cv = static_cast<int>(std::lround(py_));
  const int side = 2 * radius_ + 5;
  std::vector<double> scores(static_cast<size_t>(side) * side, -3.0);
  auto score = [&](int u, int v) -> double {
    const int gu = u - cu + radius_ + 2, gv = v - cv + radius_ + 2;
    if (gu < 0 || gv < 0 || gu >= side || gv >= side) return -3;
    double& slot = scores[static_cast<size_t>(gv) * side + gu];
    if (slot > -2.5) return slot;
    LkAccum a;
    if (!pass(f, u, v, 0, 0, a)) return slot = -1.5;
    ++res.passes;
    return slot = zncc(a, nullptr);
  };
  int bu = cu, bv = cv;
  double best = -3;
  for (int v = cv - radius_; v <= cv + radius_; v += 2)
    for (int u = cu - radius_; u <= cu + radius_; u += 2) {
      const double s = score(u, v);
      if (s > best) {
        best = s;
        bu = u;
        bv = v;
      }
    }
  const int gu = bu, gv = bv;
  for (int v = gv - 2; v <= gv + 2; ++v)
    for (int u = gu - 2; u <= gu + 2; ++u) {
      if (std::abs(u - cu) > radius_ + 1 || std::abs(v - cv) > radius_ + 1) continue;
      const double s = score(u, v);
      if (s > best) {
        best = s;
        bu = u;
        bv = v;
      }
    }
  if (best < -1) return res;

  // 2. Bounded inverse-compositional updates on the 1/256 px grid.
  auto split = [](double p, int& i, int& frac) {
    i = static_cast<int>(std::floor(p));
    frac = static_cast<int>(std::lround((p - i) * kPix));
    if (frac >= (1 << kLkFracBits)) {
      ++i;
      frac = 0;
    }
  };
  double px = bu, py = bv;
  LkAccum a;
  int iu, iv, ax, ay;
  for (int it = 0; it < opt_.maxIterations; ++it) {
    split(px, iu, ax);
    split(py, iv, ay);
    if (!pass(f, iu, iv, ax, ay, a)) return res;
    ++res.passes;
    double normI = 0;
    zncc(a, &normI);
    if (normI < 1e-6) return res;
    const double m = static_cast<double>(a.s1) / n;
    const double g = tNorm_ / normI;  // gain normalisation of the warped window
    const double bx = g * (a.sx - m * sumGx_) / (kCoef * kPix) - cx_;
    const double by = g * (a.sy - m * sumGy_) / (kCoef * kPix) - cy_;
    const double dpx = hinv_[0] * bx + hinv_[1] * by, dpy = hinv_[1] * bx + hinv_[2] * by;
    px -= dpx;
    py -= dpy;
    res.iterations = it + 1;
    res.lastStep = std::hypot(dpx, dpy);
    if (res.lastStep < opt_.convergePx) {
      res.converged = true;
      break;
    }
  }
  // Score and residual at the reported (quantised) position.
  split(px, iu, ax);
  split(py, iv, ay);
  if (!pass(f, iu, iv, ax, ay, a)) return res;
  ++res.passes;
  res.score = zncc(a, nullptr);
  res.residual = tNorm_ * std::sqrt(std::max(0.0, 2 * (1 - res.score)) / n);
  res.dx = iu + ax / kPix;
  res.dy = iv + ay / kPix;
  res.ok = true;
  res.walked = std::fabs(res.dx - bu) > 1.5 || std::fabs(res.dy - bv) > 1.5;
  res.atEdge = std::abs(bu - cu) >= radius_ || std::abs(bv - cv) >= radius_ || res.walked ||
               (opt_.maxDisplacementPx > 0 && std::hypot(res.dx, res.dy) > opt_.maxDisplacementPx);

  const int w = roi_.w, h = roi_.h;
  int sat = 0;
  const int sx = roi_.x + static_cast<int>(std::lround(res.dx)), sy = roi_.y + static_cast<int>(std::lround(res.dy));
  for (int y = 0; y < h; y += 2) {
    const uint8_t* row = &f.pixels[static_cast<size_t>(sy + y) * f.width + sx];
    for (int x = 0; x < w; x += 2) sat += (row[x] == 0 || row[x] == 255) ? 1 : 0;
  }
  res.saturated = sat * 4 > w * h / 20;
  px_ = res.dx;
  py_ = res.dy;
  return res;
}

}  // namespace vm
