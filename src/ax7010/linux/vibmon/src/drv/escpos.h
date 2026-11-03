#pragma once

#include <cstdint>
#include <string>
#include <vector>

// ESC/POS byte sequences for the EM5820H 58 mm panel printer (384 dots/line).
// The batch-specific command manual is still outstanding; only widely
// supported commands are used and the status query style is configurable.
namespace vm {
namespace escpos {

std::string init();            // ESC @
std::string feedLines(int n);  // ESC d n
std::string feedDots(int n);   // ESC J chunks; default movement unit = one dot
// GS v 0 raster strip: rows of widthBytes, MSB = leftmost dot, 1 = black.
std::string rasterStrip(const uint8_t* rows, int widthBytes, int height);
// Splits a full image into strips of at most stripRows rows.
std::vector<std::string> rasterImage(const std::vector<uint8_t>& bits, int widthBytes, int height, int stripRows);

std::string realtimeStatus(int n);  // DLE EOT n (answered immediately, not in print order)
std::string paperSensorStatus();    // GS r 1 (answered in print order)

struct PaperStatus {
  bool paperOut = false;
  bool nearEnd = false;
};
// DLE EOT 4 reply: bits 2-3 near end, bits 5-6 paper end.
PaperStatus parseDleEot4(uint8_t b);
// GS r 1 reply: bits 0-1 near end, bits 2-3 paper end.
PaperStatus parseGsR1(uint8_t b);

}  // namespace escpos
}  // namespace vm
