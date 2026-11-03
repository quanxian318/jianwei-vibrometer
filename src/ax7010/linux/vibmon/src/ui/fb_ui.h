#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "gfx/canvas.h"
#include "ui/fb_display.h"
#include "ui/ui_core.h"
#include "util/json_util.h"

namespace vm {

class Application;

// Touch screen UI (1024x600): six pages (live, result, vision, history,
// devices, settings) behind a navigation rail. Operator actions go through
// Application::command(), the same path as vibmonctl.
//
// Input: tap (finger released near where it went down), horizontal swipe =
// previous/next page. On the live preview a press on the measuring box drags
// it (inside moves it, edges and corners resize it) and a tap elsewhere on the
// image moves it there. EC11: on the live page rotation zooms and a short press
// resets the zoom; on the history page rotation turns the list pages; on the
// other pages a short press returns to the live page; a long press cycles the
// pages.
class FbUi {
 public:
  explicit FbUi(Application& app) : app_(app) {}
  ~FbUi();
  bool start();
  // Front-panel encoder; called from the main loop thread.
  void onEncoder(int detents);
  void onEncoderPress(bool longPress);

  // Applies a touched action to the UI state; returns the command to send
  // to the application (null when the action is UI-local). Used by tests.
  static JsonValue applyAction(UiState& st, const UiAction& a, const Rect& region, int x, int y, const UiData& d,
                               int64_t nowMs);

 private:
  void run();
  void gather(UiData& d);
  // Tells the application what the screen shows, so camera, vision and
  // depth work runs only for what is seen.
  void updateDisplayNeeds();
  // Only the camera card changes: the live page without a dialog, a message
  // over it or a finger on it.
  bool cameraOnly(int64_t nowMs) const;
  void handleTouch(const TouchEvent& e, const UiData& d);
  void handleEncoder();
  void perform(const UiAction& a, const Rect& region, int x, int y, const UiData& d);
  void toast(const std::string& text);

  Application& app_;
  std::unique_ptr<Display> display_;
  std::unique_ptr<TextConsoleOff> console_;
  TouchInput touch_;
  Canvas canvas_;
  UiState st_;
  std::vector<HitRegion> hits_;
  bool dirty_ = false;
  bool fingerDown_ = false, moved_ = false;
  int downX_ = 0, downY_ = 0;
  // Measuring-box drag: image window of the pressed region, box at the press.
  UiAction dragAction_;
  Rect dragRegion_;
  NormBox dragStart_;
  int dragFw_ = 0, dragFh_ = 0, dragMaxPx_ = 0;
  bool dragging_ = false;
  bool panArmed_ = false;  // zoomed in: a drag that does not take the box moves the view
  int needs_ = -1;         // display needs last sent (bit 0 preview, 1 vision, 2 depth)

  std::mutex encMutex_;
  int encSteps_ = 0, encShort_ = 0, encLong_ = 0;

  std::vector<RecordSummary> records_;
  int64_t recordsMs_ = -100000;
  std::string recordsKey_;
  RecordView record_;

  std::thread thread_;
  std::atomic<bool> running_{false};
};

}  // namespace vm
