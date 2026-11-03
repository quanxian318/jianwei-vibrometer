#include "gfx/draw.h"

#include <algorithm>
#include <cmath>

namespace vm {

namespace {
constexpr float kTwoPi = 6.28318530718f;

inline int coverage(float cov, int alpha) {
  if (cov <= 0.f) return 0;
  if (cov >= 1.f) return alpha;
  return static_cast<int>(cov * alpha + 0.5f);
}

void span(Canvas& c, int x0, int x1, int y, uint32_t color, int alpha) {
  if (y < 0 || y >= c.height()) return;
  x0 = std::max(0, x0);
  x1 = std::min(c.width(), x1);
  if (x0 >= x1) return;
  if (alpha >= 255) {
    uint32_t* row = c.data() + static_cast<size_t>(y) * c.width();
    std::fill(row + x0, row + x1, color);
    return;
  }
  for (int x = x0; x < x1; ++x) c.blend(x, y, color, alpha);
}

inline float frac(float v) { return v - std::floor(v); }
}  // namespace

uint32_t mixColor(uint32_t a, uint32_t b, int t) {
  t = std::max(0, std::min(255, t));
  const int it = 255 - t;
  const uint32_t r = (((a >> 16) & 0xFF) * it + ((b >> 16) & 0xFF) * t) / 255;
  const uint32_t g = (((a >> 8) & 0xFF) * it + ((b >> 8) & 0xFF) * t) / 255;
  const uint32_t bl = ((a & 0xFF) * it + (b & 0xFF) * t) / 255;
  return (r << 16) | (g << 8) | bl;
}

void fillRoundRect(Canvas& c, int x, int y, int w, int h, int r, uint32_t color, int alpha) {
  if (w <= 0 || h <= 0 || alpha <= 0) return;
  r = std::max(0, std::min(r, std::min(w, h) / 2));
  for (int j = 0; j < h; ++j) {
    const int py = y + j;
    if (py < 0 || py >= c.height()) continue;
    float dy = 0;
    if (j < r) {
      dy = r - (j + 0.5f);
    } else if (j >= h - r) {
      dy = (j + 0.5f) - (h - r);
    }
    if (dy <= 0) {
      span(c, x, x + w, py, color, alpha);
      continue;
    }
    for (int i = 0; i < r; ++i) {  // corner pixels
      const float dx = r - (i + 0.5f);
      const int a = coverage(r - std::sqrt(dx * dx + dy * dy) + 0.5f, alpha);
      if (a) {
        c.blend(x + i, py, color, a);
        c.blend(x + w - 1 - i, py, color, a);
      }
    }
    span(c, x + r, x + w - r, py, color, alpha);
  }
}

void strokeRoundRect(Canvas& c, int x, int y, int w, int h, int r, int width, uint32_t color, int alpha) {
  if (w <= 0 || h <= 0 || width <= 0 || alpha <= 0) return;
  r = std::max(0, std::min(r, std::min(w, h) / 2));
  const float hx = w * 0.5f, hy = h * 0.5f, cx = x + hx, cy = y + hy, hw = width * 0.5f;
  auto px = [&](int i, int py) {
    const float qx = std::fabs(x + i + 0.5f - cx) - hx + r, qy = std::fabs(py + 0.5f - cy) - hy + r;
    const float ox = std::max(qx, 0.f), oy = std::max(qy, 0.f);
    const float d = std::min(std::max(qx, qy), 0.f) + std::sqrt(ox * ox + oy * oy) - r;  // < 0 inside
    const int a = coverage(hw - std::fabs(d + hw) + 0.5f, alpha);
    if (a) c.blend(x + i, py, color, a);
  };
  const int band = width + 1;
  for (int j = 0; j < h; ++j) {
    const int py = y + j;
    if (py < 0 || py >= c.height()) continue;
    if (j < band + r || j >= h - band - r) {
      for (int i = 0; i < w; ++i) px(i, py);
    } else {
      for (int i = 0; i < band && i < w; ++i) {
        px(i, py);
        px(w - 1 - i, py);
      }
    }
  }
}

void fillCircle(Canvas& c, float cx, float cy, float r, uint32_t color, int alpha) {
  if (r <= 0 || alpha <= 0) return;
  const int x0 = static_cast<int>(std::floor(cx - r - 1)), x1 = static_cast<int>(std::ceil(cx + r + 1));
  const int y0 = static_cast<int>(std::floor(cy - r - 1)), y1 = static_cast<int>(std::ceil(cy + r + 1));
  const float inner = std::max(0.f, r - 1.f);
  for (int py = y0; py <= y1; ++py) {
    const float dy = py + 0.5f - cy;
    for (int px = x0; px <= x1; ++px) {
      const float dx = px + 0.5f - cx;
      const float d2 = dx * dx + dy * dy;
      if (d2 <= inner * inner) {
        c.blend(px, py, color, alpha);
        continue;
      }
      const int a = coverage(r - std::sqrt(d2) + 0.5f, alpha);
      if (a) c.blend(px, py, color, a);
    }
  }
}

void strokeCircle(Canvas& c, float cx, float cy, float r, float width, uint32_t color, int alpha) {
  const float hw = width * 0.5f, ro = r + hw + 1;
  const int x0 = static_cast<int>(std::floor(cx - ro)), x1 = static_cast<int>(std::ceil(cx + ro));
  const int y0 = static_cast<int>(std::floor(cy - ro)), y1 = static_cast<int>(std::ceil(cy + ro));
  for (int py = y0; py <= y1; ++py) {
    const float dy = py + 0.5f - cy;
    for (int px = x0; px <= x1; ++px) {
      const float dx = px + 0.5f - cx;
      const int a = coverage(hw - std::fabs(std::sqrt(dx * dx + dy * dy) - r) + 0.5f, alpha);
      if (a) c.blend(px, py, color, a);
    }
  }
}

void strokeArc(Canvas& c, float cx, float cy, float r, float width, float a0, float a1, uint32_t color, int alpha) {
  if (a1 < a0) std::swap(a0, a1);
  if (a1 - a0 <= 0) return;
  if (a1 - a0 >= kTwoPi - 1e-4f) {
    strokeCircle(c, cx, cy, r, width, color, alpha);
    return;
  }
  const float hw = width * 0.5f, ro = r + hw + 1, sweep = a1 - a0;
  float base = std::fmod(a0, kTwoPi);
  if (base < 0) base += kTwoPi;
  const int x0 = static_cast<int>(std::floor(cx - ro)), x1 = static_cast<int>(std::ceil(cx + ro));
  const int y0 = static_cast<int>(std::floor(cy - ro)), y1 = static_cast<int>(std::ceil(cy + ro));
  for (int py = y0; py <= y1; ++py) {
    const float dy = py + 0.5f - cy;
    for (int px = x0; px <= x1; ++px) {
      const float dx = px + 0.5f - cx;
      const int a = coverage(hw - std::fabs(std::sqrt(dx * dx + dy * dy) - r) + 0.5f, alpha);
      if (!a) continue;
      float th = std::atan2(dx, -dy);
      if (th < 0) th += kTwoPi;
      float rel = th - base;
      if (rel < 0) rel += kTwoPi;
      if (rel <= sweep) c.blend(px, py, color, a);
    }
  }
  // Round caps hide the hard angular ends.
  fillCircle(c, cx + r * std::sin(a0), cy - r * std::cos(a0), hw, color, alpha);
  fillCircle(c, cx + r * std::sin(a1), cy - r * std::cos(a1), hw, color, alpha);
}

void lineAA(Canvas& c, float x0, float y0, float x1, float y1, float width, uint32_t color, int alpha) {
  const float hw = width * 0.5f;
  const int bx0 = static_cast<int>(std::floor(std::min(x0, x1) - hw - 1));
  const int bx1 = static_cast<int>(std::ceil(std::max(x0, x1) + hw + 1));
  const int by0 = static_cast<int>(std::floor(std::min(y0, y1) - hw - 1));
  const int by1 = static_cast<int>(std::ceil(std::max(y0, y1) + hw + 1));
  const float vx = x1 - x0, vy = y1 - y0, len2 = vx * vx + vy * vy;
  for (int py = by0; py <= by1; ++py) {
    for (int px = bx0; px <= bx1; ++px) {
      const float qx = px + 0.5f - x0, qy = py + 0.5f - y0;
      float t = len2 > 0 ? (qx * vx + qy * vy) / len2 : 0.f;
      t = std::max(0.f, std::min(1.f, t));
      const float ex = qx - t * vx, ey = qy - t * vy;
      const int a = coverage(hw - std::sqrt(ex * ex + ey * ey) + 0.5f, alpha);
      if (a) c.blend(px, py, color, a);
    }
  }
}

void wuLine(Canvas& c, float x0, float y0, float x1, float y1, uint32_t color, int alpha) {
  const bool steep = std::fabs(y1 - y0) > std::fabs(x1 - x0);
  if (steep) {
    std::swap(x0, y0);
    std::swap(x1, y1);
  }
  if (x0 > x1) {
    std::swap(x0, x1);
    std::swap(y0, y1);
  }
  const float dx = x1 - x0, dy = y1 - y0;
  const float g = dx < 1e-6f ? 0.f : dy / dx;
  auto plot = [&](int x, int y, float cov) {
    const int a = coverage(cov, alpha);
    if (!a) return;
    if (steep) {
      c.blend(y, x, color, a);
    } else {
      c.blend(x, y, color, a);
    }
  };
  float xend = std::round(x0);
  float yend = y0 + g * (xend - x0);
  float xgap = 1 - frac(x0 + 0.5f);
  const int xp1 = static_cast<int>(xend);
  int yp = static_cast<int>(std::floor(yend));
  plot(xp1, yp, (1 - frac(yend)) * xgap);
  plot(xp1, yp + 1, frac(yend) * xgap);
  float intery = yend + g;
  xend = std::round(x1);
  yend = y1 + g * (xend - x1);
  xgap = frac(x1 + 0.5f);
  const int xp2 = static_cast<int>(xend);
  yp = static_cast<int>(std::floor(yend));
  plot(xp2, yp, (1 - frac(yend)) * xgap);
  plot(xp2, yp + 1, frac(yend) * xgap);
  for (int x = xp1 + 1; x < xp2; ++x) {
    const int iy = static_cast<int>(std::floor(intery));
    plot(x, iy, 1 - frac(intery));
    plot(x, iy + 1, frac(intery));
    intery += g;
  }
}

void fillPolygon(Canvas& c, const float* xy, int n, uint32_t color, int alpha) {
  if (n < 3 || alpha <= 0) return;
  float minX = xy[0], maxX = xy[0], minY = xy[1], maxY = xy[1];
  for (int i = 1; i < n; ++i) {
    minX = std::min(minX, xy[2 * i]);
    maxX = std::max(maxX, xy[2 * i]);
    minY = std::min(minY, xy[2 * i + 1]);
    maxY = std::max(maxY, xy[2 * i + 1]);
  }
  auto inside = [&](float px, float py) {
    bool in = false;
    for (int i = 0, j = n - 1; i < n; j = i++) {
      const float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
      if ((yi > py) != (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) in = !in;
    }
    return in;
  };
  for (int py = static_cast<int>(std::floor(minY)); py <= static_cast<int>(std::ceil(maxY)); ++py) {
    for (int px = static_cast<int>(std::floor(minX)); px <= static_cast<int>(std::ceil(maxX)); ++px) {
      int hits = 0;
      for (int sy = 0; sy < 4; ++sy) {
        for (int sx = 0; sx < 4; ++sx) hits += inside(px + (sx + 0.5f) * 0.25f, py + (sy + 0.5f) * 0.25f) ? 1 : 0;
      }
      if (hits) c.blend(px, py, color, alpha * hits / 16);
    }
  }
}

void fillGradientV(Canvas& c, int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  for (int j = 0; j < h; ++j) {
    const uint32_t col = mixColor(top, bottom, h > 1 ? j * 255 / (h - 1) : 0);
    span(c, x, x + w, y + j, col, 255);
  }
}

void dashRect(Canvas& c, int x, int y, int w, int h, uint32_t color, int dash, int gap, int width) {
  const int period = dash + gap;
  for (int i = 0; i < w; ++i) {
    if (i % period >= dash) continue;
    for (int t = 0; t < width; ++t) {
      c.set(x + i, y + t, color);
      c.set(x + i, y + h - 1 - t, color);
    }
  }
  for (int j = 0; j < h; ++j) {
    if (j % period >= dash) continue;
    for (int t = 0; t < width; ++t) {
      c.set(x + t, y + j, color);
      c.set(x + w - 1 - t, y + j, color);
    }
  }
}

void blitGray(Canvas& c, int x, int y, int w, int h, const uint8_t* img, int iw, int ih, double sx, double sy,
              double sw, double sh, const uint32_t* lut, int alpha, bool nearest) {
  if (!img || iw <= 0 || ih <= 0 || w <= 0 || h <= 0 || alpha <= 0) return;
  if (nearest) {
    for (int j = 0; j < h; ++j) {
      const int py = y + j;
      if (py < 0 || py >= c.height()) continue;
      const int iy = std::max(0, std::min(ih - 1, static_cast<int>(sy + (j + 0.5) * sh / h)));
      const uint8_t* row = img + static_cast<size_t>(iy) * iw;
      for (int i = 0; i < w; ++i) {
        const int px = x + i;
        if (px < 0 || px >= c.width()) continue;
        const int ix = std::max(0, std::min(iw - 1, static_cast<int>(sx + (i + 0.5) * sw / w)));
        const uint32_t col = lut ? lut[row[ix]] : (static_cast<uint32_t>(row[ix]) * 0x010101u);
        c.blend(px, py, col, alpha);
      }
    }
    return;
  }
  std::vector<int> xi(w), xf(w);
  for (int i = 0; i < w; ++i) {
    double fx = sx + (i + 0.5) * sw / w - 0.5;
    fx = std::max(0.0, std::min(static_cast<double>(iw - 1), fx));
    int ix = static_cast<int>(fx);
    if (ix > iw - 2) ix = std::max(0, iw - 2);
    xi[i] = ix;
    xf[i] = iw > 1 ? static_cast<int>((fx - ix) * 256) : 0;
  }
  const int x1 = iw > 1 ? 1 : 0;
  for (int j = 0; j < h; ++j) {
    const int py = y + j;
    if (py < 0 || py >= c.height()) continue;
    double fy = sy + (j + 0.5) * sh / h - 0.5;
    fy = std::max(0.0, std::min(static_cast<double>(ih - 1), fy));
    int iy = static_cast<int>(fy);
    if (iy > ih - 2) iy = std::max(0, ih - 2);
    const int wy = ih > 1 ? static_cast<int>((fy - iy) * 256) : 0;
    const uint8_t* r0 = img + static_cast<size_t>(iy) * iw;
    const uint8_t* r1 = img + static_cast<size_t>(std::min(iy + 1, ih - 1)) * iw;
    uint32_t* drow = c.data() + static_cast<size_t>(py) * c.width();
    for (int i = 0; i < w; ++i) {
      const int px = x + i;
      if (px < 0 || px >= c.width()) continue;
      const int a = xi[i], wx = xf[i];
      const int top = r0[a] * (256 - wx) + r0[a + x1] * wx;
      const int bot = r1[a] * (256 - wx) + r1[a + x1] * wx;
      const int v = (top * (256 - wy) + bot * wy + 32768) >> 16;
      const uint32_t col = lut ? lut[v] : (static_cast<uint32_t>(v) * 0x010101u);
      if (alpha >= 255) {
        drow[px] = col;
      } else {
        c.blend(px, py, col, alpha);
      }
    }
  }
}

void blitBgr(Canvas& c, int x, int y, int w, int h, const uint8_t* img, int iw, int ih, double sx, double sy,
             double sw, double sh) {
  if (!img || iw <= 0 || ih <= 0 || w <= 0 || h <= 0) return;
  const int cx0 = std::max(0, x), cx1 = std::min(c.width(), x + w);
  if (cx0 >= cx1) return;
  const int n = cx1 - cx0;
  if (sw >= w && sh >= h) {
    // A display-only 2x2 area sample avoids point-sampling colour speckle
    // and aliasing at the normal ~2:1 preview reduction. Raw measurement
    // pixels and their timestamps never pass through this branch.
    const bool area2 = sw >= 2.0 * w && sh >= 2.0 * h;
    std::vector<int> xo(n);
    for (int i = 0; i < n; ++i)
      xo[i] = 3 * std::max(0, std::min(iw - 1, static_cast<int>(sx + (cx0 - x + i + 0.5) * sw / w - (area2 ? 0.5 : 0.0))));
    for (int j = 0; j < h; ++j) {
      const int py = y + j;
      if (py < 0 || py >= c.height()) continue;
      const int iy = std::max(0, std::min(ih - 1, static_cast<int>(sy + (j + 0.5) * sh / h - (area2 ? 0.5 : 0.0))));
      const uint8_t* row = img + static_cast<size_t>(iy) * iw * 3;
      const uint8_t* row2 = img + static_cast<size_t>(std::min(iy + 1, ih - 1)) * iw * 3;
      uint32_t* d = c.data() + static_cast<size_t>(py) * c.width() + cx0;
      for (int i = 0; i < n; ++i) {
        const uint8_t* p = row + xo[i];
        if (area2) {
          const int nx = std::min(xo[i] + 3, 3 * (iw - 1));
          const uint8_t* q = row2 + xo[i];
          uint32_t color = 0;
          for (int k = 0; k < 3; ++k)
            color |= static_cast<uint32_t>((p[k] + row[nx + k] + q[k] + row2[nx + k] + 2) / 4) << (8 * k);
          d[i] = color;
        } else {
          d[i] = p[0] | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16);
        }
      }
    }
    return;
  }
  std::vector<int> xo(n), xf(n);
  for (int i = 0; i < n; ++i) {
    double fx = sx + (cx0 - x + i + 0.5) * sw / w - 0.5;
    fx = std::max(0.0, std::min(static_cast<double>(iw - 1), fx));
    int ix = static_cast<int>(fx);
    if (ix > iw - 2) ix = std::max(0, iw - 2);
    xo[i] = 3 * ix;
    xf[i] = iw > 1 ? static_cast<int>((fx - ix) * 256) : 0;
  }
  const int next = iw > 1 ? 3 : 0;
  for (int j = 0; j < h; ++j) {
    const int py = y + j;
    if (py < 0 || py >= c.height()) continue;
    double fy = sy + (j + 0.5) * sh / h - 0.5;
    fy = std::max(0.0, std::min(static_cast<double>(ih - 1), fy));
    int iy = static_cast<int>(fy);
    if (iy > ih - 2) iy = std::max(0, ih - 2);
    const int wy = ih > 1 ? static_cast<int>((fy - iy) * 256) : 0;
    const uint8_t* r0 = img + static_cast<size_t>(iy) * iw * 3;
    const uint8_t* r1 = img + static_cast<size_t>(std::min(iy + 1, ih - 1)) * iw * 3;
    uint32_t* d = c.data() + static_cast<size_t>(py) * c.width() + cx0;
    for (int i = 0; i < n; ++i) {
      const uint8_t* a = r0 + xo[i];
      const uint8_t* b = r1 + xo[i];
      const int wx = xf[i];
      uint32_t col = 0;
      for (int k = 0; k < 3; ++k) {
        const int top = a[k] * (256 - wx) + a[k + next] * wx;
        const int bot = b[k] * (256 - wx) + b[k + next] * wx;
        col |= static_cast<uint32_t>((top * (256 - wy) + bot * wy + 32768) >> 16) << (8 * k);
      }
      d[i] = col;
    }
  }
}

namespace {
struct HeatLut {
  uint32_t v[256];
  HeatLut() {
    // Google "Turbo" polynomial approximation.
    for (int i = 0; i < 256; ++i) {
      const double t = i / 255.0;
      auto ch = [](double x) { return static_cast<uint32_t>(std::max(0.0, std::min(1.0, x)) * 255.0 + 0.5); };
      const double r = 0.13572138 + t * (4.61539260 + t * (-42.66032258 + t * (132.13108234 + t * (-152.94239396 + t * 59.28637943))));
      const double g = 0.09140261 + t * (2.19418839 + t * (4.84296658 + t * (-14.18503333 + t * (4.27729857 + t * 2.82956604))));
      const double b = 0.10667330 + t * (12.64194608 + t * (-60.58204836 + t * (110.36276771 + t * (-89.90310912 + t * 27.34824973))));
      v[i] = (ch(r) << 16) | (ch(g) << 8) | ch(b);
    }
  }
};
}  // namespace

const uint32_t* heatLut() {
  static const HeatLut lut;  // thread-safe initialisation
  return lut.v;
}

}  // namespace vm
