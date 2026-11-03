#pragma once

#include <string>
#include <vector>

#include "gfx/canvas.h"
#include "gfx/font.h"

namespace vm {

struct PlotStyle {
  uint32_t background = 0xFFFFFF;
  uint32_t frame = 0x000000;
  uint32_t grid = 0xC0C0C0;
  uint32_t line = 0x000000;
  uint32_t text = 0x000000;
  int fontSize = 18;
  bool symmetric = false;  // y range symmetric around zero
};

// Line plot of ys over xs (xs ascending) inside the rectangle, with the
// title at the top-left and the y range printed on the right.
void drawLinePlot(Canvas& c, int x, int y, int w, int h, const std::vector<double>& xs, const std::vector<double>& ys,
                  const std::string& title, const std::string& unit, const PlotStyle& st);

}  // namespace vm
