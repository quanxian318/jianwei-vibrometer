#include <fcntl.h>
#include <linux/videodev2.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "measure/frame_source.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "v4l2";

int xioctl(int fd, unsigned long req, void* arg) {
  int r;
  do {
    r = ioctl(fd, req, arg);
  } while (r < 0 && errno == EINTR);
  return r;
}

uint32_t fourcc(const std::string& s) {
  if (s.size() != 4) return V4L2_PIX_FMT_YUYV;
  return v4l2_fourcc(s[0], s[1], s[2], s[3]);
}

std::string fourccName(uint32_t f) {
  char b[5] = {static_cast<char>(f & 0xFF), static_cast<char>((f >> 8) & 0xFF), static_cast<char>((f >> 16) & 0xFF),
               static_cast<char>((f >> 24) & 0xFF), 0};
  return b;
}

class V4l2Source : public FrameSource {
 public:
  explicit V4l2Source(const V4l2Config& cfg) : cfg_(cfg) {}
  ~V4l2Source() override { stop(); }

  bool start() override {
    stop();
    fd_ = ::open(cfg_.device.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd_ < 0) {
      LOGE(kTag, "open %s: %s", cfg_.device.c_str(), strerror(errno));
      return false;
    }
    v4l2_capability cap{};
    if (xioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) return fail("VIDIOC_QUERYCAP");
    const uint32_t caps = (cap.capabilities & V4L2_CAP_DEVICE_CAPS) ? cap.device_caps : cap.capabilities;
    if (caps & V4L2_CAP_VIDEO_CAPTURE) {
      type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    } else if (caps & V4L2_CAP_VIDEO_CAPTURE_MPLANE) {
      type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    } else {
      return fail("not a capture device");
    }
    if (!(caps & V4L2_CAP_STREAMING)) return fail("no streaming I/O");

    v4l2_format fmt{};
    fmt.type = type_;
    if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
      fmt.fmt.pix.width = static_cast<uint32_t>(cfg_.width);
      fmt.fmt.pix.height = static_cast<uint32_t>(cfg_.height);
      fmt.fmt.pix.pixelformat = fourcc(cfg_.pixelFormat);
      fmt.fmt.pix.field = V4L2_FIELD_NONE;
    } else {
      fmt.fmt.pix_mp.width = static_cast<uint32_t>(cfg_.width);
      fmt.fmt.pix_mp.height = static_cast<uint32_t>(cfg_.height);
      fmt.fmt.pix_mp.pixelformat = fourcc(cfg_.pixelFormat);
      fmt.fmt.pix_mp.field = V4L2_FIELD_NONE;
      fmt.fmt.pix_mp.num_planes = 1;
    }
    if (xioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
      LOGW(kTag, "VIDIOC_S_FMT rejected, using the current format");
      xioctl(fd_, VIDIOC_G_FMT, &fmt);
    }
    if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE) {
      width_ = static_cast<int>(fmt.fmt.pix.width);
      height_ = static_cast<int>(fmt.fmt.pix.height);
      pixfmt_ = fmt.fmt.pix.pixelformat;
      stride_ = static_cast<int>(fmt.fmt.pix.bytesperline);
    } else {
      width_ = static_cast<int>(fmt.fmt.pix_mp.width);
      height_ = static_cast<int>(fmt.fmt.pix_mp.height);
      pixfmt_ = fmt.fmt.pix_mp.pixelformat;
      stride_ = static_cast<int>(fmt.fmt.pix_mp.plane_fmt[0].bytesperline);
    }
    if (stride_ <= 0) stride_ = width_ * bytesPerPixel();

    if (cfg_.fps > 0) {
      v4l2_streamparm parm{};
      parm.type = type_;
      parm.parm.capture.timeperframe.numerator = 1;
      parm.parm.capture.timeperframe.denominator = static_cast<uint32_t>(cfg_.fps);
      if (xioctl(fd_, VIDIOC_S_PARM, &parm) < 0) LOGD(kTag, "VIDIOC_S_PARM not supported");
    }

    v4l2_requestbuffers req{};
    req.count = static_cast<uint32_t>(cfg_.buffers);
    req.type = type_;
    req.memory = V4L2_MEMORY_MMAP;
    if (xioctl(fd_, VIDIOC_REQBUFS, &req) < 0 || req.count < 2) return fail("VIDIOC_REQBUFS");
    for (uint32_t i = 0; i < req.count; ++i) {
      v4l2_buffer b{};
      v4l2_plane planes[1]{};
      b.type = type_;
      b.memory = V4L2_MEMORY_MMAP;
      b.index = i;
      if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
        b.m.planes = planes;
        b.length = 1;
      }
      if (xioctl(fd_, VIDIOC_QUERYBUF, &b) < 0) return fail("VIDIOC_QUERYBUF");
      size_t len = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE ? b.length : planes[0].length;
      off_t off = type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE ? static_cast<off_t>(b.m.offset)
                                                       : static_cast<off_t>(planes[0].m.mem_offset);
      void* p = mmap(nullptr, len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, off);
      if (p == MAP_FAILED) return fail("mmap");
      bufs_.push_back({p, len});
      if (xioctl(fd_, VIDIOC_QBUF, &b) < 0) return fail("VIDIOC_QBUF");
    }
    int t = static_cast<int>(type_);
    if (xioctl(fd_, VIDIOC_STREAMON, &t) < 0) return fail("VIDIOC_STREAMON");
    LOGI(kTag, "%s streaming %dx%d %s, %zu buffers", cfg_.device.c_str(), width_, height_, fourccName(pixfmt_).c_str(),
         bufs_.size());
    return true;
  }

  void stop() override {
    if (fd_ >= 0) {
      int t = static_cast<int>(type_);
      xioctl(fd_, VIDIOC_STREAMOFF, &t);
    }
    for (auto& b : bufs_) munmap(b.first, b.second);
    bufs_.clear();
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }

  bool next(GrayFrame& out, int timeoutMs) override {
    if (fd_ < 0) return false;
    pollfd p{fd_, POLLIN, 0};
    if (poll(&p, 1, timeoutMs) <= 0) return false;
    v4l2_buffer b{};
    v4l2_plane planes[1]{};
    b.type = type_;
    b.memory = V4L2_MEMORY_MMAP;
    if (type_ == V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE) {
      b.m.planes = planes;
      b.length = 1;
    }
    if (xioctl(fd_, VIDIOC_DQBUF, &b) < 0) return false;
    const int64_t dqNs = monoNs();
    const uint8_t* src = static_cast<const uint8_t*>(bufs_[b.index].first);
    convert(src, out);
    if ((b.flags & V4L2_BUF_FLAG_TIMESTAMP_MASK) == V4L2_BUF_FLAG_TIMESTAMP_MONOTONIC &&
        (b.timestamp.tv_sec != 0 || b.timestamp.tv_usec != 0)) {
      out.timestampValid = true;
      out.timestampNs = static_cast<int64_t>(b.timestamp.tv_sec) * 1000000000LL + b.timestamp.tv_usec * 1000LL;
    } else {
      out.timestampValid = false;
      out.timestampNs = dqNs;
    }
    out.sequence = b.sequence;
    xioctl(fd_, VIDIOC_QBUF, &b);
    return true;
  }

  std::string describe() const override {
    return cfg_.device + " " + std::to_string(width_) + "x" + std::to_string(height_) + " " + fourccName(pixfmt_);
  }
  double nominalFps() const override { return cfg_.fps; }

 private:
  bool fail(const char* what) {
    LOGE(kTag, "%s: %s failed: %s", cfg_.device.c_str(), what, strerror(errno));
    stop();
    return false;
  }

  int bytesPerPixel() const {
    switch (pixfmt_) {
      case V4L2_PIX_FMT_YUYV:
      case V4L2_PIX_FMT_UYVY: return 2;
      case V4L2_PIX_FMT_RGB24:
      case V4L2_PIX_FMT_BGR24: return 3;
      default: return 1;
    }
  }

  void convert(const uint8_t* src, GrayFrame& out) {
    out.width = width_;
    out.height = height_;
    out.pixels.resize(static_cast<size_t>(width_) * height_);
    uint8_t* dst = out.pixels.data();
    for (int y = 0; y < height_; ++y) {
      const uint8_t* row = src + static_cast<size_t>(y) * stride_;
      uint8_t* d = dst + static_cast<size_t>(y) * width_;
      switch (pixfmt_) {
        case V4L2_PIX_FMT_YUYV:
          for (int x = 0; x < width_; ++x) d[x] = row[2 * x];
          break;
        case V4L2_PIX_FMT_UYVY:
          for (int x = 0; x < width_; ++x) d[x] = row[2 * x + 1];
          break;
        case V4L2_PIX_FMT_RGB24:
          for (int x = 0; x < width_; ++x)
            d[x] = static_cast<uint8_t>((77 * row[3 * x] + 150 * row[3 * x + 1] + 29 * row[3 * x + 2]) >> 8);
          break;
        case V4L2_PIX_FMT_BGR24:
          for (int x = 0; x < width_; ++x)
            d[x] = static_cast<uint8_t>((29 * row[3 * x] + 150 * row[3 * x + 1] + 77 * row[3 * x + 2]) >> 8);
          break;
        default:  // GREY and the Y plane of NV12/NV16/YUV420
          memcpy(d, row, static_cast<size_t>(width_));
          break;
      }
    }
  }

  V4l2Config cfg_;
  int fd_ = -1;
  uint32_t type_ = V4L2_BUF_TYPE_VIDEO_CAPTURE;
  int width_ = 0, height_ = 0, stride_ = 0;
  uint32_t pixfmt_ = 0;
  std::vector<std::pair<void*, size_t>> bufs_;
};

}  // namespace

std::unique_ptr<FrameSource> makeV4l2Source(const V4l2Config& cfg) {
  return std::unique_ptr<FrameSource>(new V4l2Source(cfg));
}

}  // namespace vm
