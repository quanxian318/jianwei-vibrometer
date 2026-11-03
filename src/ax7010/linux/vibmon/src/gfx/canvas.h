#pragma once

#include <cstdint>
#include <vector>

namespace vm {

// 32-bit 0x00RRGGBB software canvas used by the screen UI and the printer
// report (thresholded to 1 bit for printing).
class Canvas {
 public:
  Canvas() = default;
  Canvas(int w, int h, uint32_t color = 0) { resize(w, h, color); }
  void resize(int w, int h, uint32_t color = 0);
  int width() const { return w_; }
  int height() const { return h_; }
  uint32_t* data() { return px_.data(); }
  const uint32_t* data() const { return px_.data(); }
  uint32_t get(int x, int y) const { return px_[static_cast<size_t>(y) * w_ + x]; }

  void fill(uint32_t c);
  void set(int x, int y, uint32_t c) {
    if (x >= 0 && y >= 0 && x < w_ && y < h_) px_[static_cast<size_t>(y) * w_ + x] = c;
  }
  void blend(int x, int y, uint32_t c, int alpha);  // alpha 0..255
  void fillRect(int x, int y, int w, int h, uint32_t c);
  void rect(int x, int y, int w, int h, uint32_t c, int thickness = 1);
  void line(int x0, int y0, int x1, int y1, uint32_t c);
  void polyline(const std::vector<int>& xs, const std::vector<int>& ys, uint32_t c);
  // Nearest-neighbour scaled blit of an 8-bit gray image into (x, y, w, h).
  void drawGray(int x, int y, int w, int h, const uint8_t* gray, int gw, int gh);

 private:
  int w_ = 0, h_ = 0;
  std::vector<uint32_t> px_;
};

}  // namespace vm
