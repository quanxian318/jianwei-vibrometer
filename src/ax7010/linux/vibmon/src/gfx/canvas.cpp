#include "gfx/canvas.h"

#include <algorithm>
#include <cstdlib>

namespace vm {

void Canvas::resize(int w, int h, uint32_t color) {
  w_ = w;
  h_ = h;
  px_.assign(static_cast<size_t>(w) * h, color);
}

void Canvas::fill(uint32_t c) { std::fill(px_.begin(), px_.end(), c); }

void Canvas::blend(int x, int y, uint32_t c, int alpha) {
  if (x < 0 || y < 0 || x >= w_ || y >= h_ || alpha <= 0) return;
  uint32_t& d = px_[static_cast<size_t>(y) * w_ + x];
  if (alpha >= 255) {
    d = c;
    return;
  }
  const int ia = 255 - alpha;
  const uint32_t r = (((c >> 16) & 0xFF) * alpha + ((d >> 16) & 0xFF) * ia) / 255;
  const uint32_t g = (((c >> 8) & 0xFF) * alpha + ((d >> 8) & 0xFF) * ia) / 255;
  const uint32_t b = ((c & 0xFF) * alpha + (d & 0xFF) * ia) / 255;
  d = (r << 16) | (g << 8) | b;
}

void Canvas::fillRect(int x, int y, int w, int h, uint32_t c) {
  const int x0 = std::max(0, x), y0 = std::max(0, y);
  const int x1 = std::min(w_, x + w), y1 = std::min(h_, y + h);
  for (int yy = y0; yy < y1; ++yy) {
    std::fill(px_.begin() + static_cast<size_t>(yy) * w_ + x0, px_.begin() + static_cast<size_t>(yy) * w_ + x1, c);
  }
}

void Canvas::rect(int x, int y, int w, int h, uint32_t c, int t) {
  fillRect(x, y, w, t, c);
  fillRect(x, y + h - t, w, t, c);
  fillRect(x, y, t, h, c);
  fillRect(x + w - t, y, t, h, c);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint32_t c) {
  const int dx = std::abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    set(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

void Canvas::polyline(const std::vector<int>& xs, const std::vector<int>& ys, uint32_t c) {
  for (size_t i = 1; i < xs.size() && i < ys.size(); ++i) line(xs[i - 1], ys[i - 1], xs[i], ys[i], c);
}

void Canvas::drawGray(int x, int y, int w, int h, const uint8_t* gray, int gw, int gh) {
  if (!gray || gw <= 0 || gh <= 0 || w <= 0 || h <= 0) return;
  for (int yy = 0; yy < h; ++yy) {
    const int dy = y + yy;
    if (dy < 0 || dy >= h_) continue;
    const uint8_t* srow = gray + static_cast<size_t>(yy * gh / h) * gw;
    uint32_t* drow = &px_[static_cast<size_t>(dy) * w_];
    for (int xx = 0; xx < w; ++xx) {
      const int dx = x + xx;
      if (dx < 0 || dx >= w_) continue;
      const uint32_t v = srow[xx * gw / w];
      drow[dx] = (v << 16) | (v << 8) | v;
    }
  }
}

}  // namespace vm
