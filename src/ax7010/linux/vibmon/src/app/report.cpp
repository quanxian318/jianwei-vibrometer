#include "app/report.h"

#include <cstdio>

#include "gfx/font.h"
#include "gfx/plot.h"

namespace vm {

const char* levelText(AlarmLevel l) {
  switch (l) {
    case AlarmLevel::kUnknown: return "未判级";
    case AlarmLevel::kNormal: return "正常";
    case AlarmLevel::kWarn: return "预警";
    case AlarmLevel::kAlarm: return "报警";
  }
  return "";
}

Canvas renderReport(const ReportData& d, int width) {
  const FontFace& big = fontFace(26);
  const FontFace& small = fontFace(18);
  Canvas c(width, 2000, 0xFFFFFF);
  const uint32_t black = 0x000000;
  int y = 4;
  drawTextCentered(c, big, width / 2, y, "视觉测振报告", black);
  y += big.lineHeight + 6;
  auto line = [&](const std::string& s) {
    drawText(c, small, 2, y, s, black);
    y += small.lineHeight + 2;
  };
  line("设备: " + d.deviceId);
  line("编号: " + d.measurementId);
  line("时间: " + d.timeText);
  line("测点: " + (d.pointText.empty() ? std::string("未指定") : d.pointText));
  c.fillRect(0, y + 2, width, 2, black);
  y += 8;
  char buf[96];
  const ResultSummary& r = d.summary;
  if (!r.valid) {
    drawText(c, big, 2, y, "结果无效", black);
    y += big.lineHeight + 2;
    line("原因: " + r.reason);
  } else if (r.method == "multipoint") {
    snprintf(buf, sizeof(buf), "主频 %.2f Hz", r.dominantHz);
    drawText(c, big, 2, y, buf, black);
    y += big.lineHeight + 2;
    snprintf(buf, sizeof(buf), "超过停机基线 %d/%d 测点", r.passedPoints, r.points);
    drawText(c, big, 2, y, buf, black);
    y += big.lineHeight + 2;
    snprintf(buf, sizeof(buf), "测点 RMS %.4g px (图像平面)", r.pointRmsPx);
    line(buf);
    if (!r.waveformValid) line("代表测点波形无效: " + r.waveformReason);
    line("多点测振: 与停机基线比较, 不判级");
  } else {
    if (r.peakClear) {
      snprintf(buf, sizeof(buf), "主频 %.2f Hz", r.dominantHz);
      drawText(c, big, 2, y, buf, black);
      y += big.lineHeight + 2;
    } else {
      drawText(c, big, 2, y, "无明确主频", black);
      y += big.lineHeight + 2;
      snprintf(buf, sizeof(buf), "最大谱峰 %.2f Hz (不足以判为主频)", r.dominantHz);
      line(buf);
    }
    snprintf(buf, sizeof(buf), "峰峰值 %.4g %s", r.peakToPeak, r.unit.c_str());
    drawText(c, big, 2, y, buf, black);
    y += big.lineHeight + 2;
    snprintf(buf, sizeof(buf), "有效值 %.4g %s  峰值 %.4g %s", r.rms, r.unit.c_str(), r.peak, r.unit.c_str());
    line(buf);
    snprintf(buf, sizeof(buf), "判级: %s", levelText(r.level));
    drawText(c, big, 2, y, buf, black);
    y += big.lineHeight + 2;
  }
  snprintf(buf, sizeof(buf), "帧率 %.1f fps  丢帧 %u", d.fps, d.dropped);
  line(buf);
  line("标定: " + d.calibrationText);
  if (!d.envText.empty()) line(d.envText);
  y += 4;
  PlotStyle st;
  st.symmetric = true;
  if (!d.t.empty()) {
    drawLinePlot(c, 0, y, width, 150, d.t, d.disp, r.method == "multipoint" ? "代表测点位移 (s)" : "位移波形 (s)",
                 r.unit, st);
    y += 154;
  }
  if (!d.f.empty()) {
    st.symmetric = false;
    drawLinePlot(c, 0, y, width, 150, d.f, d.amp, "幅值谱 (Hz)", r.unit, st);
    y += 154;
  }
  y += 4;
  line("仅对本次安装与测量条件有效");
  Canvas out(width, y + 4, 0xFFFFFF);
  for (int yy = 0; yy < out.height(); ++yy) {
    for (int xx = 0; xx < width; ++xx) out.set(xx, yy, c.get(xx, yy));
  }
  return out;
}

std::vector<uint8_t> toMonoBits(const Canvas& c, int& widthBytes, int threshold) {
  widthBytes = (c.width() + 7) / 8;
  std::vector<uint8_t> bits(static_cast<size_t>(widthBytes) * c.height(), 0);
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t p = c.get(x, y);
      const int lum = (77 * ((p >> 16) & 0xFF) + 150 * ((p >> 8) & 0xFF) + 29 * (p & 0xFF)) >> 8;
      if (lum < threshold) bits[static_cast<size_t>(y) * widthBytes + x / 8] |= static_cast<uint8_t>(0x80 >> (x % 8));
    }
  }
  return bits;
}

}  // namespace vm
