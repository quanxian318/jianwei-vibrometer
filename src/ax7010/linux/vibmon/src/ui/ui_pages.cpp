// Live and result pages.
#include <algorithm>
#include <cmath>
#include <cstdio>

#include "app/report.h"
#include "gfx/draw.h"
#include "ui/ui_core.h"
#include "ui/demo_sensors.h"

namespace vm {

using namespace theme;

namespace {

constexpr float kPi = 3.14159265f;

// Normalised image window shown at the current display zoom.
void zoomWindow(const SystemState& s, double& x0, double& y0, double& w, double& h) {
  w = 1.0 / std::max(1.0, s.zoom);
  h = w;
  x0 = std::max(0.0, std::min(1.0 - w, s.zoomCx - w / 2));
  y0 = std::max(0.0, std::min(1.0 - h, s.zoomCy - h / 2));
}

// Why a measurement (or a baseline capture) cannot be started or stopped now;
// empty when it can.
std::string measureBlock(const SystemState& s) {
  if (s.shutdownPrepared) return "正在关机";
  if (s.pfail) return "掉电预警中，不能测量";
  if (!s.cameraOk) return "相机未就绪";
  if (s.printerState != "idle") return "正在打印，请稍候";
  if (s.measureState == "analyzing") return "正在计算结果";
  if (s.measureState != "preview" && s.measureState != "capturing") return "暂不能测量";
  return std::string();
}

// Measuring box b clipped to the image area (zoomed in, it may reach past the
// view): optional light fill, dashed or solid 2 px outline, and drag handles
// at the corners (and edge centres once the box is large enough).
void measureBox(Canvas& c, const Rect& clip, const Rect& b, uint32_t color, bool dashed, int fillAlpha,
                bool handles) {
  auto put = [&](int x, int y, int w, int h, int alpha) {
    const int ax = std::max(x, clip.x), ay = std::max(y, clip.y);
    const int bx = std::min(x + w, clip.x + clip.w), by = std::min(y + h, clip.y + clip.h);
    if (bx > ax && by > ay) fillRoundRect(c, ax, ay, bx - ax, by - ay, 0, color, alpha);
  };
  if (fillAlpha > 0) put(b.x, b.y, b.w, b.h, fillAlpha);
  const int step = dashed ? 12 : std::max(b.w, b.h) + 1, len = dashed ? 7 : step;
  for (int i = 0; i < b.w; i += step) {
    put(b.x + i, b.y, std::min(len, b.w - i), 2, 255);
    put(b.x + i, b.y + b.h - 2, std::min(len, b.w - i), 2, 255);
  }
  for (int j = 0; j < b.h; j += step) {
    put(b.x, b.y + j, 2, std::min(len, b.h - j), 255);
    put(b.x + b.w - 2, b.y + j, 2, std::min(len, b.h - j), 255);
  }
  if (!handles) return;
  // Smaller dots on small boxes so the box itself stays visible.
  const float r = std::max(3.f, std::min(6.f, std::min(b.w, b.h) / 6.f));
  auto handle = [&](int x, int y, float hr) {
    if (!clip.contains(x, y)) return;
    fillCircle(c, static_cast<float>(x), static_cast<float>(y), hr + 2.5f, 0x05080C, 160);
    fillCircle(c, static_cast<float>(x), static_cast<float>(y), hr, color);
  };
  const int x1 = b.x + b.w, y1 = b.y + b.h;
  for (int x : {b.x, x1}) {
    for (int y : {b.y, y1}) handle(x, y, r);
  }
  if (b.w >= 56) {
    handle(b.x + b.w / 2, b.y, 0.75f * r);
    handle(b.x + b.w / 2, y1, 0.75f * r);
  }
  if (b.h >= 56) {
    handle(b.x, b.y + b.h / 2, 0.75f * r);
    handle(x1, b.y + b.h / 2, 0.75f * r);
  }
}

// Multi-point layout on the preview: grid templates (green), reference
// templates (amber), the reference group outline while it is being edited,
// a layout problem, and the chip that switches dragging to the references.
void drawMultiPoint(Ui& ui, const UiData& d, const Rect& img, const UiAction& roi, bool editable) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const MeasurementEngine::Live& live = d.live;
  const int fw = live.frameW, fh = live.frameH;
  std::vector<std::array<float, 2>> pts = live.mpPoints, refs = live.mpRefs;
  const bool dragRef = editable && ui.st.refEdit && ui.st.roiGrab != 0;
  if (editable) {
    const bool dragBox = ui.st.roiGrab != 0 && !ui.st.refEdit;
    const NormBox nb = dragBox ? ui.st.roiDrag : roiBox(s, fw, fh);
    const Roi box{static_cast<int>(std::lround(nb.x0 * fw)), static_cast<int>(std::lround(nb.y0 * fh)),
                  static_cast<int>(std::lround((nb.x1 - nb.x0) * fw)), static_cast<int>(std::lround((nb.y1 - nb.y0) * fh))};
    const double rcx = dragRef ? 0.5 * (ui.st.roiDrag.x0 + ui.st.roiDrag.x1) : s.refCx;
    const double rcy = dragRef ? 0.5 * (ui.st.roiDrag.y0 + ui.st.roiDrag.y1) : s.refCy;
    const MultiPointLayout l = multiPointLayout(fw, fh, box, rcx, rcy, d.multipoint);
    pts = l.points;
    refs = l.refs;
  }
  const auto patch = [&](const std::array<float, 2>& p, uint32_t col) {
    const NormBox pb{(p[0] - 12.5) / fw, (p[1] - 12.5) / fh, (p[0] + 12.5) / fw, (p[1] + 12.5) / fh};
    measureBox(c, img, roiScreenRect(pb, roi, img), col, false, 0, false);
  };
  for (const auto& p : pts) patch(p, kGreen);
  for (const auto& p : refs) patch(p, kAmber);
  if (!editable) return;
  if (ui.st.refEdit) {
    const NormBox rb = dragRef ? ui.st.roiDrag : refGroupBox(s, fw, fh, d.multipoint.refPitchPx);
    measureBox(c, img, roiScreenRect(rb, roi, img), kAmber, !dragRef, dragRef ? 40 : 0, false);
  }
  if (!s.layoutError.empty() && s.layoutError != "camera_not_ready") {
    const std::string text = reasonText(s.layoutError);
    const FontFace& f = fontFace(18);
    const int tw = textWidth(f, text) + 28;
    fillRoundRect(c, img.x + img.w / 2 - tw / 2, img.y + 56, tw, 32, 16, kRed, 220);
    drawTextCentered(c, f, img.x + img.w / 2, textTopIn(f, img.y + 56, 32), text, 0xFFFFFF);
  }
}

JsonValue setCmd(const char* key, const JsonValue& v) {
  JsonValue j = JsonValue::makeObject();
  j.set("cmd", "set");
  j.set(key, v);
  return j;
}

// Result summary for the result page, from the live state or a saved record.
struct ResultInfo {
  bool have = false, valid = false, saved = false;
  std::string id, reason, unit = "px", timeText, point;
  std::string method = "roi";
  int points = 0, passed = 0;  // multipoint
  double pointRms = 0;
  bool waveformOk = true;      // multipoint: the representative waveform passed its checks
  std::string waveformReason;
  bool clear = true;           // a clear dominant line (else freq is only the largest bin)
  ResultSummary acoustic;      // acoustic* fields only
  double freq = 0, pp = 0, rms = 0, peak = 0, fps = 0;
  unsigned long dropped = 0;
  AlarmLevel level = AlarmLevel::kUnknown;
  double warnPp = 0, alarmPp = 0;
};

AlarmLevel levelFromKey(const std::string& k) {
  if (k == "alarm") return AlarmLevel::kAlarm;
  if (k == "warn") return AlarmLevel::kWarn;
  if (k == "normal") return AlarmLevel::kNormal;
  return AlarmLevel::kUnknown;
}

ResultInfo fromRecord(const RecordView& v) {
  ResultInfo r;
  if (!v.loaded) return r;
  const JsonValue& rec = v.rec;
  const JsonValue& res = jObj(rec, "result");
  r.have = true;
  r.saved = true;
  r.id = v.id;
  r.valid = jBool(res, "valid", false);
  r.reason = jStr(res, "reason", "");
  r.unit = jStr(res, "unit", "px");
  const bool mm = r.unit == "mm";
  r.freq = jNum(res, "dominant_frequency_hz", 0);
  r.clear = jBool(res, "peak_clear", true);
  r.pp = jNum(res, mm ? "displacement_peak_to_peak_mm" : "displacement_peak_to_peak_px", 0);
  r.rms = jNum(res, mm ? "displacement_rms_mm" : "displacement_rms_px", 0);
  r.peak = jNum(res, mm ? "spectral_peak_mm" : "spectral_peak_px", 0);
  r.level = levelFromKey(jStr(res, "level", "unknown"));
  const JsonValue& th = jObj(res, "thresholds");
  r.warnPp = jNum(th, "warn_peak_to_peak", 0);
  r.alarmPp = jNum(th, "alarm_peak_to_peak", 0);
  r.timeText = jStr(rec, "start_local", "");
  if (r.timeText.empty()) r.timeText = "时间未校准";
  r.point = jStr(rec, "point_id", "");
  const std::string name = jStr(rec, "point_name", "");
  if (!name.empty()) r.point += (r.point.empty() ? "" : " ") + name;
  const JsonValue& cam = jObj(rec, "camera");
  r.fps = jNum(cam, "fps_measured", 0);
  r.dropped = static_cast<unsigned long>(jInt(cam, "frames_dropped", 0));
  ResultSummary mp;
  readMultiPointSummary(res, mp);
  r.method = mp.method;
  r.points = mp.points;
  r.passed = mp.passedPoints;
  r.pointRms = mp.pointRmsPx;
  r.waveformOk = mp.waveformValid;
  r.waveformReason = mp.waveformReason;
  readAcousticSummary(rec, r.acoustic);
  return r;
}

ResultInfo fromState(const SystemState& s) {
  ResultInfo r;
  const ResultSummary& l = s.last;
  if (!l.have) return r;
  r.have = true;
  r.valid = l.valid;
  r.saved = l.saved;
  r.id = l.measurementId;
  r.reason = l.reason;
  r.unit = l.unit;
  r.freq = l.dominantHz;
  r.clear = l.peakClear;
  r.pp = l.peakToPeak;
  r.rms = l.rms;
  r.peak = l.peak;
  r.fps = l.fps;
  r.dropped = l.dropped;
  r.level = l.level;
  r.point = l.pointId;
  r.warnPp = s.settings.warnPp;
  r.alarmPp = s.settings.alarmPp;
  r.method = l.method;
  r.points = l.points;
  r.passed = l.passedPoints;
  r.pointRms = l.pointRmsPx;
  r.waveformOk = l.waveformValid;
  r.waveformReason = l.waveformReason;
  r.acoustic = l;
  return r;
}

// One clause on the acoustic reference for the details line, and its colour.
std::string acousticClause(const ResultSummary& a, uint32_t& col) {
  char b[96];
  col = kText2;
  if (!a.acousticEnabled) return std::string();
  if (!a.acousticValid) {
    col = kText3;
    const std::string& r = a.acousticReason;
    const char* why = r == "gap" ? "数据中断"
                      : r == "late" || r == "not_buffered" || r == "not_ready" ? "数据不全"
                      : r == "stuck" || r == "no_signal" ? "无声音数据"
                      : r == "clipped" ? "削顶"
                      : r == "too_short" ? "时长不足" : "不可用";
    return std::string("声学参考") + why;
  }
  if (a.acousticRelation == "no_peak") {
    col = kText3;
    return "声学包络无明显周期";
  }
  if (a.acousticRelation == "agree") {
    col = kGreen;
    snprintf(b, sizeof(b), "声学包络 %s Hz · 与视觉一致", fmtNum(a.acousticHz, 4).c_str());
  } else if (a.acousticRelation == "harmonic") {
    col = kAmber;
    if (a.acousticHarmonic > 0) {
      snprintf(b, sizeof(b), "声学包络 %s Hz · 视觉频率的 %d 倍", fmtNum(a.acousticHz, 4).c_str(), a.acousticHarmonic);
    } else {
      snprintf(b, sizeof(b), "声学包络 %s Hz · 视觉频率的 1/%d", fmtNum(a.acousticHz, 4).c_str(), -a.acousticHarmonic);
    }
  } else if (a.acousticRelation == "differ") {
    col = kAmber;
    snprintf(b, sizeof(b), "声学包络 %s Hz · 与视觉不同", fmtNum(a.acousticHz, 4).c_str());
  } else {
    snprintf(b, sizeof(b), "声学包络 %s Hz", fmtNum(a.acousticHz, 4).c_str());
  }
  return b;
}

}  // namespace

// ---------------------------------------------------------------------------
// Live page
// ---------------------------------------------------------------------------

namespace {

// Camera card of the live page: the image, the measuring box and everything
// drawn over them. Nothing leaves the card, so it can be redrawn alone
// between full frames (renderLivePreview).
void livePreviewCard(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const MeasurementEngine::Live& live = d.live;
  const bool capturing = s.measureState == "capturing";
  const bool analyzing = s.measureState == "analyzing";
  const bool multipoint = s.method == "multipoint";
  char buf[160];

  // Camera preview.
  const Rect pc = kLiveCameraCard;
  ui.card(pc, 0x05080C);
  const Rect img = fitAspect(Rect{pc.x + 4, pc.y + 4, pc.w - 8, pc.h - 8}, live.frameW, live.frameH);
  double zx, zy, zw, zh;
  zoomWindow(s, zx, zy, zw, zh);
  if (ui.st.panning) {  // the view follows the finger until the pan command is applied
    zx = ui.st.panCx - zw / 2;
    zy = ui.st.panCy - zh / 2;
  }
  if (live.cameraOk && live.preview && live.preview->width > 0) {
    drawPreview(c, img, *live.preview, zx, zy, zw, zh);
    const VisionLive& v = live.vision;
    if (ui.st.liveHeat && v.ew > 0 && v.energy.size() == static_cast<size_t>(v.ew) * v.eh) {
      blitGray(c, img.x, img.y, img.w, img.h, v.energy.data(), v.ew, v.eh, zx * v.ew, zy * v.eh, zw * v.ew,
               zh * v.eh, heatLut(), 120);
    }
    // Measuring box: tracked position while capturing; in preview it can be
    // dragged (inside = move, edges and corners = resize) or moved by a tap.
    UiAction roi = uiAction("roi_box");
    roi.nx0 = zx;
    roi.ny0 = zy;
    roi.nw = zw;
    roi.nh = zh;
    const bool editable = s.measureState == "preview";
    if ((capturing || analyzing) && live.frameW > 0 && live.roi.w > 0) {
      const NormBox tracked{static_cast<double>(live.roi.x) / live.frameW,
                            static_cast<double>(live.roi.y) / live.frameH,
                            static_cast<double>(live.roi.x + live.roi.w) / live.frameW,
                            static_cast<double>(live.roi.y + live.roi.h) / live.frameH};
      measureBox(c, img, roiScreenRect(tracked, roi, img), kAmber, false, 0, false);
    } else if (live.frameW > 0) {
      const bool dragging = editable && ui.st.roiGrab != 0 && !ui.st.refEdit;
      const NormBox nb = dragging ? ui.st.roiDrag : roiBox(s, live.frameW, live.frameH);
      const Rect b = roiScreenRect(nb, roi, img);
      const uint32_t col = dragging ? kAccent : kGreen;
      measureBox(c, img, b, col, !dragging, dragging ? 40 : 0, editable && !ui.st.refEdit);
      const float px = b.x + b.w / 2.f, py = b.y + b.h / 2.f;
      if (img.contains(static_cast<int>(px), static_cast<int>(py))) {
        lineAA(c, px - 16, py, px - 5, py, 2, col);
        lineAA(c, px + 5, py, px + 16, py, 2, col);
        lineAA(c, px, py - 16, px, py - 5, 2, col);
        lineAA(c, px, py + 5, px, py + 16, 2, col);
      }
      if (dragging) {
        snprintf(buf, sizeof(buf), "%ld × %ld px", std::lround((nb.x1 - nb.x0) * live.frameW),
                 std::lround((nb.y1 - nb.y0) * live.frameH));
        const FontFace& f = fontFace(18);
        const int tw = textWidth(f, buf) + 24, th = 32;
        int ly = b.y - th - 8;
        if (ly < img.y + 4) ly = b.y + b.h + 8;
        if (ly + th > img.y + img.h - 4) ly = std::max(img.y + 4, b.y + 4);
        const int lx = std::max(img.x + 4, std::min(img.x + img.w - tw - 4, b.x + b.w / 2 - tw / 2));
        fillRoundRect(c, lx, ly, tw, th, 16, 0x05080C, 210);
        drawTextCentered(c, f, lx + tw / 2, textTopIn(f, ly, th), buf, kText);
      }
    }
    if (multipoint && live.frameW > 0) drawMultiPoint(ui, d, img, roi, editable);
    ui.hit(img, roi, Enable(editable, capturing || analyzing ? "测量中不能移动测点框" : ""));
    if (!editable && s.zoom > 1.01) {  // the zoomed view can be moved while the box is locked
      UiAction pan = uiAction("pan_view");
      pan.nx0 = zx;
      pan.ny0 = zy;
      pan.nw = zw;
      pan.nh = zh;
      ui.hit(img, pan, true);
    }
    // After the image's own hit region, so a tap on it switches what is dragged.
    if (multipoint && editable && live.frameW > 0) {
      ui.chip(Rect{img.x + img.w - 128, img.y + img.h - 44, 120, 34}, "移动参考点", ui.st.refEdit, true,
              uiAction("ref_edit"));
    }
  } else {
    drawIcon(c, Icon::kCamera, pc.x + pc.w / 2.f, pc.y + pc.h / 2.f - 30, 56, kText3);
    drawTextCentered(c, fontFace(22, true), pc.x + pc.w / 2, pc.y + pc.h / 2 + 10,
                     live.cameraOk ? "等待画面" : "相机未就绪", kText2);
    if (!live.error.empty()) {
      drawTextCentered(c, fontFace(14), pc.x + pc.w / 2, pc.y + pc.h / 2 + 46, live.error, kText3);
    }
  }
  // Overlays: state, zoom controls, frame info.
  {
    uint32_t col = kGreen;
    std::string text = "预览";
    if (capturing) {
      col = kRed;
      text = s.baselineCapture ? "基线采集中" : "采集中";
    } else if (analyzing) {
      col = kAccent;
      text = "计算中";
    } else if (!s.cameraOk) {
      col = kText3;
      text = "相机未就绪";
    }
    const FontFace& f = fontFace(18);
    const int w = textWidth(f, text) + 38;
    fillRoundRect(c, pc.x + 12, pc.y + 12, w, 32, 16, 0x05080C, 200);
    fillCircle(c, pc.x + 28.f, pc.y + 28.f, 6, col);
    drawText(c, f, pc.x + 40, textTopIn(f, pc.y + 12, 32), text, kText);
    // Acoustic reference: its level while it runs, otherwise its state (a
    // fault says why when tapped). Nothing while no microphone is connected.
    if (s.acousticState != "disabled" && s.acousticState != "absent") {
      std::string mt = "麦克风启动";
      uint32_t mc = kText2;
      const bool live = s.acousticState == "ready" || s.acousticState == "capturing";
      if (live) {
        snprintf(buf, sizeof(buf), "%.0f dBFS", std::max(-99.0, s.acousticLevelDbfs));
        mt = buf;
        mc = kText;
      } else if (s.acousticState == "fault") {
        mt = "麦克风故障";
        mc = kAmber;
      }
      const Rect mr{pc.x + 12 + w + 8, pc.y + 12, textWidth(f, mt) + 50, 32};
      fillRoundRect(c, mr.x, mr.y, mr.w, mr.h, 16, 0x05080C, 200);
      drawIcon(c, Icon::kMic, mr.x + 24.f, mr.y + 16.f, 18, live ? kGreen : mc);
      drawText(c, f, mr.x + 38, textTopIn(f, mr.y, mr.h), mt, mc);
      if (s.acousticState == "fault") ui.hit(mr, UiAction(), Enable(false, acousticErrorText(s.acousticError)));
    }
  }
  {
    const int bx = pc.x + pc.w - 12 - 3 * 40 - 70;
    const Rect minus{bx, pc.y + 12, 40, 36}, plus{bx + 110, pc.y + 12, 40, 36}, fit{bx + 156, pc.y + 12, 40, 36};
    JsonValue zin = JsonValue::makeObject(), zout = JsonValue::makeObject();
    zin.set("cmd", "zoom");
    zin.set("steps", 1);
    zout.set("cmd", "zoom");
    zout.set("steps", -1);
    ui.iconButton(minus, Icon::kMinus, Enable(s.zoom > 1.01, "已是原始大小"), cmdAction(zout), 0x1A2430);
    fillRoundRect(c, bx + 44, pc.y + 12, 62, 36, 10, 0x05080C, 200);
    snprintf(buf, sizeof(buf), "%.1f×", s.zoom);
    drawTextCentered(c, fontFace(18), bx + 75, textTopIn(fontFace(18), pc.y + 12, 36), buf,
                     s.zoom > 1.01 ? kAccent : kText2);
    ui.iconButton(plus, Icon::kPlus, Enable(s.zoom < 7.99, "已是最大倍率"), cmdAction(zin), 0x1A2430);
    ui.iconButton(fit, Icon::kFit, Enable(s.zoom > 1.01, "已是原始大小"), cmdAction("zoom_reset"), 0x1A2430);
  }
  if (live.frameW > 0) {
    // Frame size and rate; while capturing also the measurement work per frame, amber when it
    // takes longer than a frame interval (frames would be dropped).
    const FontFace& f = fontFace(14);
    snprintf(buf, sizeof(buf), "%d×%d · 接收 %.1f / 预览 %.1f fps", live.frameW, live.frameH, live.fps,
             d.previewDisplayFps);
    const std::string frameInfo = buf;
    std::string work;
    if (capturing && live.procMs > 0) {
      snprintf(buf, sizeof(buf), " · 每帧处理 %.1f ms", live.procMs);
      work = buf;
    }
    const int w = textWidth(f, frameInfo) + textWidth(f, work) + 20;
    fillRoundRect(c, pc.x + 12, pc.y + pc.h - 36, w, 24, 12, 0x05080C, 190);
    const int ty = textTopIn(f, pc.y + pc.h - 36, 24);
    const int x = drawText(c, f, pc.x + 22, ty, frameInfo, kText2);
    drawText(c, f, x, ty, work, live.procMs * live.fps > 1000 ? kAmber : kText2);
  }
}

}  // namespace

void renderLivePreview(Canvas& c, const UiState& st, const UiData& d) {
  // The page background under the card first: its rounded corners then blend
  // as in a full frame.
  const Rect& r = kLiveCameraCard;
  for (int y = r.y; y < r.y + r.h; ++y) c.fillRect(r.x, y, r.w, 1, mixColor(kBgTop, kBg, y * 255 / (kH - 1)));
  std::vector<HitRegion> unused;  // the regions of the last full frame stay in use
  Ui ui(c, st, unused);
  livePreviewCard(ui, d);
}

void renderLivePage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const MeasurementEngine::Live& live = d.live;
  const bool capturing = s.measureState == "capturing";
  const bool analyzing = s.measureState == "analyzing";
  const bool multipoint = s.method == "multipoint";
  char buf[160];

  livePreviewCard(ui, d);

  // Live displacement.
  {
    const Rect wr{kCx, kCy + 356, 600, kCh - 356};
    ui.card(wr);
    std::vector<double> xs, ys;
    const double fps = live.fps > 1 ? live.fps : 30;
    const size_t n = live.recent.size();
    double lo = 0, hi = 0;
    for (size_t i = 0; i < n; ++i) {
      xs.push_back((static_cast<double>(i) - (n - 1)) / fps);
      ys.push_back(live.recent[i]);
      lo = i ? std::min(lo, ys.back()) : ys.back();
      hi = i ? std::max(hi, ys.back()) : ys.back();
    }
    ChartOptions o;
    o.title = "实时位移";
    o.unitY = "px";
    o.unitX = "s";
    o.symmetric = true;
    o.line = kGreen;
    if (n > 1) {
      snprintf(buf, sizeof(buf), "峰峰 %s px", fmtNum(hi - lo, 3).c_str());
      o.rightNote = buf;
    }
    drawChart(c, wr, xs, ys, o);
  }

  // Measurement control.
  const int rx = kCx + 608, rw = kCw - 608;
  {
    const Rect r{rx, kCy, rw, 228};
    ui.card(r);
    std::string why = measureBlock(s);
    if (why.empty() && multipoint && !capturing && !s.baselineValid) {
      why = !s.layoutError.empty() && s.layoutError != "camera_not_ready" ? reasonText(s.layoutError)
                                                                         : "需先在电机停机时采集基线";
    }
    const Enable enabled = unless(why);
    const float cx = r.x + 78.f, cy = r.y + 98.f;
    strokeCircle(c, cx, cy, 62, 6, kNeutral);
    if (capturing && s.measureDurationS > 0) {
      const float frac = static_cast<float>(std::min(1.0, s.measureElapsedS / s.measureDurationS));
      if (frac > 0.002f) strokeArc(c, cx, cy, 62, 6, 0, frac * 2 * kPi, kAccent);
    } else if (analyzing) {
      const float a = (d.nowMs % 1200) / 1200.f * 2 * kPi;
      strokeArc(c, cx, cy, 62, 6, a, a + 1.6f, kAccent);
    }
    const Rect btn{static_cast<int>(cx - 64), static_cast<int>(cy - 64), 128, 128};  // the whole ring reacts
    uint32_t fill = enabled ? (capturing ? kRed : kGreen) : kDisabled;
    if (enabled && ui.pressed(btn)) fill = mixColor(fill, 0xFFFFFF, 50);
    if (enabled) fillCircle(c, cx, cy, 59, fill, 44);  // soft halo inside the progress ring
    fillCircle(c, cx, cy, 52, fill);
    drawIcon(c, capturing ? Icon::kStop : Icon::kPlay, cx + (capturing ? 0 : 4), cy, 42, enabled ? 0xFFFFFF : kText3);
    ui.hit(btn, cmdAction(capturing ? "measure_stop" : "measure_start"), enabled);
    // Text centred on the ring: title, progress or state, and (single box) the calibration.
    const int tx = r.x + 160, tw = r.x + r.w - 12 - tx, ty = r.y + (multipoint ? 62 : 50);
    drawText(c, fontFace(22, true), tx, ty,
             capturing ? (s.baselineCapture ? "停止采集" : "停止测量") : "开始测量", enabled ? kText : kText3);
    std::string line;
    uint32_t lineCol = kText2;
    if (capturing) {
      snprintf(buf, sizeof(buf), "%.1f / %.0f s", s.measureElapsedS, s.measureDurationS);
      line = buf;
      lineCol = kAccent;
    } else if (analyzing) {
      line = "正在计算";
    } else if (multipoint) {
      // Motor-off baseline first; it belongs to this box and reference group.
      line = s.baselineValid ? "停机基线已采集" : "未采集停机基线";
      lineCol = s.baselineValid ? kGreen : kAmber;
    } else {
      line = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(s.settings.axis)))) + " 向位移";
    }
    drawText(c, fontFace(18), tx, ty + 38, fitText(fontFace(18), line, tw), lineCol);
    if (!multipoint) {
      drawText(c, fontFace(14), tx, ty + 68, s.calibrated ? "已标定 · 毫米结果" : "未标定 · 像素结果",
               s.calibrated ? kGreen : kAmber);
    }
    const Enable durOk(!capturing && !analyzing, "测量中不能修改时长");
    static const int kDurRoi[4] = {5, 10, 30, 60}, kDurMp[2] = {10, 30};
    const int* durs = multipoint ? kDurMp : kDurRoi;
    const int nDur = multipoint ? 2 : 4;
    for (int i = 0; i < nDur; ++i) {
      const Rect cr{r.x + 14 + i * 72, r.y + r.h - 46, 66, 34};
      snprintf(buf, sizeof(buf), "%d s", durs[i]);
      ui.chip(cr, buf, std::fabs(s.settings.durationS - durs[i]) < 0.01, durOk,
              cmdAction(setCmd("duration_s", JsonValue(durs[i]))));
    }
    if (multipoint) {
      std::string baseWhy = measureBlock(s);
      if (baseWhy.empty() && capturing) baseWhy = "测量中不能采集基线";
      if (baseWhy.empty() && !s.layoutError.empty()) baseWhy = reasonText(s.layoutError);
      ui.chip(Rect{r.x + 14 + 2 * 72, r.y + r.h - 46, 138, 34}, "采集停机基线", false, unless(baseWhy),
              cmdAction("baseline_start"));
    }
  }

  // Live readouts.
  {
    const Rect r{rx, kCy + 236, rw, 152};
    ui.card(r);
    const int y1 = r.y + 18, y2 = r.y + 84;
    const bool track = capturing && live.frameCount > 0;
    if (multipoint) {
      // Latest 128-frame window of the running capture.
      const MultiPointWindow& w = live.mpWindow;
      const bool sig = track && w.status == 1;
      snprintf(buf, sizeof(buf), "%.2f", w.frequencyHz);
      ui.metric(r.x + 18, y1, "窗口主频", sig ? buf : "--", sig ? "Hz" : "");
      snprintf(buf, sizeof(buf), "%d/%zu", w.passed, live.mpPoints.size());
      ui.metric(r.x + 160, y1, "超过基线", track && w.status >= 0 ? buf : "--", "", sig ? kGreen : kText);
    } else {
      snprintf(buf, sizeof(buf), "%+.3f", live.lastDisp);
      ui.metric(r.x + 18, y1, "位移", track ? buf : "--", "px");
      snprintf(buf, sizeof(buf), "%.3f", live.lastScore);
      ui.metric(r.x + 160, y1, "相关度", track ? buf : "--", "", track && live.lastScore < 0.6 ? kAmber : kText);
    }
    snprintf(buf, sizeof(buf), "%.1f", live.fps);
    ui.metric(r.x + 18, y2, "接收帧率", live.fps > 0 ? buf : "--", "fps");
    if (uiSensorDemoEnabled()) {
      ui.metric(r.x + 160, y2, "工作距离", "200", "mm");
    } else if (s.distanceValid) {
      snprintf(buf, sizeof(buf), "%d", s.distanceMm);
      ui.metric(r.x + 160, y2, "工作距离", buf, "mm");
    } else {
      ui.metric(r.x + 160, y2, "工作距离", "--", "");
    }
  }

  // Quick actions.
  {
    const int y0 = kCy + 396, bw = (rw - 8) / 2, bh = (kCh - 396 - 8) / 2;
    JsonValue light = JsonValue::makeObject();
    light.set("cmd", "light");
    light.set("on", !s.lightOn);
    ui.button(Rect{rx, y0, bw, bh}, s.lightOn ? "关闭照明" : "打开照明", Icon::kBulb, s.lightOn ? kAmber : kNeutral,
              unless(lightBlock(s)), cmdAction(light), 18);
    JsonValue laser = JsonValue::makeObject();
    laser.set("cmd", "laser");
    laser.set("on", !s.laserOn);
    ui.button(Rect{rx + bw + 8, y0, bw, bh}, s.laserOn ? "关闭投影" : "十字投影", Icon::kLaser,
              s.laserOn ? kRed : kNeutral, unless(laserBlock(s)), cmdAction(laser), 18);
    const bool alarm = s.alarmLevel == AlarmLevel::kWarn || s.alarmLevel == AlarmLevel::kAlarm;
    ui.button(Rect{rx, y0 + bh + 8, bw, bh}, "报警静音", Icon::kBellOff, kNeutral,
              unless(!alarm ? "当前没有报警" : (s.alarmMuted ? "报警已静音" : "")), cmdAction("mute"), 18);
    ui.button(Rect{rx + bw + 8, y0 + bh + 8, bw, bh}, ui.st.liveHeat ? "关闭热力" : "振动热力", Icon::kHeat,
              ui.st.liveHeat ? kViolet : kNeutral,
              Enable(live.vision.w > 0, live.cameraOk ? "视觉处理未运行" : "相机未就绪"), uiAction("live_heat"), 18);
  }
}

// ---------------------------------------------------------------------------
// Result page
// ---------------------------------------------------------------------------

void renderResultPage(Ui& ui, const UiData& d) {
  Canvas& c = ui.c;
  const SystemState& s = d.s;
  const bool fromHistory = !ui.st.openRecordId.empty();
  ResultInfo r = fromHistory ? fromRecord(d.record) : fromState(s);
  if (!fromHistory && d.record.loaded && d.record.id == r.id) {
    // The saved record adds the time and the point name.
    const ResultInfo saved = fromRecord(d.record);
    r.timeText = saved.timeText;
    r.point = saved.point;
    r.acoustic = saved.acoustic;
  }
  char buf[200];
  if (!r.have) {
    if (fromHistory) {
      ui.emptyState(Rect{kCx, kCy, kCw, kCh}, Icon::kWarn, "记录无法读取", ui.st.openRecordId);
      ui.button(Rect{kCx + kCw / 2 - 90, kCy + kCh - 90, 180, 56}, "返回记录", Icon::kLeft, kNeutral, true,
                uiAction("close_record"));
    } else {
      ui.emptyState(Rect{kCx, kCy, kCw, kCh}, Icon::kChart, "尚无测量结果", "在实时页点击“开始测量”");
      ui.button(Rect{kCx + kCw / 2 - 90, kCy + kCh - 110, 180, 56}, "去测量", Icon::kPlay, kGreen, true,
                uiAction("nav", static_cast<int>(Page::kLive)));
    }
    return;
  }

  // KPI row.
  const int kw = (kCw - 3 * 8) / 4, kh = 104;
  if (r.valid) {
    // Show the same representative peak-to-peak quantity used by the alarm.
    const bool mp = r.method == "multipoint";
    const char* labels[3] = {"主频", mp ? "超过基线测点" : "峰峰值", mp ? "峰峰值" : "有效值"};
    snprintf(buf, sizeof(buf), "%d/%d", r.passed, r.points);
    const std::string values[3] = {r.clear ? fmtNum(r.freq, 4) : std::string("无明确"),
                                   mp ? std::string(buf) : fmtNum(r.pp, 4),
                                   mp ? (r.waveformOk ? fmtNum(r.pp, 4) : std::string("--")) : fmtNum(r.rms, 3)};
    const std::string units[3] = {r.clear ? "Hz" : "", mp ? "" : r.unit, r.unit};
    static const uint32_t kKpi[3] = {kAccent, kGreen, kViolet};
    for (int i = 0; i < 3; ++i) {
      const Rect k{kCx + i * (kw + 8), kCy, kw, kh};
      ui.card(k);
      fillCircle(c, k.x + 21.f, k.y + 21.f, 4, kKpi[i]);
      drawText(c, fontFace(18), k.x + 32, k.y + 10, labels[i], kText2);
      // The 44 px face is digits and Latin only.
      const bool ascii = std::all_of(values[i].begin(), values[i].end(),
                                     [](char ch) { return static_cast<unsigned char>(ch) < 0x80; });
      const bool fits =
          ascii && textWidth(fontFace(44, true), values[i]) + 6 + textWidth(fontFace(18), units[i]) <= k.w - 28;
      const int vx = drawText(c, fontFace(fits ? 44 : 32, true), k.x + 14, k.y + (fits ? 38 : 48), values[i], kText);
      drawText(c, fontFace(18), vx + 6, k.y + 64, units[i], kText2);
    }
  } else {
    const Rect k{kCx, kCy, 3 * kw + 16, kh};
    ui.card(k);
    fillCircle(c, k.x + 52.f, k.y + kh / 2.f, 30, kAmber, 46);
    drawIcon(c, Icon::kWarn, k.x + 52.f, k.y + kh / 2.f, 34, kAmber);
    drawText(c, fontFace(32, true), k.x + 100, k.y + 14, "结果无效", kAmber);
    drawText(c, fontFace(22), k.x + 100, k.y + 60, fitText(fontFace(22), reasonText(r.reason), k.w - 116), kText);
  }
  {
    const Rect k{kCx + 3 * (kw + 8), kCy, kw, kh};
    const uint32_t col = levelColor(r.level);
    ui.card(k);
    drawText(c, fontFace(18), k.x + 16, k.y + 10, "判级", kText2);
    std::string l1, l2;
    if (r.unit == "mm" && r.alarmPp > 0) {
      l1 = "预警 " + fmtNum(r.warnPp, 3);
      l2 = "报警 " + fmtNum(r.alarmPp, 3) + " mm";
    } else {
      l1 = r.method == "multipoint" ? "像素结果" : "未标定";
      l2 = "不判级";
    }
    const FontFace& f14 = fontFace(14);
    const int lw = std::max(textWidth(f14, l1), textWidth(f14, l2));
    const int bw = std::max(80, std::min(116, k.w - 16 - 14 - lw - 14));
    const FontFace& lf = textWidth(fontFace(32, true), levelText(r.level)) <= bw - 12 ? fontFace(32, true)
                                                                                    : fontFace(22, true);
    fillRoundRect(c, k.x + 16, k.y + 42, bw, 46, 12, col, r.level == AlarmLevel::kUnknown ? 60 : 255);
    drawTextCentered(c, lf, k.x + 16 + bw / 2, textTopIn(lf, k.y + 42, 46), levelText(r.level),
                     r.level == AlarmLevel::kUnknown ? kText2 : 0xFFFFFF);
    drawText(c, f14, k.x + 16 + bw + 14, k.y + 46, l1, kText2);
    drawText(c, f14, k.x + 16 + bw + 14, k.y + 66, l2, kText2);
  }

  // Details line.
  {
    std::string line = r.timeText.empty() ? std::string() : r.timeText + "  ·  ";
    line += "测点 " + (r.point.empty() ? std::string("未选") : r.point);
    if (r.dropped > 0) {
      snprintf(buf, sizeof(buf), "  ·  丢帧 %lu", r.dropped);
      line += buf;
      if (r.valid && (r.method != "multipoint" || r.waveformOk)) line += " (估计)";
    }
    if (r.unit != "mm" && r.method != "multipoint") line += "  ·  未标定，结果为像素";
    if (r.method == "multipoint" && !r.waveformOk) line += "  ·  代表测点波形无效：" + reasonText(r.waveformReason);
    uint32_t acol;
    const std::string ac = acousticClause(r.acoustic, acol);
    const FontFace& f14 = fontFace(14);
    const int aw = ac.empty() ? 0 : textWidth(f14, "  ·  " + ac);
    const int x = drawText(c, f14, kCx + 4, kCy + kh + 8, fitText(f14, line, kCw - 8 - aw), kText2);
    if (!ac.empty()) {
      const int x2 = drawText(c, f14, x, kCy + kh + 8, "  ·  ", kText2);
      drawText(c, f14, x2, kCy + kh + 8, ac, acol);
    }
  }

  // Waveform.
  const RecordView& rv = d.record;
  const bool haveData = rv.loaded && rv.id == r.id;
  {
    const Rect w{kCx, kCy + kh + 34, kCw, 196};
    ui.card(w);
    ChartOptions o;
    o.title = r.method == "multipoint" ? "代表测点位移" : "位移波形";
    o.unitY = r.unit;
    o.unitX = "s";
    o.line = kGreen;
    o.symmetric = false;
    static const std::vector<double> kEmpty;
    drawChart(c, w, haveData ? rv.t : kEmpty, haveData ? rv.disp : kEmpty, o);
  }
  // Spectrum and actions.
  {
    const int y = kCy + kh + 34 + 204;
    const Rect sp{kCx, y, kCw - 174, kCh - (y - kCy)};
    ui.card(sp);
    // The acoustic envelope spectrum of the same window, when the record has one.
    const bool acoustic = haveData && !rv.af.empty() && rv.af.size() == rv.aamp.size();
    const bool showAcoustic = acoustic && ui.st.resultChart == 1;
    ChartOptions o;
    std::vector<double> f, a;
    if (showAcoustic) {
      o.title = "声学包络谱";
      o.unitY = "相对";
      o.unitX = "Hz";
      o.fromZero = true;
      o.fill = true;
      o.line = kViolet;
      f = rv.af;
      a = rv.aamp;
      if (r.acoustic.acousticValid && r.acoustic.acousticHz > 0) {
        o.markerX = r.acoustic.acousticHz;
        o.markerLabel = "声学 " + fmtNum(r.acoustic.acousticHz, 4) + " Hz";
      }
      if (r.valid && r.clear && r.freq > 0) {
        o.refX = r.freq;
        o.refLabel = "视觉 " + fmtNum(r.freq, 4) + " Hz";
      }
    } else {
      o.title = "幅值谱";
      o.unitY = r.unit;
      o.unitX = "Hz";
      o.fromZero = true;
      o.fill = true;
      if (haveData) {
        const double fmax = r.fps > 0 ? std::min(30.0, r.fps * 0.5) : 30.0;  // physical Nyquist limit
        for (size_t i = 0; i < rv.f.size() && i < rv.amp.size(); ++i) {
          if (rv.f[i] > fmax) break;
          f.push_back(rv.f[i]);
          a.push_back(rv.amp[i]);
        }
      }
      if (r.valid && r.freq > 0) {
        o.markerX = r.freq;
        if (!r.clear) {
          snprintf(buf, sizeof(buf), "最大峰 %s Hz · 不明确", fmtNum(r.freq, 4).c_str());
        } else if (!r.waveformOk) {
          snprintf(buf, sizeof(buf), "%s Hz", fmtNum(r.freq, 4).c_str());
        } else {
          snprintf(buf, sizeof(buf), "%s Hz · %s %s", fmtNum(r.freq, 4).c_str(), fmtNum(r.peak, 3).c_str(),
                   r.unit.c_str());
        }
        o.markerLabel = buf;
      }
    }
    drawChart(c, sp, f, a, o);
    if (acoustic) {
      ui.chip(Rect{sp.x + sp.w - 2 * 104 - 10, sp.y + 6, 98, 28}, "位移谱", !showAcoustic, true,
              uiAction("result_chart", 0));
      ui.chip(Rect{sp.x + sp.w - 104 - 10, sp.y + 6, 98, 28}, "声学包络", showAcoustic, true, uiAction("result_chart", 1));
    }
    const int bx = kCx + kCw - 166, bh = (sp.h - 8) / 2;
    std::string printWhy;
    if (!r.saved) {
      printWhy = "结果未保存，不能打印";
    } else if (s.pfail) {
      printWhy = "掉电预警中";
    } else if (s.measureState == "capturing" || s.measureState == "analyzing") {
      printWhy = "测量中不能打印";
    } else if (s.printerState != "idle") {
      printWhy = "正在打印";
    }
    JsonValue pr = JsonValue::makeObject();
    pr.set("cmd", "print");
    pr.set("measurement_id", r.id);
    ui.button(Rect{bx, y, 166, bh}, s.printerState == "idle" ? "打印报告" : "打印中", Icon::kPrinter, kAccent,
              unless(printWhy), cmdAction(pr), 18);
    if (fromHistory) {
      ui.button(Rect{bx, y + bh + 8, 166, bh}, "返回记录", Icon::kLeft, kNeutral, true, uiAction("close_record"), 18);
    } else {
      ui.button(Rect{bx, y + bh + 8, 166, bh}, "再测一次", Icon::kPlay, kNeutral, true,
                uiAction("nav", static_cast<int>(Page::kLive)), 18);
    }
  }
}

}  // namespace vm
