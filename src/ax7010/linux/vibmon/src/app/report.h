#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "app/state.h"
#include "gfx/canvas.h"

namespace vm {

// Content of one printed slip, built from the saved measurement record so
// the paper always matches the stored result.
struct ReportData {
  std::string deviceId, measurementId, timeText, pointText;
  ResultSummary summary;
  std::string calibrationText;
  double fps = 0;
  uint32_t dropped = 0;
  std::string envText;
  std::vector<double> t, disp;  // waveform in summary.unit
  std::vector<double> f, amp;   // spectrum, amp in summary.unit (0-peak)
};

Canvas renderReport(const ReportData& d, int widthPx);
// 1 bit per dot, MSB = leftmost, 1 = black (luminance below threshold).
std::vector<uint8_t> toMonoBits(const Canvas& c, int& widthBytes, int threshold = 128);
const char* levelText(AlarmLevel l);

}  // namespace vm
