#pragma once

#include <cstdint>
#include <vector>

#include "gfx/canvas.h"

namespace vm {

// Anti-aliased primitives for the touch UI. Coverage comes from the distance
// to the shape edge and is evaluated only over the bounding box or edge band,
// so the cost stays proportional to the drawn area.
uint32_t mixColor(uint32_t a, uint32_t b, int t);  // t = 0..255, from a to b
void fillRoundRect(Canvas& c, int x, int y, int w, int h, int r, uint32_t color, int alpha = 255);
void strokeRoundRect(Canvas& c, int x, int y, int w, int h, int r, int width, uint32_t color, int alpha = 255);
void fillCircle(Canvas& c, float cx, float cy, float r, uint32_t color, int alpha = 255);
void strokeCircle(Canvas& c, float cx, float cy, float r, float width, uint32_t color, int alpha = 255);
// Arc with round caps; angles in radians, 0 = 12 o'clock, clockwise.
void strokeArc(Canvas& c, float cx, float cy, float r, float width, float a0, float a1, uint32_t color,
               int alpha = 255);
// Thick segment with round caps (icons and short strokes).
void lineAA(Canvas& c, float x0, float y0, float x1, float y1, float width, uint32_t color, int alpha = 255);
// Thin anti-aliased line (Xiaolin Wu); cost proportional to the length.
void wuLine(Canvas& c, float x0, float y0, float x1, float y1, uint32_t color, int alpha = 255);
// Filled polygon (even-odd rule) with 4x4 supersampling; meant for small shapes.
void fillPolygon(Canvas& c, const float* xy, int n, uint32_t color, int alpha = 255);
void fillGradientV(Canvas& c, int x, int y, int w, int h, uint32_t top, uint32_t bottom);
// Dashed rectangle outline (dash and gap in pixels).
void dashRect(Canvas& c, int x, int y, int w, int h, uint32_t color, int dash = 6, int gap = 4, int width = 2);
// Blit of the source window (sx, sy, sw, sh) of an 8-bit image into
// (x, y, w, h), bilinear unless nearest is set (index images such as depth
// maps). lut maps the value to a colour (nullptr = gray); alpha blends over
// the canvas.
void blitGray(Canvas& c, int x, int y, int w, int h, const uint8_t* img, int iw, int ih, double sx, double sy,
              double sw, double sh, const uint32_t* lut = nullptr, int alpha = 255, bool nearest = false);
// Opaque blit of the source window of a BGR888 image (byte 0 = blue): nearest
// when the window is reduced (a camera frame on the screen stays sharp),
// bilinear when it is enlarged (zoom).
void blitBgr(Canvas& c, int x, int y, int w, int h, const uint8_t* img, int iw, int ih, double sx, double sy,
             double sw, double sh);
// 256-entry perceptual colour map (dark blue -> cyan -> yellow -> red).
const uint32_t* heatLut();

}  // namespace vm
