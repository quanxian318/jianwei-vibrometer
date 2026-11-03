#pragma once

#include <cmath>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "app/state.h"
#include "app/storage.h"
#include "gfx/canvas.h"
#include "gfx/font.h"
#include "measure/depth.h"
#include "measure/session.h"
#include "ui/icons.h"
#include "ui/theme.h"
#include "util/json_util.h"

namespace vm {

enum class Page { kLive = 0, kResult, kVision, kHistory, kDevice, kSettings };
constexpr int kPageCount = 6;

enum class Dialog { kNone, kShutdown, kWifi };

struct Rect {
  int x = 0, y = 0, w = 0, h = 0;
  bool contains(int px, int py) const { return px >= x && py >= y && px < x + w && py < y + h; }
  bool operator==(const Rect& o) const { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// What touching a region does. Kind "cmd" sends cmd to Application::command();
// the other kinds change UI-local state (see FbUi::applyAction).
struct UiAction {
  std::string kind;
  JsonValue cmd;
  int arg = 0;
  std::string text;
  double nx0 = 0, ny0 = 0, nw = 1, nh = 1;  // "roi"/"roi_box": normalised image window shown in the region
  bool isNull() const { return kind.empty(); }
};

// Measuring box in normalised image coordinates (0..1).
struct NormBox {
  double x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

// Parts of the measuring box a touch drags (bit set).
enum RoiGrab { kGrabLeft = 1, kGrabRight = 2, kGrabTop = 4, kGrabBottom = 8, kGrabMove = 16 };

// Whether a control reacts to touch. A disabled control is drawn grey; when
// it carries a reason, tapping it shows the reason next to the control.
struct Enable {
  bool on = true;
  std::string why;
  Enable(bool enabled = true) : on(enabled) {}
  Enable(bool enabled, std::string reason) : on(enabled), why(enabled ? std::string() : std::move(reason)) {}
  operator bool() const { return on; }
};
// Enabled when reason is empty, otherwise disabled for that reason.
inline Enable unless(const std::string& reason) { return Enable(reason.empty(), reason); }

struct HitRegion {
  Rect r;
  UiAction a;
  bool enabled = true;
  std::string why;  // disabled: reason shown when it is tapped
};

// A saved record loaded for the result page.
struct RecordView {
  std::string id;
  bool loaded = false;
  JsonValue rec;
  std::vector<double> t, disp, f, amp;
  std::vector<double> af, aamp;  // acoustic envelope spectrum (may be empty)
};

// UI-local state, owned by the UI thread.
struct UiState {
  Page page = Page::kLive;
  Dialog dialog = Dialog::kNone;
  Rect pressed;  // region under the finger, drawn highlighted
  bool pressing = false;
  int historyPage = 0;
  std::string openRecordId;  // result page shows this saved record instead of the latest one
  int visionView = 0;        // 0 = motion magnification, 1 = vibration heat map, 2 = stereo depth
  double probeX = -1, probeY = -1;  // depth probe point (normalised), -1 = none
  int depthRange = 0;               // 0 = automatic, 1..3 = fixed display ranges
    mutable DepthDisplayRange depthDisplayRange;  // rendering cache, not a user setting
  int resultChart = 0;              // result page: 0 = displacement spectrum, 1 = acoustic envelope
  bool liveHeat = false;     // heat-map overlay on the live preview
  int roiGrab = 0;           // measuring box being dragged on the live preview (RoiGrab bits, 0 = none)
  NormBox roiDrag;           // its position while dragging
  bool refEdit = false;      // multi-point: the next drag or tap places the reference group, not the box
  bool panning = false;      // zoomed live view being dragged by the finger
  double panCx = 0.5, panCy = 0.5;  // its centre meanwhile (normalised)
  std::string toast;
  int64_t toastUntilMs = 0;
  Rect toastAt;  // control a reason belongs to; empty = message at the bottom
  // Wi-Fi dialog
  std::string ssid, password;
  int field = 0;  // 0 = SSID, 1 = password
  bool shift = false, symbols = false, showPassword = false;
};

// Everything one frame shows, gathered by the UI thread before drawing.
struct UiData {
  SystemState s;
  MeasurementEngine::Live live;
  std::shared_ptr<const DepthMap> depth;  // stereo depth, null until the stereo module publishes one
  bool stereoOn = false;                  // the stereo module runs (no map yet: waiting for one)
  double previewDisplayFps = 0;          // unique preview images actually submitted, not capture rate
  std::vector<RecordSummary> records;
  RecordView record;
  int64_t nowMs = 0;   // CLOCK_MONOTONIC
  int64_t wallMs = 0;  // CLOCK_REALTIME
  int tzOffsetMin = 480;
  MultiPointConfig multipoint;  // grid and reference layout of the multi-point method
};

// Immediate-mode painter: widgets draw themselves and register their touch
// regions in the same call, so what is shown and what reacts cannot differ.
class Ui {
 public:
  Ui(Canvas& canvas, const UiState& state, std::vector<HitRegion>& hits) : c(canvas), st(state), hits_(hits) {}
  Canvas& c;
  const UiState& st;

  void hit(const Rect& r, const UiAction& a, const Enable& enabled = true);
  void block(const Rect& r);  // swallows touches (modal background)
  bool pressed(const Rect& r) const { return st.pressing && st.pressed == r; }

  void card(const Rect& r, uint32_t fill = theme::kCard);
  void cardTitle(const Rect& r, const std::string& title, Icon icon = Icon::kNone);
  void button(const Rect& r, const std::string& label, Icon icon, uint32_t fill, const Enable& enabled,
              const UiAction& a, int fontPx = 22);
  void iconButton(const Rect& r, Icon icon, const Enable& enabled, const UiAction& a, uint32_t fill = theme::kNeutral);
  void chip(const Rect& r, const std::string& label, bool selected, const Enable& enabled, const UiAction& a);
  void toggle(const Rect& r, bool on, const Enable& enabled, const UiAction& a);
  void stepper(const Rect& r, const std::string& value, const Enable& decOk, const Enable& incOk,
               const UiAction& dec, const UiAction& inc);
  int badge(int x, int y, const std::string& text, uint32_t color, bool filled = false, int fontPx = 18);
  void emptyState(const Rect& r, Icon icon, const std::string& title, const std::string& hint);
  void metric(int x, int y, const std::string& label, const std::string& value, const std::string& unit,
              uint32_t valueColor = theme::kText);

 private:
  std::vector<HitRegion>& hits_;
};

// Line chart with nice axis ticks, used by the result and live pages.
struct ChartOptions {
  std::string title, unitX, unitY;
  uint32_t line = theme::kAccent;
  bool symmetric = false;   // y range symmetric around zero
  bool fromZero = false;    // y axis starts at zero (spectra)
  bool fill = false;        // shaded area under the curve
  double markerX = NAN;     // vertical marker (spectral peak)
  std::string markerLabel;
  double refX = NAN;        // second marker (comparison), below the first label
  std::string refLabel;
  uint32_t refColor = theme::kGreen;
  std::string rightNote;    // text at the top right
};
void drawChart(Canvas& c, const Rect& r, const std::vector<double>& xs, const std::vector<double>& ys,
               const ChartOptions& o);

UiAction cmdAction(const JsonValue& cmd);
UiAction cmdAction(const std::string& name);
inline UiAction cmdAction(const char* name) { return cmdAction(std::string(name)); }
UiAction uiAction(const std::string& kind, int arg = 0, const std::string& text = std::string());
uint32_t levelColor(AlarmLevel l);
std::string fmtNum(double v, int sig = 4);           // %.4g without "-0"
std::string reasonText(const std::string& reason);  // invalid-result reason -> text
std::string errorText(const std::string& code);     // command error -> text
std::string acousticErrorText(const std::string& error);  // acoustic service fault -> short text
// Why the lighting / the cross projector cannot be switched now; empty when it can.
std::string lightBlock(const SystemState& s);
std::string laserBlock(const SystemState& s);
const char* pageTitle(Page p);
// Largest rectangle with the aspect fw:fh inside box, centred.
Rect fitAspect(const Rect& box, int fw, int fh);

// Measuring box editing on the live preview ("roi_box" region).
// Box of the current settings (centre and pixel size) for a fw x fh frame.
NormBox roiBox(const SystemState& s, int fw, int fh);
// The multi-point reference group: its 2 x 2 templates around (refCx, refCy).
NormBox refGroupBox(const SystemState& s, int fw, int fh, int refPitchPx);
// The box on screen, through the image window that action a shows in region.
Rect roiScreenRect(const NormBox& b, const UiAction& a, const Rect& region);
// Part of the on-screen box b that a touch at (x, y) grabs, 0 = none. Edges
// and corners reach a finger width outside the box and up to a quarter of its
// size inside; the rest of the inside moves the whole box.
int roiGrabAt(const Rect& b, int x, int y);
// b with the grabbed part moved by (dx, dy) (normalised), kept inside the
// image and within kRoiMinPx..maxPx (the tracker's limit) for a fw x fh frame.
NormBox roiDragged(const NormBox& b, int grab, double dx, double dy, int fw, int fh, int maxPx);
// "roi" command that places the box b (centre and pixel size).
JsonValue roiCommand(const NormBox& b, int fw, int fh);
// Centre of the zoomed view after the finger moved (dx, dy) screen pixels on
// region, whose action a holds the window shown at the press; the view keeps
// inside the image.
void pannedCentre(const UiAction& a, const Rect& region, int dx, int dy, double& cx, double& cy);

void renderUi(Canvas& c, const UiState& st, const UiData& d, std::vector<HitRegion>& hits);
// Camera card of the live page (kLiveCameraCard) alone, over the last full
// frame: a newer camera image between full frames. Its touch regions are
// those the last renderUi registered.
constexpr Rect kLiveCameraCard{theme::kCx, theme::kCy, 600, 348};
void renderLivePreview(Canvas& c, const UiState& st, const UiData& d);
// The window (sx, sy, sw, sh, normalised) of a camera image into r.
void drawPreview(Canvas& c, const Rect& r, const PreviewImage& p, double sx, double sy, double sw, double sh);
// Topmost region at (x, y). A disabled region returns a "why" action with its
// reason (null without one); blocking regions return a null action.
UiAction hitTest(const std::vector<HitRegion>& hits, int x, int y, Rect* region = nullptr);

// Page renderers (ui_pages.cpp, ui_pages2.cpp).
void renderLivePage(Ui& ui, const UiData& d);
void renderResultPage(Ui& ui, const UiData& d);
void renderVisionPage(Ui& ui, const UiData& d);
void renderHistoryPage(Ui& ui, const UiData& d);
void renderDevicePage(Ui& ui, const UiData& d);
void renderSettingsPage(Ui& ui, const UiData& d);
void renderWifiDialog(Ui& ui, const UiData& d);

}  // namespace vm
