#include <cmath>
#include <random>

#include "measure/frame_source.h"
#include "util/clock.h"

namespace vm {

namespace {

// Bench/test source: a smoothed random texture; the central third of the
// image moves sinusoidally with sub-pixel (bilinear) shifts.
class SyntheticSource : public FrameSource {
 public:
  explicit SyntheticSource(const SyntheticConfig& cfg) : cfg_(cfg), rng_(1234) {
    texW_ = cfg_.width + 64;
    texH_ = cfg_.height + 64;
    std::vector<float> raw(static_cast<size_t>(texW_) * texH_);
    std::uniform_real_distribution<float> u(0.f, 255.f);
    for (auto& v : raw) v = u(rng_);
    tex_.assign(raw.size(), 0.f);
    const int r = 2;  // box blur keeps the texture band-limited so sub-pixel motion is visible
    for (int y = 0; y < texH_; ++y) {
      for (int x = 0; x < texW_; ++x) {
        float s = 0;
        int n = 0;
        for (int j = -r; j <= r; ++j) {
          for (int i = -r; i <= r; ++i) {
            int xx = x + i, yy = y + j;
            if (xx < 0 || yy < 0 || xx >= texW_ || yy >= texH_) continue;
            s += raw[static_cast<size_t>(yy) * texW_ + xx];
            ++n;
          }
        }
        tex_[static_cast<size_t>(y) * texW_ + x] = s / n;
      }
    }
    // Stretch contrast back to roughly 20..235.
    float lo = 255, hi = 0;
    for (float v : tex_) {
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
    for (auto& v : tex_) v = 20.f + (v - lo) * 215.f / std::max(1.f, hi - lo);
  }

  bool start() override {
    frame_ = 0;
    seq_ = 0;
    t0_ = monoNs();
    return true;
  }
  void stop() override {}

  bool next(GrayFrame& out, int timeoutMs) override {
    (void)timeoutMs;
    const int64_t periodNs = static_cast<int64_t>(1e9 / cfg_.fps);
    if (cfg_.dropEvery > 0 && frame_ > 0 && frame_ % cfg_.dropEvery == 0) {
      frame_++;  // this frame is "lost"
      seq_++;
    }
    const int64_t due = t0_ + frame_ * periodNs;
    if (cfg_.realtime) {
      int64_t wait = due - monoNs();
      if (wait > 0) sleepMs(static_cast<int>(wait / 1000000));
    }
    // The image phase is generated at due; host scheduling delays are not acquisition jitter.
    out.timestampNs = due;
    const double t = static_cast<double>(due - t0_) * 1e-9;
    const double d = cfg_.amplitudePx * std::sin(2 * M_PI * cfg_.freqHz * t);
    const double dx = cfg_.axis == 'x' ? d : 0.0;
    const double dy = cfg_.axis == 'y' ? d : 0.0;
    render(out, dx, dy);
    out.sequence = seq_++;
    frame_++;
    return true;
  }

  std::string describe() const override { return "synthetic"; }
  double nominalFps() const override { return cfg_.fps; }

 private:
  float sample(double x, double y) const {
    int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    float fx = static_cast<float>(x - x0), fy = static_cast<float>(y - y0);
    x0 = std::max(0, std::min(texW_ - 2, x0));
    y0 = std::max(0, std::min(texH_ - 2, y0));
    const float* p = &tex_[static_cast<size_t>(y0) * texW_ + x0];
    float a = p[0] + (p[1] - p[0]) * fx;
    float b = p[texW_] + (p[texW_ + 1] - p[texW_]) * fx;
    return a + (b - a) * fy;
  }

  void render(GrayFrame& out, double dx, double dy) {
    out.width = cfg_.width;
    out.height = cfg_.height;
    out.pixels.resize(static_cast<size_t>(cfg_.width) * cfg_.height);
    std::normal_distribution<float> noise(0.f, static_cast<float>(cfg_.noise));
    const int x1 = cfg_.width / 3, x2 = 2 * cfg_.width / 3, y1 = cfg_.height / 3, y2 = 2 * cfg_.height / 3;
    for (int y = 0; y < cfg_.height; ++y) {
      for (int x = 0; x < cfg_.width; ++x) {
        const bool moving = x >= x1 && x < x2 && y >= y1 && y < y2;
        float v = moving ? sample(x + 32 - dx, y + 32 - dy) : tex_[static_cast<size_t>(y + 32) * texW_ + x + 32];
        if (cfg_.noise > 0) v += noise(rng_);
        out.pixels[static_cast<size_t>(y) * cfg_.width + x] =
            static_cast<uint8_t>(v < 0 ? 0 : (v > 255 ? 255 : std::lround(v)));
      }
    }
  }

  SyntheticConfig cfg_;
  std::mt19937 rng_;
  std::vector<float> tex_;
  int texW_ = 0, texH_ = 0;
  int64_t t0_ = 0;
  int64_t frame_ = 0;
  uint32_t seq_ = 0;
};

}  // namespace

std::unique_ptr<FrameSource> makeSyntheticSource(const SyntheticConfig& cfg) {
  return std::unique_ptr<FrameSource>(new SyntheticSource(cfg));
}

}  // namespace vm
