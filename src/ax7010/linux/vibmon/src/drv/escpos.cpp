#include "drv/escpos.h"

namespace vm {
namespace escpos {

std::string init() { return std::string("\x1B\x40", 2); }

std::string feedLines(int n) {
  if (n < 0) n = 0;
  if (n > 255) n = 255;
  std::string s("\x1B\x64", 2);
  s += static_cast<char>(n);
  return s;
}

std::string feedDots(int n) {
  if (n < 0) n = 0;
  if (n > 4096) n = 4096;
  std::string s;
  while (n > 0) {
    const int step = n > 255 ? 255 : n;
    s.append("\x1B\x4A", 2);
    s += static_cast<char>(step);
    n -= step;
  }
  return s;
}

std::string rasterStrip(const uint8_t* rows, int widthBytes, int height) {
  std::string s("\x1D\x76\x30\x00", 4);  // GS v 0, m = 0 (normal density)
  s += static_cast<char>(widthBytes & 0xFF);
  s += static_cast<char>((widthBytes >> 8) & 0xFF);
  s += static_cast<char>(height & 0xFF);
  s += static_cast<char>((height >> 8) & 0xFF);
  s.append(reinterpret_cast<const char*>(rows), static_cast<size_t>(widthBytes) * static_cast<size_t>(height));
  return s;
}

std::vector<std::string> rasterImage(const std::vector<uint8_t>& bits, int widthBytes, int height, int stripRows) {
  std::vector<std::string> out;
  if (stripRows < 1) stripRows = 1;
  for (int y = 0; y < height; y += stripRows) {
    int h = height - y < stripRows ? height - y : stripRows;
    out.push_back(rasterStrip(bits.data() + static_cast<size_t>(y) * widthBytes, widthBytes, h));
  }
  return out;
}

std::string realtimeStatus(int n) {
  std::string s("\x10\x04", 2);
  s += static_cast<char>(n);
  return s;
}

std::string paperSensorStatus() { return std::string("\x1D\x72\x01", 3); }

PaperStatus parseDleEot4(uint8_t b) {
  PaperStatus p;
  p.nearEnd = (b & 0x0C) != 0;
  p.paperOut = (b & 0x60) != 0;
  return p;
}

PaperStatus parseGsR1(uint8_t b) {
  PaperStatus p;
  p.nearEnd = (b & 0x03) != 0;
  p.paperOut = (b & 0x0C) != 0;
  return p;
}

}  // namespace escpos
}  // namespace vm
