#include "ui/fb_display.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include "util/log.h"

namespace vm {

TextConsoleOff::TextConsoleOff() {
  fd_ = ::open("/dev/tty0", O_RDWR | O_CLOEXEC);
  if (fd_ < 0 || ioctl(fd_, KDSETMODE, KD_GRAPHICS) != 0) {
    LOGW("ui", "text console not switched off: %s", strerror(errno));
    if (fd_ >= 0) ::close(fd_);
    fd_ = -1;
  }
}

TextConsoleOff::~TextConsoleOff() {
  if (fd_ < 0) return;
  ioctl(fd_, KDSETMODE, KD_TEXT);
  ::close(fd_);
}

FbDisplay::~FbDisplay() {
  if (mem_) munmap(mem_, size_);
  if (fd_ >= 0) ::close(fd_);
}

bool FbDisplay::open(const std::string& device) {
  fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    LOGW("ui", "open %s: %s", device.c_str(), strerror(errno));
    return false;
  }
  fb_var_screeninfo v{};
  fb_fix_screeninfo f{};
  if (ioctl(fd_, FBIOGET_VSCREENINFO, &v) != 0 || ioctl(fd_, FBIOGET_FSCREENINFO, &f) != 0) return false;
  w_ = static_cast<int>(v.xres);
  h_ = static_cast<int>(v.yres);
  bpp_ = static_cast<int>(v.bits_per_pixel);
  stride_ = static_cast<int>(f.line_length);
  rOff_ = static_cast<int>(v.red.offset);
  gOff_ = static_cast<int>(v.green.offset);
  bOff_ = static_cast<int>(v.blue.offset);
  rLen_ = static_cast<int>(v.red.length);
  gLen_ = static_cast<int>(v.green.length);
  bLen_ = static_cast<int>(v.blue.length);
  if (bpp_ != 16 && bpp_ != 24 && bpp_ != 32) {
    LOGW("ui", "%s: unsupported %d bpp", device.c_str(), bpp_);
    return false;
  }
  size_ = f.smem_len;
  void* p = mmap(nullptr, size_, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
  if (p == MAP_FAILED) return false;
  mem_ = static_cast<uint8_t*>(p);
  LOGI("ui", "%s %dx%d %d bpp", device.c_str(), w_, h_, bpp_);
  return true;
}

void FbDisplay::present(const Canvas& c) {
  if (!mem_) return;
  const int w = c.width() < w_ ? c.width() : w_;
  const int h = c.height() < h_ ? c.height() : h_;
  for (int y = 0; y < h; ++y) {
    const uint32_t* src = c.data() + static_cast<size_t>(y) * c.width();
    uint8_t* row = mem_ + static_cast<size_t>(y) * stride_;
    for (int x = 0; x < w; ++x) {
      const uint32_t p = src[x];
      const uint32_t r = (p >> 16) & 0xFF, g = (p >> 8) & 0xFF, b = p & 0xFF;
      const uint32_t v = ((r >> (8 - rLen_)) << rOff_) | ((g >> (8 - gLen_)) << gOff_) | ((b >> (8 - bLen_)) << bOff_);
      if (bpp_ == 32) {
        reinterpret_cast<uint32_t*>(row)[x] = v;
      } else if (bpp_ == 16) {
        reinterpret_cast<uint16_t*>(row)[x] = static_cast<uint16_t>(v);
      } else {
        row[3 * x] = static_cast<uint8_t>(v);
        row[3 * x + 1] = static_cast<uint8_t>(v >> 8);
        row[3 * x + 2] = static_cast<uint8_t>(v >> 16);
      }
    }
  }
}

namespace {
bool testBit(const unsigned long* bits, int n) {
  const int w = static_cast<int>(sizeof(unsigned long) * 8);
  return (bits[n / w] >> (n % w)) & 1UL;
}
}  // namespace

TouchInput::~TouchInput() {
  if (fd_ >= 0) ::close(fd_);
}

bool TouchInput::probe(const std::string& path) {
  int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
  if (fd < 0) return false;
  unsigned long abs[(ABS_MAX + 1 + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
  unsigned long keys[(KEY_MAX + 1 + 8 * sizeof(unsigned long) - 1) / (8 * sizeof(unsigned long))] = {0};
  ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
  ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
  const bool mt = testBit(abs, ABS_MT_POSITION_X) && testBit(abs, ABS_MT_POSITION_Y);
  const bool st = testBit(abs, ABS_X) && testBit(abs, ABS_Y);
  if (!(mt || st) || !testBit(keys, BTN_TOUCH)) {
    ::close(fd);
    return false;
  }
  input_absinfo ax{}, ay{};
  ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_X : ABS_X), &ax);
  ioctl(fd, EVIOCGABS(mt ? ABS_MT_POSITION_Y : ABS_Y), &ay);
  minX_ = ax.minimum;
  maxX_ = ax.maximum > ax.minimum ? ax.maximum : ax.minimum + 1;
  minY_ = ay.minimum;
  maxY_ = ay.maximum > ay.minimum ? ay.maximum : ay.minimum + 1;
  fd_ = fd;
  LOGI("ui", "touch %s (%s, x %d..%d, y %d..%d)", path.c_str(), mt ? "multi-touch" : "single", minX_, maxX_, minY_,
       maxY_);
  return true;
}

bool TouchInput::open(const std::string& device) {
  if (device != "auto") return probe(device);
  DIR* d = opendir("/dev/input");
  if (!d) return false;
  bool ok = false;
  while (dirent* e = readdir(d)) {
    if (strncmp(e->d_name, "event", 5) == 0 && probe(std::string("/dev/input/") + e->d_name)) {
      ok = true;
      break;
    }
  }
  closedir(d);
  return ok;
}

void TouchInput::setScreen(int w, int h, bool swapXY, bool invertX, bool invertY) {
  sw_ = w;
  sh_ = h;
  swap_ = swapXY;
  invX_ = invertX;
  invY_ = invertY;
}

void TouchInput::toScreen(int& x, int& y) const {
  double nx = static_cast<double>(rawX_ - minX_) / (maxX_ - minX_);
  double ny = static_cast<double>(rawY_ - minY_) / (maxY_ - minY_);
  if (swap_) {
    const double t = nx;
    nx = ny;
    ny = t;
  }
  if (invX_) nx = 1 - nx;
  if (invY_) ny = 1 - ny;
  x = std::max(0, std::min(sw_ - 1, static_cast<int>(nx * sw_)));
  y = std::max(0, std::min(sh_ - 1, static_cast<int>(ny * sh_)));
}

void TouchInput::read(std::vector<TouchEvent>& out) {
  if (fd_ < 0) return;
  input_event ev;
  ssize_t n;
  while ((n = ::read(fd_, &ev, sizeof(ev))) == static_cast<ssize_t>(sizeof(ev))) {
    if (ev.type == EV_ABS) {
      if (ev.code == ABS_MT_POSITION_X || ev.code == ABS_X) rawX_ = ev.value;
      if (ev.code == ABS_MT_POSITION_Y || ev.code == ABS_Y) rawY_ = ev.value;
    } else if (ev.type == EV_KEY && ev.code == BTN_TOUCH) {
      if (ev.value && !down_) {
        down_ = true;
        downPending_ = true;
      } else if (!ev.value && down_) {
        down_ = false;
        upPending_ = true;
      }
    } else if (ev.type == EV_SYN && ev.code == SYN_REPORT) {
      int x, y;
      toScreen(x, y);
      if (downPending_) {
        out.push_back(TouchEvent{TouchEvent::kDown, x, y});
        downPending_ = false;
      } else if (down_ && (x != lastX_ || y != lastY_)) {
        out.push_back(TouchEvent{TouchEvent::kMove, x, y});
      }
      if (upPending_) {
        out.push_back(TouchEvent{TouchEvent::kUp, x, y});
        upPending_ = false;
      }
      lastX_ = x;
      lastY_ = y;
    }
  }
  if (n < 0 && errno == ENODEV) {  // unplugged or enumerated again: open() finds it anew
    LOGW("ui", "touch device gone");
    ::close(fd_);
    fd_ = -1;
    down_ = downPending_ = upPending_ = false;
  }
}

}  // namespace vm
