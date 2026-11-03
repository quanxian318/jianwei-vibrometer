// Renders every UI page, the dialogs and a printed slip off-screen and checks
// that touches map to the right actions. With an output directory argument
// the images are written as PPM for visual review.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <random>
#include <string>

#include "app/report.h"
#include "gfx/font.h"
#include "gfx/draw.h"
#include "test_util.h"
#include "ui/fb_ui.h"
#include "ui/ui_core.h"

using namespace vm;

namespace {

void writePpm(const Canvas& c, const std::string& path) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", c.width(), c.height());
  for (int y = 0; y < c.height(); ++y) {
    for (int x = 0; x < c.width(); ++x) {
      const uint32_t p = c.get(x, y);
      const unsigned char rgb[3] = {static_cast<unsigned char>(p >> 16), static_cast<unsigned char>(p >> 8),
                                    static_cast<unsigned char>(p)};
      fwrite(rgb, 1, 3, f);
    }
  }
  fclose(f);
}

// Textured scene with a bright beam that moves vertically by `dy` pixels.
std::vector<uint8_t> scene(int w, int h, double dy) {
  std::vector<uint8_t> img(static_cast<size_t>(w) * h);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      double v = 70 + 25 * std::sin(x * 0.11) * std::cos(y * 0.07) + 12 * std::sin((x + 2 * y) * 0.031);
      const double by = h * 0.45 + dy;
      if (x > w * 0.25 && x < w * 0.8 && std::fabs(y - by) < h * 0.05) v = 200 + 30 * std::sin(x * 0.4);
      if (std::hypot(x - w * 0.18, y - h * 0.7) < h * 0.12) v = 150 + 40 * std::cos(x * 0.5);
      img[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(std::max(0.0, std::min(255.0, v)));
    }
  }
  return img;
}

// Topmost action at (x, y) after rendering st/d.
UiAction tap(const UiState& st, const UiData& d, int x, int y, Rect* r = nullptr) {
  Canvas c(1024, 600);
  std::vector<HitRegion> hits;
  renderUi(c, st, d, hits);
  return hitTest(hits, x, y, r);
}

std::string cmdOf(const UiAction& a) { return a.kind == "cmd" ? jStr(a.cmd, "cmd", "") : std::string(); }

// A grey control: the tap only shows why.
bool refused(const UiAction& a) { return a.kind == "why" && !a.text.empty(); }

}  // namespace

int main(int argc, char** argv) {
  const std::string out = argc > 1 ? argv[1] : "";
  {
    // Display-only spatial averaging: a checkerboard must not alias into
    // coloured snow; source data stays byte-for-byte unchanged.
    std::vector<uint8_t> bgr(8 * 8 * 3);
    for (int y = 0; y < 8; ++y)
      for (int x = 0; x < 8; ++x)
        for (int k = 0; k < 3; ++k) bgr[3 * (y * 8 + x) + k] = (x + y) % 2 ? 240 : 16;
    const auto before = bgr;
    Canvas dst(4, 4);
    blitBgr(dst, 0, 0, 4, 4, bgr.data(), 8, 8, 0, 0, 8, 8);
    for (int y = 0; y < 4; ++y)
      for (int x = 0; x < 4; ++x) CHECK(dst.get(x, y) == 0x808080);
    CHECK(bgr == before);
    Canvas clipped(3, 3);
    blitBgr(clipped, -1, -1, 4, 4, bgr.data(), 8, 8, 0, 0, 8, 8);
    CHECK(clipped.get(0, 0) == 0x808080);
    const uint8_t red[] = {0, 0, 255};
    blitBgr(clipped, 0, 0, 3, 3, red, 1, 1, 0, 0, 1, 1);
    CHECK(clipped.get(2, 2) == 0xff0000);
  }

  // ---------------------------------------------------------------- sample data
  UiData d;
  SystemState& s = d.s;
  s.initDone = s.carrierOk = s.tcaOk = s.inaOk = s.rtcOk = true;
  s.timeValid = true;
  s.timeSource = "ntp";
  s.nanoOnline = true;
  s.wifiState = "CONNECTED";
  s.wifiSsid = "Lab-5G";
  s.wifiIp = "192.168.1.57";
  s.sdMounted = s.nfcReady = true;
  s.power.busV = 12.05;
  s.power.currentA = 2.31;
  s.power.powerW = 27.8;
  s.pointId = "P-01";
  s.pointName = "电机A";
  s.lightAllowed = true;
  s.lightOn = true;
  s.laserOn = true;
  s.cameraOk = true;
  s.envValid = true;
  s.tempC = 24.6;
  s.rhPercent = 48;
  s.distanceValid = true;
  s.distanceMm = 512;
  s.imuOk = true;
  s.tofOk = true;
  s.measureState = "capturing";
  s.measureElapsedS = 3.4;
  s.measureDurationS = 10;
  s.zoom = 1.5;
  s.calibrated = true;
  s.calibrationId = "CAL-01";
  s.deviceId = "vibmon-01";
  s.appVersion = "vibmon 0.2.0";
  s.plVersion = "carrier_ctrl v1.0 · 100 MHz";
  s.nanoVersion = "vibmon-nano 1.0.0";
  s.cameraSource = "v4l2 /dev/video0";
  s.imuEnabled = s.tofEnabled = s.shtEnabled = s.printerEnabled = true;
  s.powerWarnA = 4.2;
  s.powerDenyA = 4.5;
  s.nanoRequests = 1234;
  s.fanOn = true;
  s.acousticState = "capturing";
  s.acousticLevelDbfs = -38.4;
  s.acousticRate = 31250;
  s.acousticLr = "GND";
  s.last.have = s.last.valid = s.last.saved = true;
  s.last.unit = "mm";
  s.last.dominantHz = 12.48;
  s.last.peakToPeak = 0.1234;
  s.last.rms = 0.0431;
  s.last.peak = 0.061;
  s.last.fps = 59.9;
  s.last.level = AlarmLevel::kWarn;
  s.alarmLevel = AlarmLevel::kWarn;
  s.last.measurementId = "vibmon-01-20261020-130709";
  s.last.pointId = "P-01";
  d.nowMs = 100000;
  d.wallMs = 1792561629000LL;

  MeasurementEngine::Live& live = d.live;
  live.cameraOk = true;
  live.fps = 59.9;
  live.frameW = 640;
  live.frameH = 480;
  {
    // Colour camera image (BGR, as the PL cameras store it) with a warm tint: red > green > blue.
    auto p = std::make_shared<PreviewImage>();
    p->width = 640;
    p->height = 480;
    p->frame = 1;
    const std::vector<uint8_t> g = scene(640, 480, 0);
    p->bgr.resize(3 * g.size());
    for (size_t i = 0; i < g.size(); ++i) {
      p->bgr[3 * i] = static_cast<uint8_t>(g[i] * 0.6);
      p->bgr[3 * i + 1] = static_cast<uint8_t>(g[i] * 0.85);
      p->bgr[3 * i + 2] = g[i];
    }
    live.preview = p;
  }
  live.roi = Roi{288, 208, 64, 64};
  live.frameCount = 200;
  live.lastDisp = 1.234;
  live.lastScore = 0.973;
  live.procMs = 0.9;
  for (int i = 0; i < 400; ++i) live.recent.push_back(static_cast<float>(2 * std::sin(i * 2 * M_PI * 12.48 / 59.9)));

  // Real vision output: 90 frames of a 5 Hz vertical motion through the PS chain.
  {
    VisionPipeline vp;
    VisionConfig vc;
    vc.lowHz = 3;
    vc.highHz = 8;
    vp.configure(vc);
    for (int i = 0; i < 90; ++i) {
      const std::vector<uint8_t> f = scene(640, 480, 2.0 * std::sin(2 * M_PI * 5 * i / 60.0));
      vp.process(f.data(), 640, 480, static_cast<int64_t>(i) * 1000000000LL / 60);
    }
    live.vision = vp.live();
    CHECK(live.vision.w == 160 && live.vision.h == 120);
    CHECK(live.vision.ew == 80 && live.vision.eh == 60);
    CHECK(live.vision.settled);
    // The strongest band energy lies on the moving beam (rows 0.4..0.5 of the image).
    CHECK(live.vision.peakY >= 120 * 0.33 && live.vision.peakY <= 120 * 0.58);
    CHECK(live.vision.peakX >= 160 * 0.2 && live.vision.peakX <= 160 * 0.85);
  }
  s.settings.visionLowHz = 2;
  s.settings.visionHighHz = 5;

  // Stereo depth: a plane at 0.9..1.3 m with a box at 0.6 m and an invalid strip.
  {
    auto dm = std::make_shared<DepthMap>();
    dm->w = 320;
    dm->h = 240;
    dm->backend = "PS";
    dm->fps = 15;
    dm->procMs = 41.5;
    dm->validFraction = 0.94;
    dm->mm.resize(320 * 240);
    for (int y = 0; y < 240; ++y) {
      for (int x = 0; x < 320; ++x) {
        uint16_t z = static_cast<uint16_t>(900 + x * 400 / 320);
        if (x > 120 && x < 220 && y > 80 && y < 170) z = 600;
        if (x < 18) z = 0;
        dm->mm[y * 320 + x] = z;
      }
    }
    dm->left = scene(320, 240, 0);
    d.depth = dm;
  }

  // History list and the record shown on the result page.
  for (int i = 0; i < 11; ++i) {
    RecordSummary r;
    char id[64];
    snprintf(id, sizeof(id), "vibmon-01-20261020-1%02d709", 30 - i);
    r.id = id;
    char t[32];
    snprintf(t, sizeof(t), "2026-10-20 1%d:%02d:09", 3 - i / 6, 59 - i * 5);
    r.startLocal = t;
    r.pointId = i % 3 ? "P-01" : "P-02";
    r.pointName = i % 3 ? "电机A" : "风机B";
    r.valid = i != 2;
    r.reason = "tracking_lost";
    r.unit = "mm";
    r.dominantHz = 12.48 - i * 0.3;
    r.peakToPeak = 0.12 + i * 0.4;
    r.rms = 0.04 + i * 0.13;
    r.level = !r.valid ? AlarmLevel::kUnknown : (r.peakToPeak >= 5 ? AlarmLevel::kAlarm : (r.peakToPeak >= 2 ? AlarmLevel::kWarn : AlarmLevel::kNormal));
    r.peakClear = i != 4;   // no clear line: frequency shown as unknown
    r.amplitudes = i != 5;  // multi-point waveform failed: amplitudes unknown
    d.records.push_back(r);
  }
  d.record.id = s.last.measurementId;
  d.record.loaded = true;
  {
    const std::string js =
        "{\"start_local\":\"2026-10-20 13:07:09\",\"point_id\":\"P-01\",\"point_name\":\"电机A\","
        "\"camera\":{\"fps_measured\":59.9,\"frames_dropped\":0,\"frame_interval_jitter_ms\":0.42},"
        "\"quality\":{\"min_ncc\":0.973},\"calibration\":{\"id\":\"CAL-01\"},"
        "\"acoustic\":{\"available\":true,\"valid\":true,\"visual\":{\"relation\":\"agree\","
        "\"visual_hz\":12.48,\"acoustic_hz\":12.46,\"harmonic\":0}},"
        "\"result\":{\"valid\":true,\"unit\":\"mm\",\"dominant_frequency_hz\":12.48,"
        "\"displacement_peak_to_peak_mm\":0.1234,\"displacement_rms_mm\":0.0431,\"spectral_peak_mm\":0.061,"
        "\"level\":\"warn\",\"thresholds\":{\"warn_peak_to_peak\":0.1,\"alarm_peak_to_peak\":0.5}}}";
    std::string err;
    CHECK(vp::jsonParse(js, d.record.rec, &err));
  }
  for (int i = 0; i < 600; ++i) {
    d.record.t.push_back(i / 59.9);
    d.record.disp.push_back(0.06 * std::sin(2 * M_PI * 12.48 * i / 59.9) + 0.004 * std::sin(i * 0.9));
  }
  for (int k = 0; k < 300; ++k) {
    d.record.f.push_back(k * 0.1);
    d.record.amp.push_back(0.061 * std::exp(-std::pow((k * 0.1 - 12.48) / 0.25, 2)) + 0.0015 * std::fabs(std::sin(k * 1.7)));
  }
  for (int k = 0; k <= 950; ++k) {  // acoustic envelope 5..100 Hz: the turning frequency and its second harmonic
    const double f = 5 + k * 0.1;
    d.record.af.push_back(f);
    d.record.aamp.push_back(std::exp(-std::pow((f - 12.46) / 0.3, 2)) + 0.35 * std::exp(-std::pow((f - 24.92) / 0.3, 2)) +
                            0.04 * std::fabs(std::sin(k * 0.77)));
  }

  // ------------------------------------------------------------- interactions
  UiState st;
  // Measure button: stop while capturing, start in preview, grey (with a reason) while analysing or printing.
  const int bx = 100 + 608 + 78, by = 56 + 98;
  CHECK(cmdOf(tap(st, d, bx, by)) == "measure_stop");
  UiData pd = d;
  pd.s.measureState = "preview";
  CHECK(cmdOf(tap(st, pd, bx, by)) == "measure_start");
  pd.s.printerState = "printing";
  CHECK(refused(tap(st, pd, bx, by)));
  pd.s.printerState = "idle";
  pd.s.measureState = "analyzing";
  CHECK(refused(tap(st, pd, bx, by)));
  // Preview tap maps through the display zoom to normalised image coordinates.
  pd.s.measureState = "preview";
  pd.s.zoom = 2;
  {
    Rect r;
    const UiAction a = tap(st, pd, 105 + 76, 61 + 1, &r);  // image starts at (104 + 76, 60) for 4:3 frames
    CHECK(a.kind == "roi_box");
    UiState s2;
    const JsonValue cmd = FbUi::applyAction(s2, a, r, r.x, r.y, pd, 0);
    CHECK(jStr(cmd, "cmd", "") == "roi");
    CHECK_NEAR(jNum(cmd, "x", 0), 0.25, 0.01);
    CHECK_NEAR(jNum(cmd, "y", 0), 0.25, 0.01);
  }
  // Measuring box: inside moves it, edges and corners (also from just outside) resize it.
  {
    Rect r;
    const UiAction a = tap(st, pd, 400, 230, &r);
    CHECK(a.kind == "roi_box");
    const NormBox nb = roiBox(pd.s, 640, 480);
    const Rect b = roiScreenRect(nb, a, r);
    CHECK(b.w > 80 && b.w < 100);  // 64 px at zoom 2 in the 453 px wide image
    CHECK(roiGrabAt(b, b.x + b.w / 2, b.y + b.h / 2) == kGrabMove);
    CHECK(roiGrabAt(b, b.x - 10, b.y - 10) == (kGrabLeft | kGrabTop));
    CHECK(roiGrabAt(b, b.x + b.w + 10, b.y + b.h / 2) == kGrabRight);
    CHECK(roiGrabAt(b, b.x + b.w / 2, b.y + b.h - 3) == kGrabBottom);
    CHECK(roiGrabAt(b, b.x - 40, b.y + b.h / 2) == 0);
    // Moving keeps the size and stops at the image border.
    const NormBox m = roiDragged(nb, kGrabMove, 0.1, -2, 640, 480, kRoiMaxPx);
    CHECK_NEAR(m.x0, nb.x0 + 0.1, 1e-9);
    CHECK_NEAR(m.y0, 0, 1e-9);
    CHECK_NEAR(m.y1 - m.y0, nb.y1 - nb.y0, 1e-9);
    // Resizing keeps the opposite edges and stays within 16 px..the tracker's limit.
    const NormBox g = roiDragged(nb, kGrabRight | kGrabBottom, 1, 0.05, 640, 480, kRoiMaxPx);
    CHECK_NEAR(g.x0, nb.x0, 1e-9);
    CHECK_NEAR(g.y0, nb.y0, 1e-9);
    CHECK_NEAR((g.x1 - g.x0) * 640, kRoiMaxPx, 1e-6);
    CHECK_NEAR(g.y1, nb.y1 + 0.05, 1e-9);
    const NormBox t = roiDragged(nb, kGrabLeft, 1, 0, 640, 480, kRoiMaxPx);
    CHECK_NEAR(t.x1, nb.x1, 1e-9);
    CHECK_NEAR((t.x1 - t.x0) * 640, kRoiMinPx, 1e-6);
    const NormBox lk = roiDragged(nb, kGrabRight, 1, 0, 640, 480, kRoiMaxPxLk);
    CHECK_NEAR((lk.x1 - lk.x0) * 640, kRoiMaxPxLk, 1e-6);
    const JsonValue cmd = roiCommand(g, 640, 480);
    CHECK(jStr(cmd, "cmd", "") == "roi" && jInt(cmd, "w", 0) == 256 && jInt(cmd, "h", 0) == 88);
    CHECK_NEAR(jNum(cmd, "x", 0), 0.5 * (g.x0 + g.x1), 1e-9);
    CHECK_NEAR(jNum(cmd, "y", 0), 0.5 * (g.y0 + g.y1), 1e-9);
    // The live page draws the box being dragged, not the stored one.
    Canvas cv(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(cv, st, pd, hits);
    CHECK(cv.get(b.x + 27, b.y) == theme::kGreen);
    UiState ds;
    ds.roiGrab = kGrabRight | kGrabBottom;
    ds.roiDrag = g;
    renderUi(cv, ds, pd, hits);
    const Rect gb = roiScreenRect(g, a, r);
    CHECK(cv.get(gb.x + gb.w / 4, gb.y) == theme::kAccent);
    CHECK(cv.get(gb.x + gb.w / 4, gb.y + gb.h - 1) == theme::kAccent);
  }
  // No ROI changes during a capture: a tap reaches the view panning (zoomed in) or the reason, neither sends anything.
  {
    const UiAction a = tap(st, d, 400, 230);
    UiState s2;
    CHECK(a.kind == "pan_view" && FbUi::applyAction(s2, a, Rect(), 400, 230, d, 0).isNull());
    UiData dz = d;
    dz.s.zoom = 1;
    const UiAction b = tap(st, dz, 400, 230);
    CHECK(refused(b) && FbUi::applyAction(s2, b, Rect(), 400, 230, dz, 0).isNull());
  }
  // Zoomed in, the view can be moved (also during a capture) and stays inside the image.
  {
    UiData zd = d;
    zd.s.zoom = 2;
    Rect r;
    const UiAction a = tap(st, zd, 400, 230, &r);
    CHECK(a.kind == "pan_view");
    CHECK_NEAR(a.nx0, 0.25, 1e-9);
    CHECK_NEAR(a.nw, 0.5, 1e-9);
    double cx = 0, cy = 0;
    pannedCentre(a, r, r.w / 4, 0, cx, cy);  // finger to the right: the view moves left
    CHECK_NEAR(cx, 0.5 - static_cast<double>(r.w / 4) / r.w * 0.5, 1e-9);
    CHECK_NEAR(cy, 0.5, 1e-9);
    pannedCentre(a, r, -10 * r.w, 10 * r.h, cx, cy);
    CHECK_NEAR(cx, 0.75, 1e-9);
    CHECK_NEAR(cy, 0.25, 1e-9);
    // While the finger is down the page shows the dragged window.
    UiState ps;
    ps.panning = true;
    ps.panCx = 0.3;
    ps.panCy = 0.6;
    const UiAction b = tap(ps, zd, 400, 230);
    CHECK_NEAR(b.nx0, 0.05, 1e-9);
    CHECK_NEAR(b.ny0, 0.35, 1e-9);
  }
  // Lighting (LED1) and the laser projector (LED2) are separate switches.
  {
    const UiAction a = tap(st, d, 708 + 158 + 40, 56 + 396 + 20);
    CHECK(cmdOf(a) == "laser" && !jBool(a.cmd, "on", true));
    const UiAction l = tap(st, d, 708 + 40, 56 + 396 + 20);
    CHECK(cmdOf(l) == "light" && !jBool(l.cmd, "on", true));
    UiData locked = d;
    locked.s.measureState = "capturing";
    locked.s.laserOn = false;
    locked.s.settings.laserOffDuringMeasure = true;
    CHECK(refused(tap(st, locked, 708 + 154 + 30, 56 + 396 + 20)));
    locked.s.measureState = "analyzing";
    CHECK(tap(st, locked, 708 + 154 + 30, 56 + 396 + 20).text == "测量期间投影保持关闭");
    locked.s.measureState = "preview";
    CHECK(cmdOf(tap(st, locked, 708 + 154 + 30, 56 + 396 + 20)) == "laser");
  }
  // Navigation rail.
  {
    const UiAction a = tap(st, d, 40, 48 + 3 * 92 + 40);
    CHECK(a.kind == "nav" && a.arg == 3);
    UiState s2;
    FbUi::applyAction(s2, a, Rect(), 0, 0, d, 0);
    CHECK(s2.page == Page::kHistory);
  }
  // Result page: printing needs a saved record, the preview state and an idle printer.
  {
    UiState rs;
    rs.page = Page::kResult;
    UiData rd = d;
    rd.s.measureState = "preview";
    const int px = 100 + 916 - 166 + 40, py = 56 + 104 + 34 + 204 + 30;
    const UiAction a = tap(rs, rd, px, py);
    CHECK(cmdOf(a) == "print" && jStr(a.cmd, "measurement_id", "") == rd.s.last.measurementId);
    rd.s.measureState = "capturing";
    CHECK(refused(tap(rs, rd, px, py)));
  }
  // Result page: with a saved envelope spectrum a chip switches the chart to it.
  {
    UiState rs;
    rs.page = Page::kResult;
    UiData rd = d;
    rd.s.measureState = "preview";
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, rs, rd, hits);
    UiAction chip;
    Rect cr;
    for (const HitRegion& h : hits) {
      if (h.a.kind == "result_chart" && h.a.arg == 1) {
        chip = h.a;
        cr = h.r;
      }
    }
    CHECK(chip.kind == "result_chart");
    FbUi::applyAction(rs, chip, cr, 0, 0, rd, 0);
    CHECK(rs.resultChart == 1);
    rd.record.af.clear();
    rd.record.aamp.clear();
    renderUi(c, rs, rd, hits);
    bool any = false;
    for (const HitRegion& h : hits) any = any || h.a.kind == "result_chart";
    CHECK(!any);
  }
  // No clear dominant line: the result page says so (from the live summary
  // and from the saved record) and the vision page does not follow it.
  {
    UiState rs;
    rs.page = Page::kResult;
    UiData ud = d;
    ud.s.measureState = "preview";
    ud.s.last.peakClear = false;
    // What such a capture looks like: a noise-like waveform and spectrum
    // whose largest bin is only a few times the median.
    std::mt19937 rng(7);
    std::normal_distribution<double> noise(0.0, 1.0);
    for (double& v : ud.record.disp) v = 0.02 * noise(rng);
    size_t top = 0;
    for (size_t k = 0; k < ud.record.amp.size(); ++k) {
      ud.record.amp[k] = 0.002 * std::fabs(noise(rng));
      if (ud.record.f[k] > 2 && ud.record.amp[k] > ud.record.amp[top]) top = k;
    }
    ud.s.last.dominantHz = ud.record.f[top];
    ud.s.last.peak = ud.record.amp[top];
    ud.s.last.peakToPeak = 0.11;
    ud.s.last.rms = 0.02;
    JsonValue ac = jObj(ud.record.rec, "acoustic");  // no visual frequency to compare with
    JsonValue vis = jObj(ac, "visual");
    vis.set("relation", "no_visual");
    ac.set("visual", vis);
    ud.record.rec.set("acoustic", ac);
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, rs, ud, hits);
    if (!out.empty()) writePpm(c, out + "/ui_result_unclear.ppm");
    JsonValue res = jObj(ud.record.rec, "result");
    res.set("peak_clear", false);
    ud.record.rec.set("result", res);
    rs.openRecordId = ud.record.id;
    renderUi(c, rs, ud, hits);
    UiState vs;
    vs.page = Page::kVision;
    vs.visionView = 1;
    renderUi(c, vs, ud, hits);
    bool refusedFollow = false;
    for (const HitRegion& h : hits) refusedFollow = refusedFollow || (!h.enabled && h.why == "上次测量没有明确主频");
    CHECK(refusedFollow);
  }
  // A microphone fault: the pill on the live page says why when tapped.
  {
    UiState ms;
    UiData md = d;
    md.s.measureState = "preview";
    md.s.acousticState = "fault";
    md.s.acousticError = "no PDM data while the clock is low (L/R = GND set)";
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, ms, md, hits);
    Rect pr;
    for (const HitRegion& h : hits) {
      if (!h.enabled && h.why == "所设相位无数据，检查 L/R") pr = h.r;
    }
    CHECK(pr.w > 0);
    Rect r;
    const UiAction a = tap(ms, md, pr.x + pr.w / 2, pr.y + pr.h / 2, &r);
    CHECK(refused(a) && a.text == "所设相位无数据，检查 L/R");
    CHECK(FbUi::applyAction(ms, a, r, pr.x + pr.w / 2, pr.y + pr.h / 2, md, md.nowMs).isNull());
    renderUi(c, ms, md, hits);
    if (!out.empty()) writePpm(c, out + "/ui_mic_fault.ppm");
  }
  // No microphone connected (no receiver in the bitstream, or no data on the
  // pins): the live page looks as without the acoustic reference, the device
  // page says "not connected" and why.
  {
    UiData md = d;
    md.s.measureState = "preview";
    md.s.acousticState = "disabled";
    Canvas off(1024, 600), absent(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(off, UiState(), md, hits);
    md.s.acousticState = "absent";
    md.s.acousticError = "no PDM receiver at 0x44030000 (bitstream without it)";
    renderUi(absent, UiState(), md, hits);
    CHECK(std::equal(off.data(), off.data() + 1024 * 600, absent.data()));
    UiState ds;
    ds.page = Page::kDevice;
    renderUi(absent, ds, md, hits);
    if (!out.empty()) writePpm(absent, out + "/ui_device_mic_absent.ppm");
  }
  // History: rows open the record, pages turn.
  {
    UiState hs;
    hs.page = Page::kHistory;
    const UiAction a = tap(hs, d, 500, 56 + 76 + 20);
    CHECK(a.kind == "open_record" && a.text == d.records[0].id);
    FbUi::applyAction(hs, a, Rect(), 0, 0, d, 0);
    CHECK(hs.page == Page::kResult && hs.openRecordId == d.records[0].id);
  }
  // Settings: a duration chip sends "set"; the shutdown button opens the confirmation.
  {
    UiState ss;
    ss.page = Page::kSettings;
    UiData sd = d;
    sd.s.measureState = "preview";
    const int cw = (916 - 8) / 2;
    const UiAction a = tap(ss, sd, 100 + cw - 4 * 64 - 8 + 2 * 64 + 20, 56 + 30);  // "30 s"
    CHECK(cmdOf(a) == "set" && std::fabs(jNum(a.cmd, "duration_s", 0) - 30) < 1e-9);
    const UiAction sh = tap(ss, sd, 100 + cw + 8 + cw - 100, 56 + 6 * 68 + 60);
    CHECK(sh.kind == "dialog" && sh.arg == static_cast<int>(Dialog::kShutdown));
  }
  // Shutdown dialog is modal: the rail does not react, confirm sends "shutdown".
  {
    UiState ds;
    ds.dialog = Dialog::kShutdown;
    CHECK(tap(ds, d, 40, 300).isNull());
    CHECK(cmdOf(tap(ds, d, 1024 / 2 + 250 - 136, 600 / 2 - 130 + 200)) == "shutdown");
  }
  // Wi-Fi dialog: typing and connecting.
  {
    UiState ws;
    UiData wd = d;
    wd.s.wifiSsid.clear();
    FbUi::applyAction(ws, uiAction("dialog", static_cast<int>(Dialog::kWifi)), Rect(), 0, 0, wd, 0);
    CHECK(ws.dialog == Dialog::kWifi && ws.field == 0);
    for (const char* k : {"l", "a", "b"}) FbUi::applyAction(ws, uiAction("kb", 0, k), Rect(), 0, 0, wd, 0);
    FbUi::applyAction(ws, uiAction("kb_field", 1), Rect(), 0, 0, wd, 0);
    for (const char* k : {"p", "w", "!"}) FbUi::applyAction(ws, uiAction("kb", 0, k), Rect(), 0, 0, wd, 0);
    FbUi::applyAction(ws, uiAction("kb_back"), Rect(), 0, 0, wd, 0);
    CHECK(ws.ssid == "lab" && ws.password == "pw");
    const UiAction conn = tap(ws, wd, 1024 - 36 - 196 + 80, 28 + 70 + 20);
    CHECK(cmdOf(conn) == "set_net" && jStr(conn.cmd, "ssid", "") == "lab" && jStr(conn.cmd, "password", "") == "pw");
    CHECK(tap(ws, wd, 40, 300).isNull());  // background blocked
  }
  // Vision page: depth probe stores the point; mode chips switch views.
  {
    UiState vs;
    vs.page = Page::kVision;
    vs.visionView = 2;
    const int cw = (916 - 8) / 2;
    Rect r;
    const UiAction a = tap(vs, d, 100 + cw + 8 + cw / 2, 56 + 38 + 150, &r);
    CHECK(a.kind == "depth_probe");
    FbUi::applyAction(vs, a, r, r.x + r.w / 2, r.y + r.h / 2, d, 0);
    CHECK_NEAR(vs.probeX, 0.5, 0.01);
    CHECK_NEAR(vs.probeY, 0.5, 0.01);
    CHECK(d.depth->at(0.5, 0.5) == 600);
    UiData colourData = d;
    auto colour = std::make_shared<DepthMap>(*d.depth);
    colour->leftBgr.resize(colour->mm.size()*3);
    for (size_t i=0;i<colour->mm.size();++i) {
      colour->leftBgr[3*i]=20; colour->leftBgr[3*i+1]=60; colour->leftBgr[3*i+2]=220;
    }
    colourData.depth = colour;
    Canvas canvas(1024,600); std::vector<HitRegion> hits;
    renderUi(canvas, vs, colourData, hits);
    int red=0;
    for(int y=145;y<340;y+=10) for(int x=125;x<515;x+=10) {
      uint32_t p=canvas.get(x,y); red+=int((p>>16)&255)>int(p&255)+100;
    }
    CHECK(red>500); // depth left card must show same-pair BGR, never gray/latest unrelated preview
  }
  // A grey control says why next to it and sends nothing: the gain only applies to motion magnification.
  {
    UiState vs;
    vs.page = Page::kVision;
    vs.visionView = 1;
    UiData vd = d;
    vd.s.measureState = "preview";
    const int gx = 100 + 96 + 5, gy = 56 + 344 + 8 + 126 + 18;  // the x5 chip
    Rect r;
    const UiAction a = tap(vs, vd, gx, gy, &r);
    CHECK(refused(a) && a.text == "放大倍数只用于运动放大");
    CHECK(FbUi::applyAction(vs, a, r, gx, gy, vd, vd.nowMs).isNull());
    CHECK(vs.toast == a.text && vs.toastAt == r && vs.toastUntilMs > vd.nowMs);
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, vs, vd, hits);
    if (!out.empty()) writePpm(c, out + "/ui_reason.ppm");
    vs.visionView = 0;
    const UiAction g = tap(vs, vd, gx, gy);
    CHECK(cmdOf(g) == "set" && jInt(g.cmd, "vision_gain", 0) == 5);
  }

  // Every glyph of the shown text exists in the generated fonts.
  for (const char* txt : {"开始测量", "停止测量", "打印报告", "双目深度未接入", "振动热力", "测量记录", "配置 Wi-Fi",
                          "放大倍数只用于运动放大", "测量期间投影保持关闭", "需先在电机停机时采集基线",
                          "应用已停止, 等待Linux关机", "视觉测振报告", "未判级", "声学包络谱", "与视觉一致",
                          "视觉频率的 2 倍", "声学包络无明显周期", "声学参考数据中断", "麦克风启动", "麦克风故障",
                          "声学麦克风", "时钟与预热", "未接入", "无麦克风数据（未接或 DAT 异常）", "所设相位无数据，检查 L/R",
                          "PL 位流不含麦克风接收核", "掉电预警，麦克风已停", "无明确", "最大峰 13.96 Hz · 不明确",
                          "上次测量没有明确主频", "上次校正尚未生效，请等新的深度图", "代表测点波形无效：丢帧",
                          "手机APP配对码 0123456789", "手机APP已连接 192.168.4.2"}) {
    const std::string str(txt);
    for (bool bold : {false, true}) {
      const FontFace& f = fontFace(22, bold);
      for (size_t i = 0; i < str.size();) CHECK(hasGlyph(f, nextCodepoint(str, i)));
    }
  }

  // ------------------------------------------------------------------- render
  struct Shot {
    const char* name;
    Page page;
    int view;  // vision view, or the result chart
    Dialog dialog;
  };
  const Shot shots[] = {
      {"ui_live", Page::kLive, 0, Dialog::kNone},         {"ui_result", Page::kResult, 0, Dialog::kNone},
      {"ui_vision", Page::kVision, 0, Dialog::kNone},     {"ui_heat", Page::kVision, 1, Dialog::kNone},
      {"ui_depth", Page::kVision, 2, Dialog::kNone},      {"ui_history", Page::kHistory, 0, Dialog::kNone},
      {"ui_device", Page::kDevice, 0, Dialog::kNone},     {"ui_settings", Page::kSettings, 0, Dialog::kNone},
      {"ui_wifi", Page::kSettings, 0, Dialog::kWifi},     {"ui_shutdown", Page::kSettings, 0, Dialog::kShutdown},
      {"ui_result_acoustic", Page::kResult, 1, Dialog::kNone},
  };
  for (const Shot& sh : shots) {
    UiState rs;
    rs.page = sh.page;
    rs.visionView = sh.view;
    rs.resultChart = sh.page == Page::kResult ? sh.view : 0;
    rs.dialog = sh.dialog;
    rs.probeX = 0.72;
    rs.probeY = 0.3;
    if (sh.dialog == Dialog::kWifi) {
      rs.ssid = "Lab-5G";
      rs.password = "secret";
      rs.field = 1;
    }
    UiData rd = d;
    if (sh.page == Page::kVision || sh.page == Page::kSettings || sh.page == Page::kResult) rd.s.measureState = "preview";
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, rs, rd, hits);
    CHECK(c.get(10, 10) != 0);
    CHECK(!hits.empty());
    if (!out.empty()) writePpm(c, out + "/" + sh.name + ".ppm");
  }
  {
    UiData sd = d;
    sd.s.shutdownPrepared = true;
    sd.s.message = "应用已停止, 等待Linux关机";
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, UiState(), sd, hits);
    CHECK(hits.empty());  // nothing reacts after the application stopped
    if (!out.empty()) writePpm(c, out + "/ui_shutdown_done.ppm");
  }
  {
    UiData pd = d;
    pd.s.measureState = "preview";
    UiState ds;
    ds.roiGrab = kGrabRight | kGrabBottom;
    ds.roiDrag = NormBox{0.36, 0.38, 0.6, 0.5};
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, ds, pd, hits);
    if (!out.empty()) writePpm(c, out + "/ui_live_roi.ppm");
  }
  {
    // Multi-point method in preview: grid (green) and reference (amber) templates, baseline state.
    UiData md = d;
    md.s.method = "multipoint";
    md.s.measureState = "preview";
    md.s.baselineValid = true;
    md.s.settings.roiW = 160;
    md.s.settings.roiH = 96;
    Canvas c(1024, 600);
    std::vector<HitRegion> hits;
    renderUi(c, UiState(), md, hits);
    if (!out.empty()) writePpm(c, out + "/ui_live_multipoint.ppm");
    // The camera image keeps its colours (red and blue not swapped).
    const Rect img = fitAspect(Rect{kLiveCameraCard.x + 4, kLiveCameraCard.y + 4, kLiveCameraCard.w - 8,
                                    kLiveCameraCard.h - 8}, 640, 480);
    int warm = 0, cold = 0;
    for (int y = img.y; y < img.y + img.h; y += 4) {
      for (int x = img.x; x < img.x + img.w; x += 4) {
        const uint32_t p = c.get(x, y);
        const int r = (p >> 16) & 0xFF, b = p & 0xFF;
        warm += r > b + 8;
        cold += b > r + 8;
      }
    }
    CHECK(warm > 4 * cold);  // the overlays (green templates, buttons) are the cold part
    // The camera card redrawn alone over the frame leaves the frame as a full render makes it.
    Canvas again = c;
    renderLivePreview(again, UiState(), md);
    CHECK(memcmp(again.data(), c.data(), sizeof(uint32_t) * c.width() * c.height()) == 0);
    // "Move reference": the next tap places the reference group, then the box takes touches again.
    UiState rs;
    rs.refEdit = true;
    Rect r;
    const UiAction a = tap(rs, md, img.x + img.w / 4, img.y + img.h * 3 / 4, &r);
    CHECK(a.kind == "roi_box");
    JsonValue cmd = FbUi::applyAction(rs, a, r, img.x + img.w / 4, img.y + img.h * 3 / 4, md, 0);
    CHECK(cmd.find("ref_x") && !rs.refEdit);
    cmd = FbUi::applyAction(rs, a, r, img.x + img.w / 4, img.y + img.h * 3 / 4, md, 0);
    CHECK(cmd.find("x") && !cmd.find("ref_x"));
  }

  // Volume preview uses the saved volume and cannot inject speech into a capture.
  {
    UiData vd = d;
    vd.s.nanoOnline = true;
    vd.s.settings.voice = true;
    vd.s.measureState = "preview";
    UiState st;
    st.page = Page::kSettings;
    const auto a = tap(st, vd, 732, 358);
    CHECK(a.kind == "cmd" && jStr(a.cmd, "cmd", "") == "voice");
    CHECK(jStr(a.cmd, "asset", "") == "measure_done" && !a.cmd.find("volume"));
    vd.s.measureState = "capturing";
    CHECK(tap(st, vd, 732, 358).kind == "why");
    vd.s.measureState = "preview";
    vd.s.nanoOnline = false;
    CHECK(tap(st, vd, 732, 358).kind == "why");
  }

  // Printed slip.
  ReportData rep;
  rep.deviceId = "vibmon-01";
  rep.measurementId = s.last.measurementId;
  rep.timeText = "2026-10-20 13:07:09 (UTC+8)";
  rep.pointText = "P-01 电机A";
  rep.summary = s.last;
  rep.calibrationText = "CAL-01";
  rep.fps = 59.9;
  rep.envText = "环境 24.6 °C  48 %RH";
  rep.t = d.record.t;
  rep.disp = d.record.disp;
  rep.f = d.record.f;
  rep.amp = d.record.amp;
  Canvas slip = renderReport(rep, 384);
  CHECK(slip.width() == 384 && slip.height() > 400);
  int wb = 0;
  std::vector<uint8_t> bits = toMonoBits(slip, wb);
  CHECK(wb == 48 && bits.size() == static_cast<size_t>(48 * slip.height()));
  if (!out.empty()) {
    writePpm(slip, out + "/report.ppm");
    printf("wrote UI pages and report.ppm to %s\n", out.c_str());
  }
  return testSummary("render");
}
