#include "ui/fb_ui.h"

#include <poll.h>

#include <algorithm>
#include <cstdlib>

#include "app/app.h"
#include "app/storage.h"
#include "ui/pl_display.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

FbUi::~FbUi() {
  running_ = false;
  if (thread_.joinable()) thread_.join();
}

bool FbUi::start() {
  const AppConfig& cfg = app_.config();
  if (cfg.ui.display == "pl_vdma") {
    std::unique_ptr<PlDisplay> pl(new PlDisplay);
    std::string err;
    if (!pl->open(cfg.ui.pl, err)) {
      LOGW("ui", "PL video output: %s; touch UI disabled (vibmonctl still works)", err.c_str());
      return false;
    }
    display_ = std::move(pl);
  } else {
    std::unique_ptr<FbDisplay> fb(new FbDisplay);
    if (!fb->open(cfg.ui.fbdev)) {
      LOGW("ui", "no framebuffer; touch UI disabled (vibmonctl still works)");
      return false;
    }
    display_ = std::move(fb);
  }
  console_.reset(new TextConsoleOff);
  canvas_.resize(display_->width(), display_->height(), theme::kBg);
  if (touch_.open(cfg.ui.touchDev)) {
    touch_.setScreen(display_->width(), display_->height(), cfg.ui.swapXY, cfg.ui.invertX, cfg.ui.invertY);
  } else {
    LOGW("ui", "no touch input device yet; looking again every 3 s");
  }
  running_ = true;
  thread_ = std::thread(&FbUi::run, this);
  return true;
}

void FbUi::onEncoder(int detents) {
  std::lock_guard<std::mutex> lock(encMutex_);
  encSteps_ += detents;
}

void FbUi::onEncoderPress(bool longPress) {
  std::lock_guard<std::mutex> lock(encMutex_);
  ++(longPress ? encLong_ : encShort_);
}

JsonValue FbUi::applyAction(UiState& st, const UiAction& a, const Rect& region, int x, int y, const UiData& d,
                            int64_t nowMs) {
  if (a.kind == "cmd") return a.cmd;
  if (a.kind == "why") {  // a grey control was tapped: say why next to it
    st.toast = a.text;
    st.toastAt = region;
    st.toastUntilMs = nowMs + 2500;
    return JsonValue();
  }
  if (a.kind == "nav") {
    st.page = static_cast<Page>(std::max(0, std::min(kPageCount - 1, a.arg)));
    if (st.page == Page::kResult) st.openRecordId.clear();
  } else if (a.kind == "roi" || a.kind == "roi_box" || a.kind == "depth_probe") {
    const double nx = a.nx0 + (x - region.x + 0.5) / std::max(1, region.w) * a.nw;
    const double ny = a.ny0 + (y - region.y + 0.5) / std::max(1, region.h) * a.nh;
    if (a.kind == "depth_probe") {
      st.probeX = nx;
      st.probeY = ny;
      return JsonValue();
    }
    JsonValue cmd = JsonValue::makeObject();
    cmd.set("cmd", "roi");
    const bool ref = st.refEdit && a.kind == "roi_box";
    cmd.set(ref ? "ref_x" : "x", nx);
    cmd.set(ref ? "ref_y" : "y", ny);
    if (ref) st.refEdit = false;  // one placement per "move reference": the box takes touches again
    return cmd;
  } else if (a.kind == "hist_page") {
    st.historyPage = std::max(0, st.historyPage + a.arg);
  } else if (a.kind == "open_record") {
    st.openRecordId = a.text;
    st.page = Page::kResult;
  } else if (a.kind == "close_record") {
    st.openRecordId.clear();
    st.page = Page::kHistory;
  } else if (a.kind == "vision_view") {
    st.visionView = a.arg;
  } else if (a.kind == "depth_range") {
    st.depthRange = a.arg;
    if (a.arg == 0) st.depthDisplayRange.reset();
  } else if (a.kind == "result_chart") {
    st.resultChart = a.arg;
  } else if (a.kind == "live_heat") {
    st.liveHeat = !st.liveHeat;
  } else if (a.kind == "ref_edit") {
    st.refEdit = !st.refEdit;
  } else if (a.kind == "dialog") {
    st.dialog = static_cast<Dialog>(a.arg);
    if (st.dialog == Dialog::kWifi) {
      st.ssid = d.s.wifiSsid;
      st.password.clear();
      st.field = st.ssid.empty() ? 0 : 1;
      st.shift = st.symbols = st.showPassword = false;
    }
  } else if (a.kind == "kb_field") {
    st.field = a.arg;
  } else if (a.kind == "kb") {
    std::string& t = st.field == 0 ? st.ssid : st.password;
    const size_t maxLen = st.field == 0 ? 32 : 63;
    if (t.size() + a.text.size() <= maxLen) t += a.text;
    st.shift = false;
  } else if (a.kind == "kb_back") {
    std::string& t = st.field == 0 ? st.ssid : st.password;
    if (!t.empty()) t.pop_back();
  } else if (a.kind == "kb_shift") {
    st.shift = !st.shift;
  } else if (a.kind == "kb_sym") {
    st.symbols = !st.symbols;
    st.shift = false;
  } else if (a.kind == "kb_show") {
    st.showPassword = !st.showPassword;
  }
  return JsonValue();
}

void FbUi::toast(const std::string& text) {
  st_.toast = text;
  st_.toastAt = Rect();
  st_.toastUntilMs = monoMs() + 4000;
}

void FbUi::perform(const UiAction& a, const Rect& region, int x, int y, const UiData& d) {
  const JsonValue cmd = applyAction(st_, a, region, x, y, d, monoMs());
  dirty_ = true;
  if (cmd.isNull()) return;
  const std::string name = jStr(cmd, "cmd", "");
  const JsonValue reply = app_.command(cmd);
  if (!jBool(reply, "ok", false)) {
    const std::string err = jStr(reply, "error", "");
    LOGI("ui", "%s: %s", name.c_str(), err.c_str());
    toast(errorText(err));
    return;
  }
  if (name == "set_net") {
    st_.dialog = Dialog::kNone;
    toast(jStr(cmd, "ssid", "").empty() ? "已关闭 Wi-Fi" : "正在连接 " + jStr(cmd, "ssid", ""));
  } else if (name == "shutdown") {
    st_.dialog = Dialog::kNone;
  } else if (name == "roi") {
    toast(cmd.find("ref_x") ? "参考点已更新" : "测点框已更新");
  } else if (name == "stereo_distance") {
    toast(st_.probeX >= 0 ? "已按选点处的距离校正深度" : "已按测点处的距离校正深度");
  }
}

void FbUi::handleTouch(const TouchEvent& e, const UiData& d) {
  switch (e.type) {
    case TouchEvent::kDown: {
      fingerDown_ = true;
      moved_ = dragging_ = false;
      downX_ = e.x;
      downY_ = e.y;
      Rect r;
      const UiAction a = hitTest(hits_, e.x, e.y, &r);
      if (st_.toastAt.w > 0) st_.toastUntilMs = 0;  // a new touch closes the reason bubble
      st_.pressing = !a.isNull();
      st_.pressed = r;
      st_.roiGrab = 0;
      panArmed_ = false;
      if (a.kind == "roi_box" && d.live.frameW > 0 && d.live.frameH > 0) {
        // The reference group keeps its size: it is only moved.
        dragStart_ = st_.refEdit ? refGroupBox(d.s, d.live.frameW, d.live.frameH, d.multipoint.refPitchPx)
                                 : roiBox(d.s, d.live.frameW, d.live.frameH);
        st_.roiGrab = roiGrabAt(roiScreenRect(dragStart_, a, r), e.x, e.y);
        if (st_.refEdit && st_.roiGrab) st_.roiGrab = kGrabMove;
        st_.roiDrag = dragStart_;
        dragAction_ = a;
        dragRegion_ = r;
        dragFw_ = d.live.frameW;
        dragFh_ = d.live.frameH;
        dragMaxPx_ = d.s.settings.roiMaxPx;
      }
      if ((a.kind == "roi_box" || a.kind == "pan_view") && !st_.roiGrab && d.s.zoom > 1.01) {
        panArmed_ = true;
        dragAction_ = a;
        dragRegion_ = r;
      }
      dirty_ = true;
      break;
    }
    case TouchEvent::kMove:
      if (!fingerDown_) break;
      if (std::abs(e.x - downX_) > 24 || std::abs(e.y - downY_) > 24) {
        moved_ = true;
        if (st_.pressing) {
          st_.pressing = false;
          dirty_ = true;
        }
      }
      // The zoomed view follows the finger; it is set on release.
      if (panArmed_ && (dragging_ || std::abs(e.x - downX_) > 6 || std::abs(e.y - downY_) > 6)) {
        dragging_ = true;
        st_.panning = true;
        pannedCentre(dragAction_, dragRegion_, e.x - downX_, e.y - downY_, st_.panCx, st_.panCy);
        dirty_ = true;
      }
      // The box follows the finger once it has left a small dead zone.
      if (st_.roiGrab && (dragging_ || std::abs(e.x - downX_) > 6 || std::abs(e.y - downY_) > 6)) {
        dragging_ = true;
        const double dx = static_cast<double>(e.x - downX_) / std::max(1, dragRegion_.w) * dragAction_.nw;
        const double dy = static_cast<double>(e.y - downY_) / std::max(1, dragRegion_.h) * dragAction_.nh;
        st_.roiDrag = roiDragged(dragStart_, st_.roiGrab, dx, dy, dragFw_, dragFh_, dragMaxPx_);
        dirty_ = true;
      }
      break;
    case TouchEvent::kUp: {
      if (!fingerDown_) break;
      fingerDown_ = false;
      st_.pressing = false;
      dirty_ = true;
      if (st_.roiGrab) {
        // Releasing a dragged box places it; a tap on the box keeps it as it is.
        st_.roiGrab = 0;
        if (dragging_ && st_.refEdit) {
          JsonValue ref = JsonValue::makeObject();
          ref.set("cmd", "roi");
          ref.set("ref_x", 0.5 * (st_.roiDrag.x0 + st_.roiDrag.x1));
          ref.set("ref_y", 0.5 * (st_.roiDrag.y0 + st_.roiDrag.y1));
          st_.refEdit = false;  // placed: the box takes touches again
          perform(cmdAction(ref), Rect(), 0, 0, d);
        } else if (dragging_) {
          perform(cmdAction(roiCommand(st_.roiDrag, dragFw_, dragFh_)), Rect(), 0, 0, d);
        }
        break;
      }
      panArmed_ = false;
      if (st_.panning) {
        st_.panning = false;
        JsonValue pan = JsonValue::makeObject();
        pan.set("cmd", "pan");
        pan.set("x", st_.panCx);
        pan.set("y", st_.panCy);
        perform(cmdAction(pan), Rect(), 0, 0, d);
        break;
      }
      const int dx = e.x - downX_, dy = e.y - downY_;
      if (st_.dialog == Dialog::kNone && std::abs(dx) > 140 && std::abs(dx) > 2 * std::abs(dy)) {
        const int p = (static_cast<int>(st_.page) + (dx < 0 ? 1 : kPageCount - 1)) % kPageCount;
        UiAction nav = uiAction("nav", p);
        perform(nav, Rect(), 0, 0, d);
      } else if (!moved_ && std::abs(dx) <= 24 && std::abs(dy) <= 24) {
        Rect r;
        const UiAction a = hitTest(hits_, downX_, downY_, &r);
        if (!a.isNull()) perform(a, r, downX_, downY_, d);
      }
      break;
    }
  }
}

void FbUi::handleEncoder() {
  int steps, shorts, longs;
  {
    std::lock_guard<std::mutex> lock(encMutex_);
    steps = encSteps_;
    shorts = encShort_;
    longs = encLong_;
    encSteps_ = encShort_ = encLong_ = 0;
  }
  if (!steps && !shorts && !longs) return;
  dirty_ = true;
  if (st_.dialog != Dialog::kNone) return;
  for (int i = 0; i < longs; ++i) {
    st_.page = static_cast<Page>((static_cast<int>(st_.page) + 1) % kPageCount);
    if (st_.page == Page::kResult) st_.openRecordId.clear();
  }
  if (st_.page == Page::kLive) {
    if (steps) {
      JsonValue z = JsonValue::makeObject();
      z.set("cmd", "zoom");
      z.set("steps", steps);
      app_.command(z);
    }
    if (shorts) {
      JsonValue z = JsonValue::makeObject();
      z.set("cmd", "zoom_reset");
      app_.command(z);
    }
  } else {
    if (st_.page == Page::kHistory && steps) st_.historyPage = std::max(0, st_.historyPage + steps);
    if (shorts) st_.page = Page::kLive;
  }
}

void FbUi::gather(UiData& d) {
  const AppConfig& cfg = app_.config();
  d.s = app_.snapshot();
  d.live = app_.cameraLive();
  d.depth = app_.depthLive();
  d.stereoOn = app_.stereoOn();
  d.nowMs = monoMs();
  d.wallMs = realtimeMs();
  d.tzOffsetMin = cfg.tzOffsetMin;
  d.multipoint = cfg.measure.multipoint;
  if (st_.page == Page::kHistory &&
      (d.nowMs - recordsMs_ > 3000 || recordsKey_ != d.s.last.measurementId)) {
    records_ = listRecords(cfg.recordsDir, 200);
    recordsMs_ = d.nowMs;
    recordsKey_ = d.s.last.measurementId;
  }
  d.records = records_;
  const std::string want = !st_.openRecordId.empty() ? st_.openRecordId
                                                     : (d.s.last.saved ? d.s.last.measurementId : std::string());
  if (st_.page == Page::kResult && !want.empty() && record_.id != want) {
    record_ = RecordView();
    record_.id = want;
    record_.loaded = loadRecord(cfg.recordsDir, want, record_.rec, record_.t, record_.disp, record_.f, record_.amp);
    if (record_.loaded) loadAcousticSpectrum(cfg.recordsDir, want, record_.af, record_.aamp);
  }
  d.record = record_;
}

void FbUi::updateDisplayNeeds() {
  const bool live = st_.page == Page::kLive, vision = st_.page == Page::kVision;
  const bool depthView = vision && st_.visionView == 2;
  // Depth shows the image belonging to its own pair. Do not keep copying a
  // separate 1280x720 colour preview that is neither aligned nor displayed.
  const int needs = (live || (vision && !depthView) ? 1 : 0) | ((vision && !depthView) || (live && st_.liveHeat) ? 2 : 0) |
                    (depthView ? 4 : 0);
  if (needs == needs_) return;
  needs_ = needs;
  app_.setDisplayNeeds(needs & 1, needs & 2, needs & 4);
}

bool FbUi::cameraOnly(int64_t nowMs) const {
  return st_.page == Page::kLive && st_.dialog == Dialog::kNone && st_.toastUntilMs <= nowMs && !fingerDown_ &&
         !st_.pressing && !st_.roiGrab && !st_.panning;
}

void FbUi::run() {
  UiData d;
  int64_t nextFrame = 0, nextCamera = 0, nextTouchScan = 0;
  uint64_t shownImage = 0;
  std::shared_ptr<const DepthMap> shownDepth;
  int64_t lastFullDraw = 0;
  int64_t displayWindow = monoMs(), nextRateLog = 0;
  int displayImages = 0;
  double displayFps = 0;
  std::vector<TouchEvent> events;
  while (running_) {
    const int64_t now = monoMs();
    if (now - displayWindow >= 1000) {
      displayFps = displayImages * 1000.0 / (now - displayWindow);
      displayImages = 0;
      displayWindow = now;
      if (now >= nextRateLog) {
        LOGI("ui", "unique preview submissions %.1f fps", displayFps);
        nextRateLog = now + 10000;
      }
    }
    // The panel's USB touch controller may enumerate after the start (or again
    // after an HDMI disturbance): look for it every 3 s while there is none.
    if (touch_.fd() < 0 && now >= nextTouchScan) {
      nextTouchScan = now + 3000;
      const AppConfig& cfg = app_.config();
      if (touch_.open(cfg.ui.touchDev))
        touch_.setScreen(display_->width(), display_->height(), cfg.ui.swapXY, cfg.ui.invertX, cfg.ui.invertY);
    }
    handleEncoder();
    const bool depthPage = st_.page == Page::kVision && st_.visionView == 2;
    bool fullDraw = now >= nextFrame || dirty_;
    if (fullDraw && depthPage) {
      const auto newest = app_.depthLive();
      const bool interactive = dirty_ || fingerDown_ || st_.pressing || st_.dialog != Dialog::kNone || st_.toastUntilMs > now;
      if (!depthRedrawNeeded(newest != shownDepth, interactive, now - lastFullDraw)) {
        fullDraw = false;
        nextFrame = now + 25;
      }
    }
    if (fullDraw) {
      gather(d);
      d.previewDisplayFps = displayFps;
      renderUi(canvas_, st_, d, hits_);
      display_->present(canvas_);
      shownDepth = d.depth;
      lastFullDraw = now;
      if (st_.page == Page::kLive && d.live.preview && d.live.preview->frame != shownImage) ++displayImages;
      shownImage = d.live.preview ? d.live.preview->frame : 0;
      dirty_ = false;
      // ~8 fps, touch feedback draws in between; 4 fps on the live page in
      // preview, where the camera card has its own updates.
      if (now >= nextFrame)
        nextFrame = now + (depthPage ? 25 : (st_.page == Page::kLive && d.s.measureState == "preview" ? 500 : 125));
      updateDisplayNeeds();
    } else if (now >= nextCamera) {
      // Between full frames the camera card follows each new camera image.
      nextCamera = now + 5;
      if (cameraOnly(now)) {
        d.live = app_.cameraLive();
        if (d.live.preview && d.live.preview->frame != shownImage) {
          d.s = app_.snapshot();
          d.nowMs = now;
          d.previewDisplayFps = displayFps;
          renderLivePreview(canvas_, st_, d);
          display_->presentRegion(canvas_, kLiveCameraCard.x, kLiveCameraCard.y, kLiveCameraCard.w, kLiveCameraCard.h);
          ++displayImages;
          shownImage = d.live.preview->frame;
        }
      }
    }
    const int64_t wake = st_.page == Page::kLive ? std::min(nextFrame, nextCamera) : nextFrame;
    const int wait = static_cast<int>(std::max<int64_t>(0, wake - monoMs()));
    if (touch_.fd() >= 0) {
      pollfd p{touch_.fd(), POLLIN, 0};
      if (poll(&p, 1, std::min(wait, 125)) > 0) {
        events.clear();
        touch_.read(events);
        for (const TouchEvent& e : events) handleTouch(e, d);
      }
    } else {
      sleepMs(std::min(wait, 125));
    }
  }
}

}  // namespace vm
