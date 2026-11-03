#include "ui/ui_core.h"

#include <algorithm>
#include <cstdio>

#include "app/report.h"
#include "gfx/draw.h"
#include "util/clock.h"

namespace vm {

using namespace theme;

// ---------------------------------------------------------------------------
// Widgets
// ---------------------------------------------------------------------------

void Ui::hit(const Rect& r, const UiAction& a, const Enable& enabled) {
  hits_.push_back(HitRegion{r, a, enabled.on, enabled.why});
}

void Ui::block(const Rect& r) { hits_.push_back(HitRegion{r, UiAction(), false, std::string()}); }

void Ui::card(const Rect& r, uint32_t fill) {
  fillRoundRect(c, r.x, r.y, r.w, r.h, kRadius, fill);
  strokeRoundRect(c, r.x, r.y, r.w, r.h, kRadius, 1, kBorder);
  c.fillRect(r.x + kRadius, r.y, r.w - 2 * kRadius, 1, kCardEdge);
}

void Ui::cardTitle(const Rect& r, const std::string& title, Icon icon) {
  int x = r.x + 14;
  if (icon != Icon::kNone) {
    drawIcon(c, icon, x + 9, r.y + 20, 18, kText2);
    x += 26;
  }
  drawText(c, fontFace(18), x, r.y + 8, title, kText2);
}

void Ui::button(const Rect& r, const std::string& label, Icon icon, uint32_t fill, const Enable& enabled,
                const UiAction& a, int fontPx) {
  const bool down = enabled && pressed(r);
  uint32_t f = enabled ? fill : kDisabled;
  if (down) f = mixColor(f, 0xFFFFFF, 50);
  fillRoundRect(c, r.x, r.y, r.w, r.h, 10, f);
  if (!enabled || fill == kNeutral) strokeRoundRect(c, r.x, r.y, r.w, r.h, 10, 1, kBorder);
  const uint32_t tc = enabled ? 0xFFFFFF : kText3;
  const FontFace& font = fontFace(fontPx, true);
  const int tw = textWidth(font, label);
  if (icon != Icon::kNone && r.h >= 72) {  // icon above the label
    const int isz = std::min(30, r.h / 3);
    drawIcon(c, icon, r.x + r.w / 2.f, r.y + r.h * 0.36f, static_cast<float>(isz), tc);
    drawTextCentered(c, font, r.x + r.w / 2, r.y + r.h * 0.36f + isz * 0.5f + 4, label, tc);
  } else {
    const int isz = icon != Icon::kNone ? fontPx + 2 : 0, gap = icon != Icon::kNone ? 10 : 0;
    const int x0 = r.x + (r.w - (isz + gap + tw)) / 2;
    if (icon != Icon::kNone) drawIcon(c, icon, x0 + isz / 2.f, r.y + r.h / 2.f, static_cast<float>(isz), tc);
    drawText(c, font, x0 + isz + gap, textTopIn(font, r.y, r.h), label, tc);
  }
  hit(r, a, enabled);
}

void Ui::iconButton(const Rect& r, Icon icon, const Enable& enabled, const UiAction& a, uint32_t fill) {
  uint32_t f = enabled ? fill : kDisabled;
  if (enabled && pressed(r)) f = mixColor(f, 0xFFFFFF, 50);
  fillRoundRect(c, r.x, r.y, r.w, r.h, std::min(r.w, r.h) / 2, f, 230);
  drawIcon(c, icon, r.x + r.w / 2.f, r.y + r.h / 2.f, std::min(r.w, r.h) * 0.48f, enabled ? kText : kText3);
  hit(r, a, enabled);
}

void Ui::chip(const Rect& r, const std::string& label, bool selected, const Enable& enabled, const UiAction& a) {
  uint32_t f = selected ? (enabled ? kAccent : kAccentDim) : kCardHi;
  if (enabled && pressed(r)) f = mixColor(f, 0xFFFFFF, 40);
  fillRoundRect(c, r.x, r.y, r.w, r.h, r.h / 2, f);
  if (!selected) strokeRoundRect(c, r.x, r.y, r.w, r.h, r.h / 2, 1, kBorder);
  const FontFace& font = fontFace(18);
  const uint32_t tc = selected ? 0xFFFFFF : (enabled ? kText : kText3);
  drawTextCentered(c, font, r.x + r.w / 2, textTopIn(font, r.y, r.h), label, tc);
  hit(r, a, enabled ? Enable(!selected) : enabled);  // a selected chip does nothing, a grey one says why
}

void Ui::toggle(const Rect& r, bool on, const Enable& enabled, const UiAction& a) {
  const uint32_t track = on ? (enabled ? kGreen : 0x2C5A40) : (enabled ? kNeutral : kDisabled);
  fillRoundRect(c, r.x, r.y, r.w, r.h, r.h / 2, track);
  const float kr = r.h / 2.f - 4;
  const float kx = on ? r.x + r.w - r.h / 2.f : r.x + r.h / 2.f;
  fillCircle(c, kx, r.y + r.h / 2.f, kr, enabled ? 0xFFFFFF : kText3);
  hit(Rect{r.x - 8, r.y - 8, r.w + 16, r.h + 16}, a, enabled);
}

void Ui::stepper(const Rect& r, const std::string& value, const Enable& decOk, const Enable& incOk,
                 const UiAction& dec, const UiAction& inc) {
  fillRoundRect(c, r.x, r.y, r.w, r.h, 10, kCardHi);
  strokeRoundRect(c, r.x, r.y, r.w, r.h, 10, 1, kBorder);
  const int bw = r.h;
  iconButton(Rect{r.x + 2, r.y + 2, bw - 4, r.h - 4}, Icon::kMinus, decOk, dec, kNeutral);
  iconButton(Rect{r.x + r.w - bw + 2, r.y + 2, bw - 4, r.h - 4}, Icon::kPlus, incOk, inc, kNeutral);
  const FontFace& font = fontFace(22, true);
  drawTextCentered(c, font, r.x + r.w / 2, textTopIn(font, r.y, r.h), value, kText);
}

int Ui::badge(int x, int y, const std::string& text, uint32_t color, bool filled, int fontPx) {
  const FontFace& font = fontFace(fontPx);
  const int w = textWidth(font, text) + 20, h = font.lineHeight + 4;
  if (filled) {
    fillRoundRect(c, x, y, w, h, h / 2, color);
  } else {
    fillRoundRect(c, x, y, w, h, h / 2, color, 46);
  }
  drawText(c, font, x + 10, textTopIn(font, y, h), text, filled ? 0xFFFFFF : color);
  return w;
}

void Ui::emptyState(const Rect& r, Icon icon, const std::string& title, const std::string& hint) {
  const int cy = r.y + r.h / 2;
  fillCircle(c, r.x + r.w / 2.f, cy - 48.f, 44, kCardHi);
  drawIcon(c, icon, r.x + r.w / 2.f, cy - 48.f, 48, kText2);
  drawTextCentered(c, fontFace(22, true), r.x + r.w / 2, cy + 8, title, kText);
  drawTextCentered(c, fontFace(18), r.x + r.w / 2, cy + 44, hint, kText2);
}

void Ui::metric(int x, int y, const std::string& label, const std::string& value, const std::string& unit,
                uint32_t valueColor) {
  drawText(c, fontFace(14), x, y, label, kText2);
  const int vx = drawText(c, fontFace(22, true), x, y + 20, value, valueColor);
  if (!unit.empty()) drawText(c, fontFace(14), vx + 4, y + 30, unit, kText2);
}

// ---------------------------------------------------------------------------
// Chart
// ---------------------------------------------------------------------------

namespace {
double niceStep(double range, int targetTicks) {
  if (range <= 0) return 1;
  const double raw = range / targetTicks;
  const double mag = std::pow(10.0, std::floor(std::log10(raw)));
  const double n = raw / mag;
  return (n < 1.5 ? 1 : (n < 3 ? 2 : (n < 7 ? 5 : 10))) * mag;
}

std::string tickText(double v, double step) {
  char b[32];
  if (std::fabs(v) < step * 1e-6) v = 0;
  const int dec = step >= 1 ? 0 : std::min(4, static_cast<int>(std::ceil(-std::log10(step) - 1e-9)));
  snprintf(b, sizeof(b), "%.*f", dec, v);
  return b;
}
}  // namespace

void drawChart(Canvas& c, const Rect& r, const std::vector<double>& xs, const std::vector<double>& ys,
               const ChartOptions& o) {
  const FontFace& f14 = fontFace(14);
  const FontFace& f18 = fontFace(18);
  const int tx = drawText(c, f18, r.x + 14, r.y + 8, o.title, kText2);
  if (!o.unitY.empty()) drawText(c, f14, tx + 8, r.y + 12, "(" + o.unitY + ")", kText3);
  if (!o.rightNote.empty()) drawTextRight(c, f14, r.x + r.w - 14, r.y + 12, o.rightNote, kText2);
  const Rect p{r.x + 56, r.y + 38, r.w - 56 - 30, r.h - 38 - 30};
  if (p.w < 20 || p.h < 20) return;
  const size_t n = std::min(xs.size(), ys.size());
  double x0 = 0, x1 = 1, y0 = -1, y1 = 1;
  if (n >= 2) {
    x0 = xs.front();
    x1 = xs[n - 1] > x0 ? xs[n - 1] : x0 + 1;
    y0 = y1 = ys[0];
    for (size_t i = 0; i < n; ++i) {
      if (!std::isfinite(ys[i])) continue;
      y0 = std::min(y0, ys[i]);
      y1 = std::max(y1, ys[i]);
    }
    if (o.symmetric) {
      const double m = std::max(std::fabs(y0), std::fabs(y1));
      y0 = -m;
      y1 = m;
    }
    if (o.fromZero) y0 = 0;
    if (y1 - y0 < 1e-12) {
      y1 += 1;
      if (!o.fromZero) y0 -= 1;
    }
    const double pad = (y1 - y0) * 0.08;
    y1 += pad;
    if (!o.fromZero) y0 -= pad;
  }
  // Grid and ticks.
  const double ys0 = niceStep(y1 - y0, 4);
  for (double v = std::ceil(y0 / ys0) * ys0; v <= y1 + 1e-12; v += ys0) {
    const int py = p.y + p.h - 1 - static_cast<int>((v - y0) / (y1 - y0) * (p.h - 1));
    c.fillRect(p.x, py, p.w, 1, std::fabs(v) < ys0 * 1e-6 ? kBorder : kGrid);
    drawTextRight(c, f14, p.x - 6, py - 10, tickText(v, ys0), kText2);
  }
  const double xs0 = niceStep(x1 - x0, 6);
  const int ux = o.unitX.empty() ? r.x + r.w : r.x + r.w - 8 - textWidth(f14, o.unitX);
  for (double v = std::ceil(x0 / xs0) * xs0; v <= x1 + 1e-12; v += xs0) {
    const int px = p.x + static_cast<int>((v - x0) / (x1 - x0) * (p.w - 1));
    c.fillRect(px, p.y, 1, p.h, kGrid);
    const std::string t = tickText(v, xs0);
    if (px + textWidth(f14, t) / 2 < ux - 4) drawTextCentered(c, f14, px, p.y + p.h + 4, t, kText2);  // clear of the unit
  }
  if (!o.unitX.empty()) drawTextRight(c, f14, r.x + r.w - 8, p.y + p.h + 4, o.unitX, kText3);
  if (n < 2) {
    drawTextCentered(c, f18, p.x + p.w / 2, p.y + p.h / 2 - 12, "无数据", kText3);
    return;
  }
  // Data: one min/max pair per pixel column so peaks are never lost.
  std::vector<float> lo(p.w, 1e30f), hi(p.w, -1e30f), last(p.w, 0.f);
  std::vector<char> has(p.w, 0);
  auto fy = [&](double v) { return static_cast<float>(p.y + p.h - 1 - (v - y0) / (y1 - y0) * (p.h - 1)); };
  for (size_t i = 0; i < n; ++i) {
    if (!std::isfinite(ys[i]) || xs[i] < x0 || xs[i] > x1) continue;
    const int col = std::max(0, std::min(p.w - 1, static_cast<int>((xs[i] - x0) / (x1 - x0) * (p.w - 1) + 0.5)));
    const float v = fy(std::max(y0, std::min(y1, ys[i])));
    lo[col] = std::min(lo[col], v);
    hi[col] = std::max(hi[col], v);
    last[col] = v;
    has[col] = 1;
  }
  if (o.fill) {
    const float base = fy(y0 > 0 ? y0 : 0);
    for (int col = 0; col < p.w; ++col) {
      if (!has[col]) continue;
      for (int yy = static_cast<int>(lo[col]); yy <= static_cast<int>(base); ++yy) c.blend(p.x + col, yy, o.line, 60);
    }
  }
  float px = -1, py = 0;
  for (int col = 0; col < p.w; ++col) {
    if (!has[col]) continue;
    const float x = static_cast<float>(p.x + col);
    if (hi[col] - lo[col] > 1.f) wuLine(c, x, lo[col], x, hi[col], o.line);
    const float mid = (lo[col] + hi[col]) * 0.5f;
    if (px >= 0) wuLine(c, px, py, x, mid, o.line);
    px = x;
    py = last[col];
  }
  if (std::isfinite(o.markerX) && o.markerX >= x0 && o.markerX <= x1) {
    const int mx = p.x + static_cast<int>((o.markerX - x0) / (x1 - x0) * (p.w - 1));
    for (int yy = p.y; yy < p.y + p.h; yy += 6) c.fillRect(mx, yy, 1, 3, kAmber);
    if (!o.markerLabel.empty()) {
      const int tw = textWidth(f14, o.markerLabel) + 12;
      const int bx = std::min(mx + 6, p.x + p.w - tw);
      fillRoundRect(c, bx, p.y + 4, tw, 22, 6, kAmber);
      drawText(c, f14, bx + 6, textTopIn(f14, p.y + 4, 22), o.markerLabel, 0x101010);
    }
  }
  if (std::isfinite(o.refX) && o.refX >= x0 && o.refX <= x1) {
    const int mx = p.x + static_cast<int>((o.refX - x0) / (x1 - x0) * (p.w - 1));
    for (int yy = p.y + 3; yy < p.y + p.h; yy += 6) c.fillRect(mx, yy, 1, 3, o.refColor);
    if (!o.refLabel.empty()) {
      const int tw = textWidth(f14, o.refLabel) + 12;
      const int bx = std::min(mx + 6, p.x + p.w - tw);
      fillRoundRect(c, bx, p.y + 30, tw, 22, 6, o.refColor);
      drawText(c, f14, bx + 6, textTopIn(f14, p.y + 30, 22), o.refLabel, 0x101010);
    }
  }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

UiAction cmdAction(const JsonValue& cmd) {
  UiAction a;
  a.kind = "cmd";
  a.cmd = cmd;
  return a;
}

UiAction cmdAction(const std::string& name) {
  JsonValue j = JsonValue::makeObject();
  j.set("cmd", name);
  return cmdAction(j);
}

UiAction uiAction(const std::string& kind, int arg, const std::string& text) {
  UiAction a;
  a.kind = kind;
  a.arg = arg;
  a.text = text;
  return a;
}

uint32_t levelColor(AlarmLevel l) {
  switch (l) {
    case AlarmLevel::kNormal: return kGreen;
    case AlarmLevel::kWarn: return kAmber;
    case AlarmLevel::kAlarm: return kRed;
    default: return kText3;
  }
}

std::string fmtNum(double v, int sig) {
  char b[32];
  snprintf(b, sizeof(b), "%.*g", sig, v);
  std::string s = b;
  if (s == "-0") s = "0";
  return s;
}

std::string reasonText(const std::string& reason) {
  static const struct {
    const char* key;
    const char* text;
  } kMap[] = {
      {"too_few_frames", "采集帧数太少"},       {"too_few_valid_frames", "有效帧数不足"},
      {"dropped_frames", "采集过程中有丢帧"},   {"tracking_lost", "测点跟踪丢失"},
      {"search_edge", "位移超出搜索范围"},      {"image_saturated", "画面过曝"},
      {"bad_timestamps", "帧时间戳无效"},       {"irregular_sampling", "采样间隔不均匀"},
      {"no_spectrum", "无法计算频谱"},          {"roi_not_trackable", "测点纹理不足"},
      {"camera_timeout", "相机无画面"},         {"roi_outside_frame", "测点超出画面"},
      {"roi_too_large", "测点框超过跟踪上限"},  {"roi_flat", "测点处没有纹理"},
      {"roi_weak_texture", "测点纹理太弱"},     {"roi_single_edge", "测点只有单方向纹理"},
      {"not_converged", "位移计算不稳定"},       {"reference_invalid", "参考点无效"},
      {"no_signal", "未超过停机基线"},          {"baseline_required", "请先采集停机基线"},
      {"baseline_mismatch", "帧率与基线不一致"},  {"invalid_windows", "分析窗口无效"},
      {"baseline_too_short", "基线时长不足"},    {"baseline_not_saved", "基线未能保存"},
      {"reference_outside_frame", "参考点超出画面"}, {"reference_in_box", "参考点落在测点框内"},
      {"reference_lost", "参考点跟踪失败"},
  };
  for (const auto& m : kMap) {
    if (reason == m.key) return m.text;
  }
  if (reason.compare(0, 10, "reference_") == 0) return "参考点: " + reasonText(reason.substr(10));
  return reason.empty() ? std::string("未知原因") : reason;
}

std::string acousticErrorText(const std::string& e) {
  if (e.find("no PDM receiver") != std::string::npos) return "PL 位流不含麦克风接收核";
  if (e.find("microphone missing") != std::string::npos) return "无麦克风数据（未接或 DAT 异常）";
  if (e.find("no PDM data while") != std::string::npos) return "所设相位无数据，检查 L/R";
  if (e.find("power") != std::string::npos) return "掉电预警，麦克风已停";
  if (e.find("cannot map") != std::string::npos) return "无法访问麦克风接收核";
  if (e.find("not running") != std::string::npos) return "麦克风时钟未运行";
  return e.empty() ? std::string("麦克风故障") : e;
}

std::string lightBlock(const SystemState& s) {
  if (s.pfail) return "掉电预警中";
  if (s.printerState != "idle") return "打印时不能开关照明和投影";
  if (!s.lightOn && !s.lightAllowed) return "机壳照明开关未打开";
  return std::string();
}

std::string laserBlock(const SystemState& s) {
  if (s.pfail) return "掉电预警中";
  if (s.printerState != "idle") return "打印时不能开关照明和投影";
  if (s.laserOn) return std::string();
  if (!s.lightAllowed) return "机壳照明开关未打开";
  // Measurements switch the projector off; its pattern does not move with the surface.
  if (s.settings.laserOffDuringMeasure && (s.measureState == "capturing" || s.measureState == "analyzing")) {
    return "测量期间投影保持关闭";
  }
  return std::string();
}

std::string errorText(const std::string& code) {
  static const struct {
    const char* key;
    const char* text;
  } kMap[] = {
      {"camera_not_ready", "相机未就绪"},     {"already_measuring", "正在测量"},
      {"printing", "打印机工作中"},           {"power", "电源异常，暂不可用"},
      {"capture_refused", "无法开始采集"},    {"no_record", "没有可打印的记录"},
      {"measuring", "测量中，请稍后"},        {"power_budget", "电源余量不足"},
      {"record_unreadable", "记录无法读取"},  {"no_printer_uart", "打印机串口不可用"},
      {"no_carrier", "载板控制不可用"},       {"power_fail", "掉电预警中"},
      {"light_interlock", "补光硬件开关未允许"}, {"light_io", "补光控制或供电状态异常"},
      {"projector_io", "投影控制或供电状态异常"},
      {"no_depth", "暂无深度图"},             {"no_depth_at_point", "该点没有有效深度"},
      {"offset_too_large", "偏差过大，检查距离和选点"}, {"stereo_off", "双目未启用"},
      {"no_calibration", "没有双目标定"},
      {"depth_stale", "上次校正尚未生效，请等新的深度图"},
      {"lights_on", "照明或激光开启中"},            {"nano_offline", "协处理器离线"},
      {"shutting_down", "正在关机"},          {"busy", "打印机忙"},
      {"invalid_value", "设置值无效"},        {"save_failed", "设置未能保存"},
      {"baseline_required", "请先采集停机基线"}, {"baseline_not_used", "当前测量方式不用基线"},
      {"storage", "存储不可用"},              {"roi_outside_frame", "测点超出画面"},
      {"reference_outside_frame", "参考点超出画面"}, {"reference_in_box", "参考点落在测点框内"},
  };
  for (const auto& m : kMap) {
    if (code == m.key) return m.text;
  }
  return code.empty() ? std::string("操作失败") : code;
}

const char* pageTitle(Page p) {
  switch (p) {
    case Page::kLive: return "实时监测";
    case Page::kResult: return "测量结果";
    case Page::kVision: return "视觉分析";
    case Page::kHistory: return "测量记录";
    case Page::kDevice: return "设备状态";
    case Page::kSettings: return "系统设置";
  }
  return "";
}

Rect fitAspect(const Rect& box, int fw, int fh) {
  if (fw <= 0 || fh <= 0) {
    fw = 16;
    fh = 9;
  }
  int w = box.w, h = box.w * fh / fw;
  if (h > box.h) {
    h = box.h;
    w = box.h * fw / fh;
  }
  return Rect{box.x + (box.w - w) / 2, box.y + (box.h - h) / 2, w, h};
}

void drawPreview(Canvas& c, const Rect& r, const PreviewImage& p, double sx, double sy, double sw, double sh) {
  const size_t n = static_cast<size_t>(p.width) * p.height;
  if (p.bgr.size() == 3 * n) {
    blitBgr(c, r.x, r.y, r.w, r.h, p.bgr.data(), p.width, p.height, sx * p.width, sy * p.height, sw * p.width,
            sh * p.height);
  } else if (p.gray.size() == n) {
    blitGray(c, r.x, r.y, r.w, r.h, p.gray.data(), p.width, p.height, sx * p.width, sy * p.height, sw * p.width,
             sh * p.height);
  }
}

NormBox roiBox(const SystemState& s, int fw, int fh) {
  const double hw = 0.5 * s.settings.roiW / std::max(1, fw), hh = 0.5 * s.settings.roiH / std::max(1, fh);
  return NormBox{s.roiCx - hw, s.roiCy - hh, s.roiCx + hw, s.roiCy + hh};
}

NormBox refGroupBox(const SystemState& s, int fw, int fh, int refPitchPx) {
  const double hw = (0.5 * refPitchPx + 12.5) / std::max(1, fw), hh = (0.5 * refPitchPx + 12.5) / std::max(1, fh);
  return NormBox{s.refCx - hw, s.refCy - hh, s.refCx + hw, s.refCy + hh};
}

Rect roiScreenRect(const NormBox& b, const UiAction& a, const Rect& region) {
  const auto sx = [&](double nx) { return static_cast<int>(std::lround(region.x + (nx - a.nx0) / a.nw * region.w)); };
  const auto sy = [&](double ny) { return static_cast<int>(std::lround(region.y + (ny - a.ny0) / a.nh * region.h)); };
  const int x0 = sx(b.x0), y0 = sy(b.y0);
  return Rect{x0, y0, sx(b.x1) - x0, sy(b.y1) - y0};
}

int roiGrabAt(const Rect& b, int x, int y) {
  constexpr int kReachOut = 28, kReachIn = 14;  // pixels; a fingertip covers about 40
  if (x < b.x - kReachOut || y < b.y - kReachOut || x >= b.x + b.w + kReachOut || y >= b.y + b.h + kReachOut) {
    return 0;
  }
  const int inX = std::min(kReachIn, b.w / 4), inY = std::min(kReachIn, b.h / 4);
  int g = 0;
  if (x < b.x + inX) {
    g |= kGrabLeft;
  } else if (x >= b.x + b.w - inX) {
    g |= kGrabRight;
  }
  if (y < b.y + inY) {
    g |= kGrabTop;
  } else if (y >= b.y + b.h - inY) {
    g |= kGrabBottom;
  }
  return g ? g : kGrabMove;
}

NormBox roiDragged(const NormBox& b, int grab, double dx, double dy, int fw, int fh, int maxPx) {
  // The lower bound wins when the range is empty (box larger than the image).
  const auto clamp = [](double v, double lo, double hi) { return std::max(lo, std::min(hi, v)); };
  NormBox r = b;
  if (grab & kGrabMove) {
    const double w = b.x1 - b.x0, h = b.y1 - b.y0;
    r.x0 = clamp(b.x0 + dx, 0, 1 - w);
    r.y0 = clamp(b.y0 + dy, 0, 1 - h);
    r.x1 = r.x0 + w;
    r.y1 = r.y0 + h;
    return r;
  }
  const double minW = static_cast<double>(kRoiMinPx) / fw, maxW = static_cast<double>(maxPx) / fw;
  const double minH = static_cast<double>(kRoiMinPx) / fh, maxH = static_cast<double>(maxPx) / fh;
  if (grab & kGrabLeft) r.x0 = clamp(b.x0 + dx, std::max(0.0, b.x1 - maxW), b.x1 - minW);
  if (grab & kGrabRight) r.x1 = clamp(b.x1 + dx, b.x0 + minW, std::min(1.0, b.x0 + maxW));
  if (grab & kGrabTop) r.y0 = clamp(b.y0 + dy, std::max(0.0, b.y1 - maxH), b.y1 - minH);
  if (grab & kGrabBottom) r.y1 = clamp(b.y1 + dy, b.y0 + minH, std::min(1.0, b.y0 + maxH));
  return r;
}

void pannedCentre(const UiAction& a, const Rect& region, int dx, int dy, double& cx, double& cy) {
  const double hw = 0.5 * a.nw, hh = 0.5 * a.nh;
  cx = std::max(hw, std::min(1 - hw, a.nx0 + hw - static_cast<double>(dx) / std::max(1, region.w) * a.nw));
  cy = std::max(hh, std::min(1 - hh, a.ny0 + hh - static_cast<double>(dy) / std::max(1, region.h) * a.nh));
}

JsonValue roiCommand(const NormBox& b, int fw, int fh) {
  JsonValue cmd = JsonValue::makeObject();
  cmd.set("cmd", "roi");
  cmd.set("x", 0.5 * (b.x0 + b.x1));
  cmd.set("y", 0.5 * (b.y0 + b.y1));
  cmd.set("w", static_cast<int>(std::lround((b.x1 - b.x0) * fw)));
  cmd.set("h", static_cast<int>(std::lround((b.y1 - b.y0) * fh)));
  return cmd;
}

UiAction hitTest(const std::vector<HitRegion>& hits, int x, int y, Rect* region) {
  for (auto it = hits.rbegin(); it != hits.rend(); ++it) {
    if (!it->r.contains(x, y)) continue;
    if (region) *region = it->r;
    if (it->enabled) return it->a;
    return it->why.empty() ? UiAction() : uiAction("why", 0, it->why);
  }
  return UiAction();
}

// ---------------------------------------------------------------------------
// Frame: status bar, navigation rail, toast, dialogs
// ---------------------------------------------------------------------------

namespace {

std::string wifiLabel(const SystemState& s) {
  if (!s.nanoOnline) return "协处理器离线";
  if (s.wifiState == "CONNECTED") return s.wifiSsid.empty() ? std::string("已联网") : s.wifiSsid;
  if (s.wifiState == "CONNECTING") return "连接中";
  if (s.wifiState == "OFF") return "未配置";
  return "未联网";
}

void topBar(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  if (s.pfail) {
    c.fillRect(0, 0, kW, kBarH, 0x4A1616);
  } else {
    fillGradientV(c, 0, 0, kW, kBarH, kBarTop, kBar);
  }
  c.fillRect(0, kBarH - 1, kW, 1, kBorder);
  drawIcon(c, Icon::kLogo, 28, 24, 28, kAccent);
  const int x = drawText(c, fontFace(22, true), 50, textTopIn(fontFace(22, true), 8, 32), "见微", kText);
  c.fillRect(x + 14, 14, 1, 20, kBorder);
  drawText(c, fontFace(18), x + 28, textTopIn(fontFace(18), 8, 32), pageTitle(ui.st.page), kText);

  // Right-aligned status items.
  int rx = kW - 14;
  // Warnings sit in tinted pills, plain status as icon and text.
  auto item = [&](Icon icon, const std::string& text, uint32_t color, bool pill = false) {
    const FontFace& f = fontFace(18);
    const int pad = pill ? 11 : 0;
    const int w = 22 + 6 + textWidth(f, text) + 2 * pad;
    const int x0 = rx - w;
    if (pill) fillRoundRect(c, x0, 8, w, 32, 16, color, 48);
    drawIcon(c, icon, x0 + pad + 11.f, 24.f, 20.f, color);
    drawText(c, f, x0 + pad + 28, textTopIn(f, 8, 32), text, color);
    rx = x0 - (pill ? 12 : 18);
  };
  const std::string clock = s.timeValid ? formatLocal(d.wallMs, d.tzOffsetMin, "%H:%M:%S") : "--:--:--";
  item(Icon::kClock, clock, s.timeValid ? kText : kAmber);
  char buf[64];
  snprintf(buf, sizeof(buf), "%.1fV %.2fA", s.power.busV, s.power.currentA);
  item(Icon::kBolt, s.inaOk ? std::string(buf) : std::string("--"), s.pfail ? kRed : (s.powerWarn ? kAmber : kText2));
  const bool net = s.nanoOnline && s.wifiState == "CONNECTED";
  item(Icon::kWifi, fitText(fontFace(18), wifiLabel(s), 100), net ? kGreen : (s.nanoOnline ? kText2 : kAmber));
  item(Icon::kTag, s.pointId.empty() ? std::string("未选测点") : fitText(fontFace(18), s.pointId, 100),
       s.pointId.empty() ? kText3 : kText);
  if (s.laserOn) item(Icon::kLaser, "激光", kRed, true);
  if (s.pfail) {
    item(Icon::kWarn, "掉电预警", kRed, true);
  } else if (s.alarmLevel == AlarmLevel::kWarn || s.alarmLevel == AlarmLevel::kAlarm) {
    item(s.alarmMuted ? Icon::kBellOff : Icon::kBell, s.alarmLevel == AlarmLevel::kAlarm ? "报警" : "预警",
         levelColor(s.alarmLevel), true);
  }
}

void rail(Ui& ui) {
  static const struct {
    Icon icon;
    const char* label;
  } kNav[kPageCount] = {{Icon::kCamera, "实时"}, {Icon::kChart, "结果"},   {Icon::kVision, "视觉"},
                        {Icon::kHistory, "记录"}, {Icon::kChip, "设备"}, {Icon::kGear, "设置"}};
  Canvas& c = ui.c;
  c.fillRect(0, kBarH, kRailW, kH - kBarH, kRail);
  c.fillRect(kRailW - 1, kBarH, 1, kH - kBarH, kBorder);
  const int h = (kH - kBarH) / kPageCount;
  for (int i = 0; i < kPageCount; ++i) {
    const Rect r{0, kBarH + i * h, kRailW - 1, h};
    const bool active = static_cast<int>(ui.st.page) == i;
    if (active) {
      fillRoundRect(c, r.x + 8, r.y + 8, r.w - 16, r.h - 16, 14, kAccent, 36);
      fillRoundRect(c, 0, r.y + 22, 4, h - 44, 2, kAccent);
    } else if (ui.pressed(r)) {
      fillRoundRect(c, r.x + 8, r.y + 8, r.w - 16, r.h - 16, 14, kCardHi);
    }
    drawIcon(c, kNav[i].icon, kRailW / 2.f, r.y + 36.f, 30.f, active ? kAccent : kText2);
    drawTextCentered(c, fontFace(14), kRailW / 2, r.y + 58, kNav[i].label, active ? kText : kText2);
    ui.hit(r, uiAction("nav", i));
  }
}

// Reason of a tapped grey control: a bubble pointing at it, above when there
// is room, else below.
void reasonBubble(Canvas& c, const Rect& at, const std::string& reason) {
  const FontFace& f = fontFace(18);
  const std::string text = fitText(f, reason, 560);
  const int w = 16 + 20 + 8 + textWidth(f, text) + 16, h = 42;
  const bool above = at.y - h - 10 >= kBarH + 4;
  const int y = above ? at.y - h - 10 : std::min(kH - h - 4, at.y + at.h + 10);
  const int x = std::max(8, std::min(kW - w - 8, at.x + at.w / 2 - w / 2));
  const uint32_t bg = 0x2A394C;
  fillRoundRect(c, x - 2, y + 2, w + 4, h + 4, 14, 0x000000, 90);
  fillRoundRect(c, x, y, w, h, 12, bg);
  const float ax = std::max(x + 18.f, std::min(x + w - 18.f, at.x + at.w / 2.f));
  const float base = above ? static_cast<float>(y + h) : static_cast<float>(y);
  const float tri[6] = {ax - 8, base, ax + 8, base, ax, base + (above ? 8.f : -8.f)};
  fillPolygon(c, tri, 3, bg);
  drawIcon(c, Icon::kWarn, x + 26.f, y + h / 2.f, 20.f, kAmber);
  drawText(c, f, x + 44, textTopIn(f, y, h), text, kText);
}

void toast(Ui& ui, const UiData& d) {
  std::string text;
  uint32_t color = kText;
  if (!ui.st.toast.empty() && d.nowMs < ui.st.toastUntilMs) {
    if (ui.st.toastAt.w > 0) {
      reasonBubble(ui.c, ui.st.toastAt, ui.st.toast);
      return;
    }
    text = ui.st.toast;
    color = kAmber;
  } else if (!d.s.message.empty() && d.nowMs - d.s.messageMs < 6000) {
    text = d.s.message;
  }
  if (text.empty()) return;
  const FontFace& f = fontFace(18);
  text = fitText(f, text, 700);
  const int w = textWidth(f, text) + 40, h = 40;
  const int x = kCx + (kCw - w) / 2, y = kH - h - 18;
  fillRoundRect(ui.c, x, y, w, h, h / 2, 0x05080C, 225);
  strokeRoundRect(ui.c, x, y, w, h, h / 2, 1, kBorder);
  drawText(ui.c, f, x + 20, textTopIn(f, y, h), text, color);
}

void shutdownScreen(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  fillCircle(c, kW / 2.f, 220.f, 64, kCardHi);
  drawIcon(c, Icon::kPower, kW / 2.f, 220.f, 64, kRed);
  drawTextCentered(c, fontFace(32, true), kW / 2, 312, "关机", kText);
  drawTextCentered(c, fontFace(22), kW / 2, 364, d.s.message.empty() ? "等待 Linux 关机" : d.s.message, kText2);
  drawTextCentered(c, fontFace(18), kW / 2, 404, "Linux 关机完成后再断开总电源", kText3);
}

void shutdownDialog(Ui& ui) {
  Canvas& c = ui.c;
  const Rect r{kW / 2 - 250, kH / 2 - 130, 500, 260};
  ui.card(r, kCardHi);
  drawIcon(c, Icon::kPower, r.x + 50.f, r.y + 56.f, 40, kRed);
  drawText(c, fontFace(32, true), r.x + 84, r.y + 34, "关机", kText);
  drawText(c, fontFace(18), r.x + 36, r.y + 100, "将停止测量、关闭补光和外设，并关闭 Linux。", kText2);
  drawText(c, fontFace(18), r.x + 36, r.y + 128, "屏幕提示关机完成后，再断开总电源。", kText2);
  ui.button(Rect{r.x + 36, r.y + 178, 200, 56}, "取消", Icon::kNone, kNeutral, true, uiAction("dialog", 0));
  ui.button(Rect{r.x + r.w - 236, r.y + 178, 200, 56}, "确认关机", Icon::kPower, kRed, true,
            cmdAction("shutdown"));
}

}  // namespace

void renderUi(Canvas& c, const UiState& st, const UiData& d, std::vector<HitRegion>& hits) {
  hits.clear();
  Ui ui(c, st, hits);
  fillGradientV(c, 0, 0, kW, kH, kBgTop, kBg);
  if (d.s.shutdownPrepared) {
    shutdownScreen(ui, d);
    return;
  }
  topBar(ui, d);
  rail(ui);
  switch (st.page) {
    case Page::kLive: renderLivePage(ui, d); break;
    case Page::kResult: renderResultPage(ui, d); break;
    case Page::kVision: renderVisionPage(ui, d); break;
    case Page::kHistory: renderHistoryPage(ui, d); break;
    case Page::kDevice: renderDevicePage(ui, d); break;
    case Page::kSettings: renderSettingsPage(ui, d); break;
  }
  toast(ui, d);
  if (st.dialog != Dialog::kNone) {
    hits.clear();  // modal: nothing underneath reacts
    for (int y = 0; y < kH; ++y) {
      for (int x = 0; x < kW; ++x) c.blend(x, y, 0x000000, 150);
    }
    ui.block(Rect{0, 0, kW, kH});
    if (st.dialog == Dialog::kShutdown) shutdownDialog(ui);
    if (st.dialog == Dialog::kWifi) renderWifiDialog(ui, d);
  }
}

}  // namespace vm
