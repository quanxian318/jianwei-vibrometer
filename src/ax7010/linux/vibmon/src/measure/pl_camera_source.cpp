#include "measure/pl_camera_source.h"
#include "measure/frame_store_policy.h"

#include <pthread.h>
#include <sched.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif

#include "hw/phys_window.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {
namespace {

// BGR888 as the camera path stores it (OV5640 BGR565, remapped so that
// byte 0 = blue) to luma, (29 B + 150 G + 77 R + 128) >> 8, read straight from
// the uncached frame store in one pass; `copy` (optional) receives the BGR
// bytes for display.
void bgrToGray(const uint8_t* bgr, uint8_t* gray, uint8_t* copy, size_t n) {
  size_t i = 0;
#if defined(__ARM_NEON)
  const uint8x8_t kb = vdup_n_u8(29), kg = vdup_n_u8(150), kr = vdup_n_u8(77);
  for (; i + 16 <= n; i += 16) {
    const uint8x16x3_t p = vld3q_u8(bgr + 3 * i);
    if (copy) vst3q_u8(copy + 3 * i, p);
    uint16x8_t lo = vmull_u8(vget_low_u8(p.val[0]), kb);
    lo = vmlal_u8(lo, vget_low_u8(p.val[1]), kg);
    lo = vmlal_u8(lo, vget_low_u8(p.val[2]), kr);
    uint16x8_t hi = vmull_u8(vget_high_u8(p.val[0]), kb);
    hi = vmlal_u8(hi, vget_high_u8(p.val[1]), kg);
    hi = vmlal_u8(hi, vget_high_u8(p.val[2]), kr);
    vst1q_u8(gray + i, vcombine_u8(vrshrn_n_u16(lo, 8), vrshrn_n_u16(hi, 8)));
  }
#endif
  for (; i < n; ++i) {
    const uint8_t* q = bgr + 3 * i;
    gray[i] = static_cast<uint8_t>((29u * q[0] + 150u * q[1] + 77u * q[2] + 128u) >> 8);
    if (copy) memcpy(copy + 3 * i, q, 3);
  }
}

struct Capture {
  explicit Capture(const PlCameraConfig& c) : cfg(c) {}
  ~Capture() {
    watching = false;
    if (watcher.joinable()) watcher.join();
  }
  PlCameraConfig cfg;
  std::mutex mutex;
  std::unique_ptr<PlCameraRig> rig;
  std::atomic<bool> active[2] = {{false}, {false}};
  // Store each writer was last seen on and when it moved there, i.e. when the
  // store before it completed: noted every 1 ms by a real-time thread, so the
  // completion times do not depend on how busy the readers are (the team's
  // window check rejects an interval 10 %, 2.2 ms at 45 fps, off).
  std::thread watcher;
  std::atomic<bool> watching{false};
  std::mutex seenMutex;
  int seenStore[2] = {-1, -1};
  int64_t seenNs[2] = {0, 0};
  std::array<int64_t, 3> completed[2]{};
  int64_t clearedNs[2] = {0, 0}, loggedNs[2] = {0, 0};

  void observe() {
    const int64_t now = monoNs();
    std::lock_guard<std::mutex> lock(seenMutex);
    for (int c = 0; c < 2; ++c) {
      if (!active[c]) continue;
      const int s = rig->writingStore(c);
      if (s != seenStore[c]) {
        if (seenStore[c] >= 0 && s >= 0 && s < 3) completed[c][(s + 2) % 3] = now;
        seenStore[c] = s;
        seenNs[c] = now;
        continue;
      }
      // A frame with SOF/EOL timing errors (the partial first frame, a glitch
      // on the sensor lines) stops the writer until the error bits are
      // cleared, as the team's start-up loop does. Done here, for both
      // cameras, because a reader that only takes the newest frame (the
      // stereo right camera) never waits on a stopped writer.
      const int64_t kStuckNs = 100000000;
      if (now - seenNs[c] < kStuckNs || now - clearedNs[c] < kStuckNs) continue;
      clearedNs[c] = now;
      const uint32_t errors = rig->errors(c);
      if (!errors) continue;
      rig->clearErrors(c);
      if (now - loggedNs[c] > 60000000000LL) {
        loggedNs[c] = now;
        LOGW("plcam", "camera %d writer stopped on VDMA errors 0x%x; cleared", c, errors);
      }
    }
  }

  void watch() {
    sched_param sp{};
    sp.sched_priority = 10;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp) != 0)
      LOGW("plcam", "frame times without real-time priority (not root?)");
    while (watching) {
      observe();
      sleepMs(1);
    }
  }

  // When the writer of camera c moved to `store`; 0 if that was not seen.
  int64_t movedAt(int c, int store) {
    std::lock_guard<std::mutex> lock(seenMutex);
    return seenStore[c] == store ? seenNs[c] : 0;
  }
  std::array<int64_t, 3> completedAt(int c) {
    std::lock_guard<std::mutex> lock(seenMutex);
    return completed[c];
  }

  bool start(int cam) {
    std::lock_guard<std::mutex> lock(mutex);
    std::string err;
    if (!rig) {
      if (cfg.frameStride < PlCameraRig::kFrameBytes ||
          !reservedForDevices(cfg.bufferBase, 2ull * PlCameraRig::kStores * cfg.frameStride)) {
        LOGE("plcam", "camera DDR overlaps Linux RAM; prepare_an5642_image.py and reboot first");
        return false;
      }
      rig = PlCameraRig::open(cfg, err);
      if (!rig || !rig->initPair(err)) {
        LOGE("plcam", "%s", err.c_str());
        rig.reset();
        return false;
      }
    }
    if (!rig->startCapture(cam, err)) {
      LOGE("plcam", "camera %d: %s", cam, err.c_str());
      return false;
    }
    {
      std::lock_guard<std::mutex> seen(seenMutex);
      seenStore[cam] = -1;
      completed[cam].fill(0);
    }
    active[cam] = true;
    if (!watching.exchange(true)) watcher = std::thread(&Capture::watch, this);
    return true;
  }

  void stop(int cam) {
    std::lock_guard<std::mutex> lock(mutex);
    if (rig && active[cam]) rig->stopCapture(cam);
    active[cam] = false;
  }
};

class PlCameraSource : public FrameSource {
 public:
  PlCameraSource(std::shared_ptr<Capture> cap, int cam) : cap_(std::move(cap)), cam_(cam) {}
  ~PlCameraSource() override { stop(); }

  bool start() override {
    if (started_) return true;
    if (!cap_->start(cam_)) return false;
    const auto& cfg = cap_->cfg;
    if (!memory_.open(cap_->rig->storeAddress(cam_, 0), 3u * cfg.frameStride)) {
      cap_->stop(cam_);
      return false;
    }
    lastStore_ = cap_->rig->writingStore(cam_);
    sequence_ = 0;
    started_ = true;
    return true;
  }

  void stop() override {
    if (started_) cap_->stop(cam_);
    started_ = false;
    memory_.close();
  }

  bool next(GrayFrame& out, int timeoutMs) override { return read(out, timeoutMs, false); }
  bool latest(GrayFrame& out, int timeoutMs) override { return read(out, timeoutMs, true); }
  bool nearest(GrayFrame& out, int64_t target, int timeoutMs) override { return read(out, timeoutMs, true, target); }

  std::string describe() const override { return "AN5642 PL/VDMA camera " + std::to_string(cam_); }
  double nominalFps() const override { return cap_->cfg.fps; }

 private:
  bool read(GrayFrame& out, int timeoutMs, bool latest, int64_t target = 0) {
    if (!started_) return false;
    // A store is rewritten two frame times after it completed.
    const int64_t reuseNs = static_cast<int64_t>(2e9 / cap_->cfg.fps) - 4000000;
    const int64_t deadline = monoMs() + timeoutMs;
    while (monoMs() < deadline) {
      cap_->observe();
      const int store = cap_->rig->writingStore(cam_);
      const int64_t moved = cap_->movedAt(cam_, store);
      if (store < 0 || store >= 3 || (store == lastStore_ && !(latest && moved > 0))) {
        sleepMs(1);
        continue;
      }
      sequence_ += (store - lastStore_ + 3) % 3;
      lastStore_ = store;
      const auto history = cap_->completedAt(cam_);
      const int done = target > 0 ? nearestCompletedStore(history, store, target, monoNs(), reuseNs) : (store + 2) % 3;
      if (done < 0) { sleepMs(1); continue; }
      const int64_t stamp = target > 0 ? history[done] : (moved > 0 ? moved : monoNs());
      out.width = PlCameraRig::kWidth;
      out.height = PlCameraRig::kHeight;
      if (!out.skipPixels) {
        const size_t n = static_cast<size_t>(out.width) * out.height;
        const uint8_t* src = memory_.bytes() + done * cap_->cfg.frameStride;
        if (out.wantColor && out.colorOnly) {
          out.bgr.resize(3 * n);
          // A contiguous burst copy is substantially cheaper than deinterleave
          // plus grayscale conversion when only the colour card is visible.
          memcpy(out.bgr.data(), src, 3 * n);
          out.pixels.clear();
        } else if (out.wantColor || out.regions.empty()) {
          // Snapshot uncached DDR in a burst first. Doing SIMD arithmetic
          // directly on device memory delayed pairing and store validation.
          auto& snapshot = out.wantColor ? out.bgr : snapshot_;
          snapshot.resize(3 * n);
          memcpy(snapshot.data(), src, 3 * n);
          if (cap_->rig->writingStore(cam_) == done || monoNs() - stamp > reuseNs ||
              (target > 0 && cap_->completedAt(cam_)[done] != stamp)) continue;
          out.pixels.resize(n);
          bgrToGray(snapshot.data(), out.pixels.data(), nullptr, n);
        } else {
          out.pixels.resize(n);
          for (const Roi& r : out.regions) {
            const int x0 = std::max(0, r.x), x1 = std::min(out.width, r.x + r.w);
            const int y0 = std::max(0, r.y), y1 = std::min(out.height, r.y + r.h);
            for (int y = y0; y < y1 && x0 < x1; ++y) {
              const size_t o = static_cast<size_t>(y) * out.width + x0;
              bgrToGray(src + 3 * o, out.pixels.data() + o, nullptr, static_cast<size_t>(x1 - x0));
            }
          }
        }
        // The writer comes back to `done` two frame times after it completed;
        // a read that ended later may mix two frames.
        // Full snapshots were validated before cached conversion; their
        // original hardware store can now be reused without corrupting them.
        if ((out.colorOnly || !out.regions.empty()) &&
            (cap_->rig->writingStore(cam_) == done || monoNs() - stamp > reuseNs)) continue;
      }
      if (!out.wantColor || out.skipPixels) out.bgr.clear();
      const uint32_t errors = cap_->rig->errors(cam_);
      if (errors) {
        cap_->rig->clearErrors(cam_);
        LOGW("plcam", "camera %d VDMA errors 0x%x", cam_, errors);
        continue;
      }
      out.sequence = sequence_;
      // When the writer moved on (1 ms polling), like a V4L2 buffer time: a
      // completion time, not the exposure; analysis bounds the jitter.
      out.timestampNs = stamp;
      out.timestampValid = true;
      return true;
    }
    return false;
  }

  std::shared_ptr<Capture> cap_;
  int cam_, lastStore_ = 0;
  uint32_t sequence_ = 0;
  bool started_ = false;
  PhysWindow memory_;
  std::vector<uint8_t> snapshot_;
};

}  // namespace

std::array<std::unique_ptr<FrameSource>, 2> makePlCameraSources(const PlCameraConfig& cfg) {
  auto cap = std::make_shared<Capture>(cfg);
  const int left = cfg.leftCamera == 1 ? 1 : 0;
  return {std::unique_ptr<FrameSource>(new PlCameraSource(cap, left)),
          std::unique_ptr<FrameSource>(new PlCameraSource(cap, 1 - left))};
}

}  // namespace vm
