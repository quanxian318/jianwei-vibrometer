#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "gfx/canvas.h"

namespace vm {

// Where the UI frames go: /dev/fb0 (FbDisplay) or the PL video path of the
// camera design (PlDisplay).
class Display {
 public:
  virtual ~Display() = default;
  virtual int width() const = 0;
  virtual int height() const = 0;
  virtual void present(const Canvas& c) = 0;
  virtual void presentRegion(const Canvas& c, int, int, int, int) { present(c); }
};

// Puts the active virtual terminal in graphics mode while the UI runs, so the
// text console (login prompt, kernel messages, cursor, blanking) stays off
// the screen; restores text mode on destruction.
class TextConsoleOff {
 public:
  TextConsoleOff();
  ~TextConsoleOff();

 private:
  int fd_ = -1;
};

// Linux framebuffer output (16/24/32 bpp). The HDMI path of the vendor base
// design exposes /dev/fb0 through DRM fbdev emulation.
class FbDisplay : public Display {
 public:
  ~FbDisplay() override;
  bool open(const std::string& device);
  int width() const override { return w_; }
  int height() const override { return h_; }
  void present(const Canvas& c) override;

 private:
  int fd_ = -1;
  uint8_t* mem_ = nullptr;
  size_t size_ = 0;
  int w_ = 0, h_ = 0, bpp_ = 0, stride_ = 0;
  int rOff_ = 16, gOff_ = 8, bOff_ = 0, rLen_ = 8, gLen_ = 8, bLen_ = 8;
};

struct TouchEvent {
  enum Type { kDown, kMove, kUp } type;
  int x, y;  // screen pixels
};

// evdev touchscreen (USB HID multi-touch of the 7inch HDMI LCD (H)).
class TouchInput {
 public:
  ~TouchInput();
  bool open(const std::string& device);  // "auto" scans /dev/input/event*
  void setScreen(int w, int h, bool swapXY, bool invertX, bool invertY);
  int fd() const { return fd_; }
  // Reads the pending input and appends finger down / move / up transitions.
  void read(std::vector<TouchEvent>& out);

 private:
  bool probe(const std::string& path);
  void toScreen(int& x, int& y) const;
  int fd_ = -1;
  int minX_ = 0, maxX_ = 4095, minY_ = 0, maxY_ = 4095;
  int sw_ = 1024, sh_ = 600;
  bool swap_ = false, invX_ = false, invY_ = false;
  int rawX_ = 0, rawY_ = 0, lastX_ = -1, lastY_ = -1;
  bool down_ = false, downPending_ = false, upPending_ = false;
};

}  // namespace vm
