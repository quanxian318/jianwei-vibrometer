#include "gfx/font.h"

#include <algorithm>
#include <cstdlib>

namespace vm {

#include "gfx/font_data.inc"

namespace {
const FontGlyph* findGlyph(const FontFace& f, uint32_t cp) {
  int lo = 0, hi = f.count - 1;
  while (lo <= hi) {
    const int mid = (lo + hi) / 2;
    if (f.glyphs[mid].codepoint == cp) return &f.glyphs[mid];
    if (f.glyphs[mid].codepoint < cp) {
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  return nullptr;
}

const FontGlyph* glyphOrFallback(const FontFace& f, uint32_t cp) {
  const FontGlyph* g = findGlyph(f, cp);
  return g ? g : findGlyph(f, '?');
}
}  // namespace

const FontFace& fontFace(int pixelSize, bool bold) {
  const FontFace* best = nullptr;
  for (const FontFace* f : kFaces) {
    if (f->bold != bold) continue;
    if (!best || std::abs(f->pixelSize - pixelSize) < std::abs(best->pixelSize - pixelSize)) best = f;
  }
  return best ? *best : *kFaces[0];
}

uint32_t nextCodepoint(const std::string& s, size_t& i) {
  const unsigned char c = static_cast<unsigned char>(s[i]);
  uint32_t cp = c;
  int extra = 0;
  if (c >= 0xF0) {
    cp = c & 0x07;
    extra = 3;
  } else if (c >= 0xE0) {
    cp = c & 0x0F;
    extra = 2;
  } else if (c >= 0xC0) {
    cp = c & 0x1F;
    extra = 1;
  }
  ++i;
  for (int k = 0; k < extra && i < s.size(); ++k, ++i) cp = (cp << 6) | (static_cast<unsigned char>(s[i]) & 0x3F);
  return cp;
}

bool hasGlyph(const FontFace& f, uint32_t codepoint) { return findGlyph(f, codepoint) != nullptr; }

int textWidth(const FontFace& f, const std::string& utf8) {
  int w = 0;
  for (size_t i = 0; i < utf8.size();) {
    const FontGlyph* g = glyphOrFallback(f, nextCodepoint(utf8, i));
    if (g) w += g->advance;
  }
  return w;
}

void textInk(const FontFace& f, const std::string& utf8, int& top, int& bottom) {
  top = f.lineHeight;
  bottom = 0;
  for (size_t i = 0; i < utf8.size();) {
    const FontGlyph* g = glyphOrFallback(f, nextCodepoint(utf8, i));
    if (!g || g->h == 0) continue;
    top = std::min(top, static_cast<int>(g->yoff));
    bottom = std::max(bottom, g->yoff + g->h);
  }
  if (bottom <= top) {
    top = 0;
    bottom = f.lineHeight;
  }
}

int textTopIn(const FontFace& f, int boxTop, int boxHeight) {
  int top = 0, bottom = 0;
  textInk(f, "测0", top, bottom);
  return boxTop + (boxHeight - (bottom - top)) / 2 - top;
}

int drawText(Canvas& c, const FontFace& f, int x, int y, const std::string& utf8, uint32_t color) {
  for (size_t i = 0; i < utf8.size();) {
    const FontGlyph* g = glyphOrFallback(f, nextCodepoint(utf8, i));
    if (!g) continue;
    const uint8_t* bm = f.bitmap + g->offset;
    for (int gy = 0; gy < g->h; ++gy) {
      for (int gx = 0; gx < g->w; ++gx) {
        const int idx = gy * g->w + gx;
        const uint8_t b = bm[idx >> 1];
        const int a4 = (idx & 1) ? (b & 0x0F) : (b >> 4);
        if (a4) c.blend(x + g->xoff + gx, y + g->yoff + gy, color, a4 * 17);
      }
    }
    x += g->advance;
  }
  return x;
}

int drawTextCentered(Canvas& c, const FontFace& f, int cx, int y, const std::string& utf8, uint32_t color) {
  return drawText(c, f, cx - textWidth(f, utf8) / 2, y, utf8, color);
}

int drawTextRight(Canvas& c, const FontFace& f, int right, int y, const std::string& utf8, uint32_t color) {
  return drawText(c, f, right - textWidth(f, utf8), y, utf8, color);
}

std::string fitText(const FontFace& f, const std::string& utf8, int maxWidth) {
  if (textWidth(f, utf8) <= maxWidth) return utf8;
  const std::string ell = "…";
  const int ellW = textWidth(f, ell);
  std::string out;
  int w = 0;
  for (size_t i = 0; i < utf8.size();) {
    const size_t start = i;
    const FontGlyph* g = glyphOrFallback(f, nextCodepoint(utf8, i));
    const int adv = g ? g->advance : 0;
    if (w + adv + ellW > maxWidth) break;
    w += adv;
    out.append(utf8, start, i - start);
  }
  return out + ell;
}

}  // namespace vm
