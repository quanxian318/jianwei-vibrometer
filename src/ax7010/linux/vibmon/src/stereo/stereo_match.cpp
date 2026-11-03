#include "stereo/stereo_match.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

namespace vm {

namespace {

inline int popcount64(uint64_t v) { return __builtin_popcountll(v); }

}  // namespace

std::vector<uint64_t> censusTransform(const StereoImage& im, int mode, int& bits, int& border) {
  std::vector<uint64_t> c(static_cast<size_t>(im.w) * im.h, 0);
  int rx = 2, ry = 2;
  if (mode == 7) rx = ry = 3;
  if (mode == 9) {
    rx = 4;
    ry = 3;
  }
  border = std::max(rx, ry);
  bits = mode == 5 ? 24 : (mode == 7 ? 48 : 31);
  for (int y = ry; y < im.h - ry; ++y)
    for (int x = rx; x < im.w - rx; ++x) {
      uint64_t v = 0;
      if (mode == 9) {
        // Center-symmetric: compare each pixel of the upper half with its mirror.
        for (int dy = -ry; dy <= 0; ++dy)
          for (int dx = -rx; dx <= rx; ++dx) {
            if (dy == 0 && dx >= 0) break;
            v = (v << 1) | (im.at(x + dx, y + dy) > im.at(x - dx, y - dy) ? 1u : 0u);
          }
      } else {
        const int c0 = im.at(x, y);
        for (int dy = -ry; dy <= ry; ++dy)
          for (int dx = -rx; dx <= rx; ++dx)
            if (dx || dy) v = (v << 1) | (im.at(x + dx, y + dy) < c0 ? 1u : 0u);
      }
      c[static_cast<size_t>(y) * im.w + x] = v;
    }
  return c;
}

namespace {

// Sub-pixel parabola through (d-1, d, d+1), result in Q4.
int16_t parabolaQ4(int d, int cm, int c0, int cp) {
  const int den = cm - 2 * c0 + cp;
  int off = 0;
  if (den > 0) {
    // 16 * (cm - cp) / (2 den), rounded, limited to +-8 (half a pixel).
    const int num = 8 * (cm - cp);
    off = (num >= 0 ? num + den / 2 : num - den / 2) / den;
    off = std::max(-8, std::min(8, off));
  }
  return static_cast<int16_t>(16 * d + off);
}

// Equiangular (V-shaped) fit, suits the near-linear cost slopes of census/SGM.
int16_t equiangularQ4(int d, int cm, int c0, int cp) {
  const int den = 2 * (std::max(cm, cp) - c0);
  int off = 0;
  if (den > 0) {
    const int num = 16 * (cm - cp);
    off = (num >= 0 ? num + den / 2 : num - den / 2) / den;
    off = std::max(-8, std::min(8, off));
  }
  return static_cast<int16_t>(16 * d + off);
}

void medianFilter(DisparityMap& m) {
  std::vector<int16_t> out = m.q4;
  int16_t buf[9];
  for (int y = 1; y < m.h - 1; ++y)
    for (int x = 1; x < m.w - 1; ++x) {
      const size_t i = static_cast<size_t>(y) * m.w + x;
      if (m.q4[i] < 0) continue;
      int n = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int16_t v = m.q4[i + dy * m.w + dx];
          if (v >= 0) buf[n++] = v;
        }
      std::nth_element(buf, buf + n / 2, buf + n);
      out[i] = buf[n / 2];
    }
  m.q4.swap(out);
}

void speckleFilter(DisparityMap& m, int maxSize, int maxDiff) {
  if (maxSize <= 0) return;
  const size_t n = m.q4.size();
  std::vector<uint8_t> seen(n, 0);
  std::vector<int> queue;
  queue.reserve(n);
  for (size_t s = 0; s < n; ++s) {
    if (seen[s] || m.q4[s] < 0) continue;
    queue.clear();
    queue.push_back(static_cast<int>(s));
    seen[s] = 1;
    for (size_t head = 0; head < queue.size(); ++head) {
      const int p = queue[head], x = p % m.w, y = p / m.w;
      const int nb[4] = {x > 0 ? p - 1 : -1, x < m.w - 1 ? p + 1 : -1, y > 0 ? p - m.w : -1, y < m.h - 1 ? p + m.w : -1};
      for (int q : nb) {
        if (q < 0 || seen[q] || m.q4[q] < 0 || std::abs(m.q4[q] - m.q4[p]) > maxDiff) continue;
        seen[q] = 1;
        queue.push_back(q);
      }
    }
    if (static_cast<int>(queue.size()) < maxSize)
      for (int p : queue) m.q4[p] = kInvalidDisparity;
  }
}

}  // namespace

int refineDisparityLk(const StereoImage& L, const StereoImage& R, int x, int y, double d0, int r, double& d) {
  if (x - r - 1 < 0 || y - r < 0 || x + r + 1 >= L.w || y + r >= L.h) return 1;
  const int n = (2 * r + 1) * (2 * r + 1);
  double t[81], g[81], w[81];
  if (n > 81) return 1;
  double tm = 0;
  int k = 0;
  for (int dy = -r; dy <= r; ++dy)
    for (int dx = -r; dx <= r; ++dx, ++k) {
      t[k] = L.at(x + dx, y + dy);
      g[k] = 0.5 * (L.at(x + dx + 1, y + dy) - L.at(x + dx - 1, y + dy));
      tm += t[k];
    }
  tm /= n;
  double tn = 0, hg = 0, gt = 0, gm = 0;
  for (int i = 0; i < n; ++i) {
    t[i] -= tm;
    tn += t[i] * t[i];
    gm += g[i];
  }
  gm /= n;
  for (int i = 0; i < n; ++i) {
    g[i] -= gm;  // project out the offset; gain is handled by normalising the window
    gt += g[i] * t[i];
  }
  for (int i = 0; i < n; ++i) {
    g[i] -= gt / tn * t[i];
    hg += g[i] * g[i];
  }
  if (tn < n * 4.0 || hg < n * 1.0) return 1;
  d = d0;
  const double tNorm = std::sqrt(tn);
  for (int it = 0; it < 5; ++it) {
    // Right window at x - d: linear interpolation along x.
    const double xs = x - d;
    const int xi = static_cast<int>(std::floor(xs));
    const double a = xs - xi;
    if (xi - r < 0 || xi + r + 1 >= R.w) return 1;
    double wm = 0;
    k = 0;
    for (int dy = -r; dy <= r; ++dy)
      for (int dx = -r; dx <= r; ++dx, ++k) {
        w[k] = (1 - a) * R.at(xi + dx, y + dy) + a * R.at(xi + dx + 1, y + dy);
        wm += w[k];
      }
    wm /= n;
    double wn = 0;
    for (int i = 0; i < n; ++i) {
      w[i] -= wm;
      wn += w[i] * w[i];
    }
    if (wn <= 0) return 1;
    const double gain = tNorm / std::sqrt(wn);
    double b = 0;
    for (int i = 0; i < n; ++i) b += g[i] * (gain * w[i] - t[i]);
    const double step = b / hg;  // R(x - d) ~ L(x) + (d* - d) gL
    d += step;
    if (std::fabs(d - d0) > 1.0) return 2;
    if (std::fabs(step) < 0.01) return 0;
  }
  return 1;
}

SgmParams plCoreParams(const SgmParams& post) {
  SgmParams p = post;
  p.maxDisp = 64;
  p.census = 9;
  p.paths = 2;
  p.p1 = 7;
  p.p2 = 86;
  p.adaptiveP2 = true;
  p.uniquenessPct = 5;
  p.subpixel = 1;
  p.lrCheck = true;
  p.lrTolerance = 1;
  return p;
}

DisparityMap plCoreModel(const StereoImage& left, const StereoImage& right) {
  const SgmParams p = plCoreParams();
  int bits = 0, border = 0;
  const auto cl = censusTransform(left, p.census, bits, border);
  const auto cr = censusTransform(right, p.census, bits, border);
  return sgmRaw(cl, cr, left, bits, border, p);
}

int DisparityMap::valid() const {
  int n = 0;
  for (size_t i = 0; i < q4.size(); ++i) n += measured(i);
  return n;
}

StereoImage flipStereoRows(const StereoImage& image) {
  StereoImage out = image;
  for (int y = 0; y < image.h; ++y)
    std::copy_n(image.px.begin() + static_cast<size_t>(image.h - 1 - y) * image.w, image.w,
                out.px.begin() + static_cast<size_t>(y) * image.w);
  return out;
}

DisparityMap fuseVerticalPasses(const DisparityMap& a, const DisparityMap& flipped, int toleranceQ4) {
  DisparityMap out;
  out.w = a.w;
  out.h = a.h;
  out.q4.assign(a.q4.size(), kInvalidDisparity);
  if (a.w <= 0 || a.h <= 0 || a.q4.size() != static_cast<size_t>(a.w) * a.h ||
      flipped.w != a.w || flipped.h != a.h || flipped.q4.size() != a.q4.size()) return out;
  auto at = [&](const DisparityMap& m, int x, int y, bool flip) {
    if (x < 0 || y < 0 || x >= a.w || y >= a.h) return kInvalidDisparity;
    const size_t i = static_cast<size_t>(flip ? a.h - 1 - y : y) * a.w + x;
    return m.measured(i) ? m.q4[i] : kInvalidDisparity;
  };
  for (int y = 0; y < a.h; ++y)
    for (int x = 0; x < a.w; ++x) {
      const int qa = at(a, x, y, false), qb = at(flipped, x, y, true);
      int16_t& q = out.q4[static_cast<size_t>(y) * a.w + x];
      if (qa >= 0 && qb >= 0) {
        if (std::abs(qa - qb) <= toleranceQ4) q = static_cast<int16_t>((qa + qb + 1) / 2);
      } else if (qa >= 0 || qb >= 0) {
        const int candidate = qa >= 0 ? qa : qb;
        const DisparityMap& other = qa >= 0 ? flipped : a;
        int support = 0;
        for (int dy = -1; dy <= 1; ++dy)
          for (int dx = -1; dx <= 1; ++dx) {
            const int v = at(other, x + dx, y + dy, qa >= 0);
            support += v >= 0 && std::abs(candidate - v) <= toleranceQ4;
          }
        // A one-sided occlusion must have nearby independent support; a
        // disagreement is not filled or counted as a measured depth.
        if (support >= 3) q = static_cast<int16_t>(candidate);
      }
    }
  return out;
}

StereoImage shiftStereoRight(const StereoImage& image, int offset) {
  StereoImage out; out.w=image.w; out.h=image.h; out.px.assign(image.px.size(),0);
  if (offset<0 || offset>=image.w) return out;
  for (int y=0;y<image.h;++y)
    std::copy_n(image.px.data()+static_cast<size_t>(y)*image.w,image.w-offset,
                out.px.data()+static_cast<size_t>(y)*image.w+offset);
  return out;
}

DisparityMap offsetDisparities(DisparityMap map, int offset) {
  for (int y=0;y<map.h;++y) for (int x=0;x<map.w;++x) {
    auto& q=map.q4[static_cast<size_t>(y)*map.w+x];
    if(q<0) continue;
    q=static_cast<int16_t>(q+offset*16);
    if(x-((q+8)>>4)<4) q=kInvalidDisparity; // padded part is not a real right-image sample
  }
  return map;
}

DisparityMap mergeDisparityRanges(const StereoImage& left, const StereoImage& right,
                                  const DisparityMap& low, const DisparityMap& high) {
  DisparityMap out=low;
  // Each band has already passed the PL's uniqueness/LR checks and the
  // vertical-pass check. Compare independent 11x11 image patches across bands:
  // a local Census descriptor alone cannot disambiguate repeated gradients.
  auto correlation=[&](int x,int y,int q) {
    const int xr=x-((q+8)>>4),radius=5,n=121;
    if(x<radius || xr<radius || x>=left.w-radius || y<radius || y>=left.h-radius) return -1.0;
    int a=0,b=0,aa=0,bb=0,ab=0;
    for(int dy=-radius;dy<=radius;++dy) for(int dx=-radius;dx<=radius;++dx) {
      const int u=left.at(x+dx,y+dy),v=right.at(xr+dx,y+dy);
      a+=u; b+=v; aa+=u*u; bb+=v*v; ab+=u*v;
    }
    const int va=aa*n-a*a,vb=bb*n-b*b;
    if(va<4*n*n || vb<4*n*n) return -1.0;
    return (ab*double(n)-double(a)*b)/std::sqrt(double(va)*vb);
  };
  for(int y=0;y<low.h;++y) for(int x=0;x<low.w;++x) {
    const size_t i=static_cast<size_t>(y)*low.w+x;
    if(!high.measured(i)) continue;
    const double b=correlation(x,y,high.q4[i]);
    if(b<.80) continue;
    if(!low.measured(i)) {out.q4[i]=high.q4[i];continue;}
    const double a=correlation(x,y,low.q4[i]);
    if(b>a+.03) out.q4[i]=high.q4[i];
    else if(a<b+.03) out.q4[i]=kInvalidDisparity;
  }
  return out;
}

namespace {
void fillHoles(DisparityMap& m, int maxRun) {
  m.filled.assign(m.q4.size(), 0);
  for (int y = 0; y < m.h; ++y) {
    int16_t* row = &m.q4[static_cast<size_t>(y) * m.w];
    uint8_t* f = &m.filled[static_cast<size_t>(y) * m.w];
    int x = 0;
    while (x < m.w) {
      if (row[x] >= 0) {
        ++x;
        continue;
      }
      const int a = x;
      while (x < m.w && row[x] < 0) ++x;
      const int b = x - 1;  // invalid run [a, b]
      if (a == 0 || x == m.w || b - a + 1 > maxRun) continue;  // open at the border or too long: no data
      const int l = row[a - 1], r = row[x];
      for (int k = a; k <= b; ++k) {
        if (std::abs(l - r) <= 16) row[k] = static_cast<int16_t>(l + (r - l) * (k - a + 1) / (b - a + 2));
        else row[k] = static_cast<int16_t>(std::min(l, r));
        f[k] = 1;
      }
    }
  }
  // Columns: runs still empty between two measured pixels that agree within
  // 1 px (vertically there is no occlusion side to prefer).
  for (int x = 0; x < m.w; ++x) {
    int y = 0;
    while (y < m.h) {
      if (m.q4[static_cast<size_t>(y) * m.w + x] >= 0) {
        ++y;
        continue;
      }
      const int a = y;
      while (y < m.h && m.q4[static_cast<size_t>(y) * m.w + x] < 0) ++y;
      const int b = y - 1;
      if (a == 0 || y == m.h || b - a + 1 > maxRun) continue;
      const size_t ia = static_cast<size_t>(a - 1) * m.w + x, ib = static_cast<size_t>(y) * m.w + x;
      const int t = m.q4[ia], u = m.q4[ib];
      if (m.filled[ia] || m.filled[ib] || std::abs(t - u) > 16) continue;
      for (int k = a; k <= b; ++k) {
        const size_t j = static_cast<size_t>(k) * m.w + x;
        m.q4[j] = static_cast<int16_t>(t + (u - t) * (k - a + 1) / (b - a + 2));
        m.filled[j] = 1;
      }
    }
  }
}
}  // namespace

DisparityMap matchSgm(const StereoImage& L, const StereoImage& R, const SgmParams& p) {
  if (L.w != R.w || L.h != R.h || L.w < 16 || L.h < 16 || p.maxDisp < 2) {
    DisparityMap out;
    out.w = L.w;
    out.h = L.h;
    out.q4.assign(static_cast<size_t>(L.w) * L.h, kInvalidDisparity);
    return out;
  }
  int bits = 0, border = 0;
  const std::vector<uint64_t> cl = censusTransform(L, p.census, bits, border);
  const std::vector<uint64_t> cr = censusTransform(R, p.census, bits, border);
  DisparityMap out = sgmRaw(cl, cr, L, bits, border, p);
  stereoPostProcess(L, R, border, p, out);
  return out;
}

DisparityMap sgmRaw(const std::vector<uint64_t>& cl, const std::vector<uint64_t>& cr, const StereoImage& L, int bits,
                    int border, const SgmParams& p) {
  DisparityMap out;
  out.w = L.w;
  out.h = L.h;
  out.q4.assign(static_cast<size_t>(L.w) * L.h, kInvalidDisparity);
  const int W = L.w, H = L.h, D = p.maxDisp;

  // Matching cost volume (Hamming distance), "outside" disparities get the maximum.
  std::vector<uint8_t> cost(static_cast<size_t>(W) * H * D);
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      uint8_t* c = &cost[(static_cast<size_t>(y) * W + x) * D];
      const uint64_t a = cl[static_cast<size_t>(y) * W + x];
      for (int d = 0; d < D; ++d)
        c[d] = static_cast<uint8_t>(x - d >= 0 ? popcount64(a ^ cr[static_cast<size_t>(y) * W + x - d]) : bits);
    }

  // Aggregation: S = sum over paths of L_r.
  std::vector<uint16_t> sum(cost.size(), 0);
  if (p.paths == 0) {
    for (size_t i = 0; i < cost.size(); ++i) sum[i] = cost[i];
  } else {
    struct Dir { int dx, dy; };
    std::vector<Dir> dirs = {{1, 0}, {0, 1}};
    if (p.paths >= 4) dirs = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}};
    if (p.paths >= 8) dirs = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    std::vector<uint16_t> lr(cost.size());
    std::vector<uint16_t> minLr(static_cast<size_t>(W) * H);
    for (const Dir& r : dirs) {
      const bool forward = r.dy > 0 || (r.dy == 0 && r.dx > 0);
      for (int yi = 0; yi < H; ++yi) {
        const int y = forward ? yi : H - 1 - yi;
        for (int xi = 0; xi < W; ++xi) {
          // Horizontal paths need the neighbour on the same row first.
          const int x = (r.dx > 0 || (r.dx == 0 && forward)) ? xi : W - 1 - xi;
          const size_t i = static_cast<size_t>(y) * W + x;
          const uint8_t* c = &cost[i * D];
          uint16_t* l = &lr[i * D];
          const int px = x - r.dx, py = y - r.dy;
          uint16_t mn = std::numeric_limits<uint16_t>::max();
          if (px < 0 || py < 0 || px >= W || py >= H) {
            for (int d = 0; d < D; ++d) {
              l[d] = c[d];
              mn = std::min(mn, l[d]);
            }
          } else {
            const size_t j = static_cast<size_t>(py) * W + px;
            const uint16_t* lp = &lr[j * D];
            const int mp = minLr[j];
            int p2 = p.p2;
            if (p.adaptiveP2) p2 = std::max(p.p1 + 1, p.p2 * 16 / (16 + std::abs(L.px[i] - L.px[j])));
            for (int d = 0; d < D; ++d) {
              int v = std::min<int>(lp[d], mp + p2);
              if (d > 0) v = std::min<int>(v, lp[d - 1] + p.p1);
              if (d < D - 1) v = std::min<int>(v, lp[d + 1] + p.p1);
              l[d] = static_cast<uint16_t>(c[d] + v - mp);
              mn = std::min(mn, l[d]);
            }
          }
          minLr[i] = mn;
          uint16_t* s = &sum[i * D];
          for (int d = 0; d < D; ++d) s[d] = static_cast<uint16_t>(s[d] + l[d]);
        }
      }
    }
  }

  // Winner-take-all, uniqueness and sub-pixel.
  std::vector<int16_t> dl(static_cast<size_t>(W) * H, kInvalidDisparity);
  std::vector<int> dr(static_cast<size_t>(W) * H, -1);
  for (int y = border; y < H - border; ++y) {
    // Right-image disparity from the same volume: dR(xr) = argmin_d S(xr + d, d).
    std::vector<int> bestR(W, std::numeric_limits<int>::max());
    for (int x = border; x < W - border; ++x) {
      const uint16_t* s = &sum[(static_cast<size_t>(y) * W + x) * D];
      const int dmax = std::min(D - 1, x - border);
      for (int d = 0; d <= dmax; ++d)
        if (s[d] < bestR[x - d]) {
          bestR[x - d] = s[d];
          dr[static_cast<size_t>(y) * W + x - d] = d;
        }
      int b = 0;
      for (int d = 1; d <= dmax; ++d)
        if (s[d] < s[b]) b = d;
      int second = std::numeric_limits<int>::max();
      for (int d = 0; d <= dmax; ++d)
        if (std::abs(d - b) > 1) second = std::min<int>(second, s[d]);
      // Needs a competitor at all (left border) and a clear margin over it.
      if (second == std::numeric_limits<int>::max() || s[b] * 100 >= second * (100 - p.uniquenessPct)) continue;
      int16_t q = static_cast<int16_t>(16 * b);
      if (b > 0 && b < dmax)
        q = p.subpixel == 0 ? parabolaQ4(b, s[b - 1], s[b], s[b + 1]) : equiangularQ4(b, s[b - 1], s[b], s[b + 1]);
      dl[static_cast<size_t>(y) * W + x] = q;
    }
  }
  for (int y = border; y < H - border; ++y)
    for (int x = border; x < W - border; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      if (dl[i] < 0) continue;
      if (p.lrCheck) {
        const int d = (dl[i] + 8) >> 4, xr = x - d;
        if (xr < 0 || dr[static_cast<size_t>(y) * W + xr] < 0 ||
            std::abs(dr[static_cast<size_t>(y) * W + xr] - d) > p.lrTolerance)
          continue;
      }
      out.q4[i] = dl[i];
    }
  return out;
}

void stereoPostProcess(const StereoImage& L, const StereoImage& R, int border, const SgmParams& p, DisparityMap& out) {
  const int W = L.w, H = L.h;
  if (p.minContrast > 0) {
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x) {
        int16_t& q = out.q4[static_cast<size_t>(y) * W + x];
        if (q < 0) continue;
        int lo = 255, hi = 0;
        for (int dy = -2; dy <= 2; ++dy)
          for (int dx = -2; dx <= 2; ++dx) {
            const int v = L.at(std::max(0, std::min(W - 1, x + dx)), std::max(0, std::min(H - 1, y + dy)));
            lo = std::min(lo, v);
            hi = std::max(hi, v);
          }
        if (hi - lo < p.minContrast) q = kInvalidDisparity;
      }
  }
  if (p.refineLk) {
    for (int y = border; y < H - border; ++y)
      for (int x = border; x < W - border; ++x) {
        int16_t& q = out.q4[static_cast<size_t>(y) * W + x];
        if (q < 0) continue;
        double d;
        const int st = refineDisparityLk(L, R, x, y, q / 16.0, p.refineRadius, d);
        if (st == 0) q = static_cast<int16_t>(std::lround(d * 16));
        else if (p.refineReject >= st) q = kInvalidDisparity;
      }
  }
  if (p.median) medianFilter(out);
  speckleFilter(out, p.speckleSize, p.speckleDiffQ4);
  if (p.fillHoles) fillHoles(out, p.fillMaxRun);
}

DisparityMap matchBmBaseline(const StereoImage& L, const StereoImage& R, const BmParams& p) {
  DisparityMap out;
  out.w = L.w;
  out.h = L.h;
  out.q4.assign(static_cast<size_t>(L.w) * L.h, kInvalidDisparity);
  if (L.w != R.w || L.h != R.h || L.w < 16 || L.h < 16) return out;
  const int W = L.w, H = L.h, D = p.maxDisp, r = p.window / 2;
  auto sobel = [&](const StereoImage& im) {
    std::vector<int8_t> g(static_cast<size_t>(W) * H, 0);
    for (int y = 1; y < H - 1; ++y)
      for (int x = 1; x < W - 1; ++x) {
        int v = im.at(x + 1, y - 1) - im.at(x - 1, y - 1) + 2 * (im.at(x + 1, y) - im.at(x - 1, y)) +
                im.at(x + 1, y + 1) - im.at(x - 1, y + 1);
        g[static_cast<size_t>(y) * W + x] = static_cast<int8_t>(std::max(-32, std::min(31, v)));
      }
    return g;
  };
  const std::vector<int8_t> gl = sobel(L), gr = sobel(R);
  // Box SAD per disparity through column sums.
  std::vector<int> sad(static_cast<size_t>(W) * H * D, std::numeric_limits<int>::max());
  std::vector<int> ad(static_cast<size_t>(W) * H);
  for (int d = 0; d < D; ++d) {
    for (int y = 0; y < H; ++y)
      for (int x = 0; x < W; ++x)
        ad[static_cast<size_t>(y) * W + x] =
            x - d >= 0 ? std::abs(gl[static_cast<size_t>(y) * W + x] - gr[static_cast<size_t>(y) * W + x - d]) : 64;
    for (int y = r; y < H - r; ++y)
      for (int x = r; x < W - r; ++x) {
        if (x - d - r < 0) continue;
        int s = 0;
        for (int dy = -r; dy <= r; ++dy) {
          const int* row = &ad[static_cast<size_t>(y + dy) * W + x - r];
          for (int k = 0; k < 2 * r + 1; ++k) s += row[k];
        }
        sad[(static_cast<size_t>(y) * W + x) * D + d] = s;
      }
  }
  DisparityMap raw = out;
  for (int y = r + 1; y < H - r - 1; ++y)
    for (int x = r + 1; x < W - r - 1; ++x) {
      const int* s = &sad[(static_cast<size_t>(y) * W + x) * D];
      int b = -1;
      for (int d = 0; d < D; ++d)
        if (s[d] != std::numeric_limits<int>::max() && (b < 0 || s[d] < s[b])) b = d;
      if (b < 0) continue;
      int second = std::numeric_limits<int>::max();
      for (int d = 0; d < D; ++d)
        if (std::abs(d - b) > 1 && s[d] < second) second = s[d];
      if (second == std::numeric_limits<int>::max() || second == 0) continue;  // uniform: rejected
      if (static_cast<int64_t>(s[b]) * 1024 / second > p.uniqQ10) continue;
      int off = 0;
      if (b > 0 && b < D - 1 && s[b - 1] != std::numeric_limits<int>::max() && s[b + 1] != std::numeric_limits<int>::max()) {
        // Equiangular fit as bm_calc_frac: (L - R) / (2 (max(L, R) - C)).
        const int lc = s[b - 1] - s[b], rc = s[b + 1] - s[b];
        const int den = 2 * (s[b - 1] < s[b + 1] ? rc : lc);
        if (den > 0) off = std::max(-8, std::min(8, 16 * (s[b - 1] - s[b + 1]) / den));
      }
      raw.q4[static_cast<size_t>(y) * W + x] = static_cast<int16_t>(16 * b + off);
    }
  if (!p.spatialSupport) return raw;
  for (int y = 1; y < H - 1; ++y)
    for (int x = 1; x < W - 1; ++x) {
      const size_t i = static_cast<size_t>(y) * W + x;
      const int v = raw.q4[i];
      if (v < 0) continue;
      int agree = 0;
      for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx) {
          const int n = raw.q4[i + dy * W + dx];
          agree += n >= 0 && std::abs(n - v) <= 32;
        }
      if (agree >= 4) out.q4[i] = static_cast<int16_t>(v);
    }
  return out;
}

}  // namespace vm
