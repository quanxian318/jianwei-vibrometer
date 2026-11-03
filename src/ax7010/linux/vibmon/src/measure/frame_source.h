#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vm {

struct Roi {
  int x = 0, y = 0, w = 0, h = 0;
};

struct GrayFrame {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;  // width * height, row major
  int64_t timestampNs = 0;      // CLOCK_MONOTONIC capture time
  bool timestampValid = true;  // false for dequeue-time fallback, unsuitable for metrology
  uint32_t sequence = 0;        // driver sequence; gaps mean dropped frames
  // What the reader needs of the next frame. skipPixels: only its time and
  // sequence (a source may leave pixels as they are). regions: when not
  // empty, only the pixels inside them. wantColor: also a colour copy of the
  // whole frame for display in bgr, left empty by sources without colour.
  bool skipPixels = false;
  std::vector<Roi> regions;
  bool wantColor = false;
  // Optional display-only fast path. No consumer may use pixels from such a
  // frame; a capture requested during the read must wait for a gray frame.
  bool colorOnly = false;
  std::vector<uint8_t> bgr;  // width * height * 3, byte 0 = blue
  uint8_t at(int x, int y) const { return pixels[static_cast<size_t>(y) * width + x]; }
};

// Camera abstraction. The AN5642 path of the vendor base project is expected
// to appear as a V4L2 capture device; any other vendor interface plugs in by
// implementing this class.
class FrameSource {
 public:
  virtual ~FrameSource() = default;
  virtual bool start() = 0;
  virtual void stop() = 0;
  virtual bool next(GrayFrame& out, int timeoutMs) = 0;
  // The most recently completed frame without waiting for a new one (it may
  // be one next() returned before); for readers that sample a running camera
  // now and then. Sources that cannot tell when a frame completed use next().
  virtual bool latest(GrayFrame& out, int timeoutMs) { return next(out, timeoutMs); }
  // Best still-readable completed frame near a partner's capture timestamp.
  // No source may change its timestamp just to satisfy pairing tolerance.
  virtual bool nearest(GrayFrame& out, int64_t, int timeoutMs) { return latest(out, timeoutMs); }
  virtual std::string describe() const = 0;
  virtual double nominalFps() const = 0;
};

struct V4l2Config {
  std::string device = "/dev/video0";
  int width = 1280;
  int height = 720;
  std::string pixelFormat = "YUYV";  // YUYV, UYVY, GREY, NV12, NV16, RGB3, BGR3
  int fps = 30;
  int buffers = 4;
};
std::unique_ptr<FrameSource> makeV4l2Source(const V4l2Config& cfg);

// Textured target moving by amplitudePx * sin(2 pi f t) along the chosen axis.
struct SyntheticConfig {
  int width = 640;
  int height = 480;
  double fps = 60;
  double freqHz = 5;
  double amplitudePx = 2;
  char axis = 'y';
  double noise = 2;        // gray-level standard deviation
  bool realtime = true;    // false: no sleeping, timestamps advance by 1/fps
  int dropEvery = 0;       // simulate a dropped frame every N frames (0 = never)
};
std::unique_ptr<FrameSource> makeSyntheticSource(const SyntheticConfig& cfg);

}  // namespace vm
