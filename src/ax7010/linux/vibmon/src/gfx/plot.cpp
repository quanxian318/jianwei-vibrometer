#include "gfx/plot.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vm {

void drawLinePlot(Canvas& c, int x, int y, int w, int h, const std::vector<double>& xs, const std::vector<double>& ys,
                  const std::string& title, const std::string& unit, const PlotStyle& st) {
  const FontFace& f = fontFace(st.fontSize);
  c.fillRect(x, y, w, h, st.background);
  const int top = y + f.lineHeight + 2;
  const int ph = h - (top - y) - 2;
  c.rect(x, top, w, ph, st.frame);
  drawText(c, f, x + 2, y, title, st.text);
  const size_t n = std::min(xs.size(), ys.size());
  if (n < 2 || ph < 8) return;
  double y0 = ys[0], y1 = ys[0];
  for (size_t i = 0; i < n; ++i) {
    y0 = std::min(y0, ys[i]);
    y1 = std::max(y1, ys[i]);
  }
  if (st.symmetric) {
    const double m = std::max(std::fabs(y0), std::fabs(y1));
    y0 = -m;
    y1 = m;
  }
  if (y1 - y0 < 1e-12) {
    y0 -= 1;
    y1 += 1;
  }
  const double x0 = xs[0], x1 = xs[n - 1] > xs[0] ? xs[n - 1] : xs[0] + 1;
  char label[48];
  snprintf(label, sizeof(label), "%.3g..%.3g %s", y0, y1, unit.c_str());
  const int lw = textWidth(f, label);
  drawText(c, f, x + w - lw - 2, y, label, st.text);
  if (y0 < 0 && y1 > 0) {
    const int zy = top + ph - 1 - static_cast<int>((0 - y0) / (y1 - y0) * (ph - 3)) - 1;
    for (int xx = x + 1; xx < x + w - 1; xx += 4) c.set(xx, zy, st.grid);
  }
  // Decimate to one min/max pair per pixel column so peaks are never lost.
  int prevX = -1, prevY = 0;
  const int inner = w - 2;
  std::vector<double> colMin(inner, 1e300), colMax(inner, -1e300);
  for (size_t i = 0; i < n; ++i) {
    int col = static_cast<int>((xs[i] - x0) / (x1 - x0) * (inner - 1));
    col = std::max(0, std::min(inner - 1, col));
    colMin[col] = std::min(colMin[col], ys[i]);
    colMax[col] = std::max(colMax[col], ys[i]);
  }
  auto py = [&](double v) { return top + ph - 2 - static_cast<int>((v - y0) / (y1 - y0) * (ph - 3)); };
  for (int col = 0; col < inner; ++col) {
    if (colMin[col] > colMax[col]) continue;
    const int px = x + 1 + col;
    const int a = py(colMin[col]), b = py(colMax[col]);
    c.line(px, a, px, b, st.line);
    if (prevX >= 0) c.line(prevX, prevY, px, (a + b) / 2, st.line);
    prevX = px;
    prevY = (a + b) / 2;
  }
}

}  // namespace vm
