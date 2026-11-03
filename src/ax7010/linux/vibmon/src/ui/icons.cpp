#include "ui/icons.h"

#include <cmath>
#include <vector>

#include "gfx/draw.h"

namespace vm {

namespace {
constexpr float kPi = 3.14159265f;

// Drawing in unit coordinates: (-1, -1) top-left .. (1, 1) bottom-right.
struct Pen {
  Canvas& c;
  float cx, cy, s, w;  // s = half size in pixels, w = stroke width in pixels
  uint32_t col;
  float X(float u) const { return cx + u * s; }
  float Y(float v) const { return cy + v * s; }
  void line(float x0, float y0, float x1, float y1, float k = 1.f) const {
    lineAA(c, X(x0), Y(y0), X(x1), Y(y1), w * k, col);
  }
  void poly(std::initializer_list<float> pts, float k = 1.f) const {
    const std::vector<float> p(pts);
    for (size_t i = 2; i + 1 < p.size(); i += 2) line(p[i - 2], p[i - 1], p[i], p[i + 1], k);
  }
  void ring(float x, float y, float r, float k = 1.f) const { strokeCircle(c, X(x), Y(y), r * s, w * k, col); }
  void dot(float x, float y, float r) const { fillCircle(c, X(x), Y(y), r * s, col); }
  void arc(float x, float y, float r, float a0, float a1, float k = 1.f) const {
    strokeArc(c, X(x), Y(y), r * s, w * k, a0, a1, col);
  }
  void rrect(float x0, float y0, float x1, float y1, float r) const {
    const int ix = static_cast<int>(std::lround(X(x0))), iy = static_cast<int>(std::lround(Y(y0)));
    const int iw = static_cast<int>(std::lround(X(x1))) - ix, ih = static_cast<int>(std::lround(Y(y1))) - iy;
    strokeRoundRect(c, ix, iy, iw, ih, static_cast<int>(r * s), std::max(1, static_cast<int>(std::lround(w))), col);
  }
  void fillRect(float x0, float y0, float x1, float y1, float r) const {
    const int ix = static_cast<int>(std::lround(X(x0))), iy = static_cast<int>(std::lround(Y(y0)));
    const int iw = static_cast<int>(std::lround(X(x1))) - ix, ih = static_cast<int>(std::lround(Y(y1))) - iy;
    fillRoundRect(c, ix, iy, iw, ih, static_cast<int>(r * s), col);
  }
  void fill(std::initializer_list<float> pts) const {
    std::vector<float> p;
    const std::vector<float> u(pts);
    for (size_t i = 0; i + 1 < u.size(); i += 2) {
      p.push_back(X(u[i]));
      p.push_back(Y(u[i + 1]));
    }
    fillPolygon(c, p.data(), static_cast<int>(p.size() / 2), col);
  }
};

void sine(const Pen& p, float x0, float x1, float y, float amp, float cycles, float k = 1.f) {
  const int n = 24;
  float px = x0, py = y;
  for (int i = 1; i <= n; ++i) {
    const float t = static_cast<float>(i) / n;
    const float x = x0 + (x1 - x0) * t;
    const float yy = y - amp * std::sin(t * cycles * 2 * kPi);
    p.line(px, py, x, yy, k);
    px = x;
    py = yy;
  }
}
}  // namespace

void drawIcon(Canvas& c, Icon icon, float cx, float cy, float size, uint32_t color) {
  const Pen p{c, cx, cy, size * 0.5f, std::max(1.4f, size * 0.085f), color};
  switch (icon) {
    case Icon::kNone:
      break;
    case Icon::kLogo:
      p.ring(0, 0, 0.86f, 1.1f);
      sine(p, -0.62f, 0.62f, 0, 0.32f, 1.5f, 1.1f);
      p.dot(0.62f, 0, 0.1f);
      break;
    case Icon::kCamera:
      p.rrect(-0.88f, -0.48f, 0.88f, 0.72f, 0.16f);
      p.poly({-0.36f, -0.48f, -0.2f, -0.72f, 0.2f, -0.72f, 0.36f, -0.48f});
      p.ring(0, 0.12f, 0.33f);
      p.dot(0.62f, -0.22f, 0.07f);
      break;
    case Icon::kChart:
      p.poly({-0.86f, -0.82f, -0.86f, 0.82f, 0.86f, 0.82f});
      p.poly({-0.58f, 0.42f, -0.24f, -0.08f, 0.1f, 0.26f, 0.62f, -0.52f});
      p.dot(0.62f, -0.52f, 0.11f);
      break;
    case Icon::kVision:
      p.ring(-0.16f, -0.16f, 0.6f);
      p.line(0.27f, 0.27f, 0.82f, 0.82f, 1.3f);
      sine(p, -0.5f, 0.18f, -0.16f, 0.2f, 1.0f, 0.85f);
      break;
    case Icon::kHistory:
      p.arc(0, 0, 0.8f, -2.3f, 2.4f * kPi / 2.2f);
      p.poly({-0.95f, -0.62f, -0.62f, -0.5f, -0.58f, -0.86f});
      p.poly({0, -0.42f, 0, 0, 0.32f, 0.22f});
      break;
    case Icon::kChip:
      p.rrect(-0.56f, -0.56f, 0.56f, 0.56f, 0.12f);
      p.fillRect(-0.24f, -0.24f, 0.24f, 0.24f, 0.05f);
      for (float t : {-0.3f, 0.f, 0.3f}) {
        p.line(t, -0.56f, t, -0.86f, 0.8f);
        p.line(t, 0.56f, t, 0.86f, 0.8f);
        p.line(-0.56f, t, -0.86f, t, 0.8f);
        p.line(0.56f, t, 0.86f, t, 0.8f);
      }
      break;
    case Icon::kGear: {
      p.ring(0, 0, 0.55f, 1.6f);
      p.ring(0, 0, 0.2f);
      for (int i = 0; i < 8; ++i) {
        const float a = i * kPi / 4;
        p.line(0.62f * std::sin(a), -0.62f * std::cos(a), 0.86f * std::sin(a), -0.86f * std::cos(a), 2.3f);
      }
      break;
    }
    case Icon::kWifi:
      p.arc(0, 0.62f, 1.0f, -0.78f, 0.78f);
      p.arc(0, 0.62f, 0.66f, -0.78f, 0.78f);
      p.arc(0, 0.62f, 0.32f, -0.78f, 0.78f);
      p.dot(0, 0.62f, 0.1f);
      break;
    case Icon::kBolt:
      p.fill({0.18f, -0.95f, -0.52f, 0.12f, -0.04f, 0.12f, -0.2f, 0.95f, 0.52f, -0.16f, 0.04f, -0.16f});
      break;
    case Icon::kTag:
      p.rrect(-0.86f, -0.62f, 0.86f, 0.62f, 0.16f);
      p.arc(-0.42f, 0, 0.22f, kPi / 2 - 0.75f, kPi / 2 + 0.75f, 0.8f);
      p.arc(-0.42f, 0, 0.45f, kPi / 2 - 0.75f, kPi / 2 + 0.75f, 0.8f);
      p.arc(-0.42f, 0, 0.68f, kPi / 2 - 0.62f, kPi / 2 + 0.62f, 0.8f);
      break;
    case Icon::kBell:
    case Icon::kBellOff:
      p.arc(0, -0.14f, 0.5f, -kPi / 2, kPi / 2);
      p.line(-0.5f, -0.14f, -0.6f, 0.46f);
      p.line(0.5f, -0.14f, 0.6f, 0.46f);
      p.line(-0.8f, 0.48f, 0.8f, 0.48f);
      p.dot(0, 0.72f, 0.14f);
      p.line(0, -0.64f, 0, -0.8f);
      if (icon == Icon::kBellOff) p.line(-0.86f, -0.86f, 0.86f, 0.86f, 1.1f);
      break;
    case Icon::kBulb:
      p.arc(0, -0.2f, 0.52f, -2.45f, 2.45f);
      p.line(-0.3f, 0.22f, -0.26f, 0.48f);
      p.line(0.3f, 0.22f, 0.26f, 0.48f);
      p.line(-0.28f, 0.5f, 0.28f, 0.5f);
      p.line(-0.2f, 0.72f, 0.2f, 0.72f);
      p.line(0, -0.18f, 0, 0.2f, 0.7f);
      break;
    case Icon::kPrinter:
      p.rrect(-0.46f, -0.86f, 0.46f, -0.32f, 0.04f);
      p.rrect(-0.88f, -0.36f, 0.88f, 0.44f, 0.14f);
      p.rrect(-0.46f, 0.14f, 0.46f, 0.86f, 0.04f);
      p.line(-0.26f, 0.42f, 0.26f, 0.42f, 0.7f);
      p.line(-0.26f, 0.62f, 0.12f, 0.62f, 0.7f);
      p.dot(0.6f, -0.1f, 0.08f);
      break;
    case Icon::kPlay:
      p.fill({-0.42f, -0.68f, 0.68f, 0.f, -0.42f, 0.68f});
      break;
    case Icon::kStop:
      p.fillRect(-0.52f, -0.52f, 0.52f, 0.52f, 0.12f);
      break;
    case Icon::kPower:
      p.arc(0, 0.06f, 0.68f, 0.62f, 2 * kPi - 0.62f);
      p.line(0, -0.86f, 0, -0.12f);
      break;
    case Icon::kFit:
      p.poly({-0.85f, -0.4f, -0.85f, -0.85f, -0.4f, -0.85f});
      p.poly({0.4f, -0.85f, 0.85f, -0.85f, 0.85f, -0.4f});
      p.poly({0.85f, 0.4f, 0.85f, 0.85f, 0.4f, 0.85f});
      p.poly({-0.4f, 0.85f, -0.85f, 0.85f, -0.85f, 0.4f});
      p.ring(0, 0, 0.22f);
      break;
    case Icon::kPlus:
      p.line(-0.62f, 0, 0.62f, 0, 1.2f);
      p.line(0, -0.62f, 0, 0.62f, 1.2f);
      break;
    case Icon::kMinus:
      p.line(-0.62f, 0, 0.62f, 0, 1.2f);
      break;
    case Icon::kCheck:
      p.poly({-0.66f, 0.02f, -0.18f, 0.5f, 0.7f, -0.46f}, 1.3f);
      break;
    case Icon::kWarn:
      p.poly({0, -0.82f, 0.88f, 0.72f, -0.88f, 0.72f, 0, -0.82f});
      p.line(0, -0.26f, 0, 0.2f, 1.1f);
      p.dot(0, 0.46f, 0.09f);
      break;
    case Icon::kCross:
      p.line(-0.6f, -0.6f, 0.6f, 0.6f, 1.2f);
      p.line(0.6f, -0.6f, -0.6f, 0.6f, 1.2f);
      break;
    case Icon::kThermo:
      p.arc(0, -0.58f, 0.2f, -kPi / 2, kPi / 2);
      p.line(-0.2f, -0.58f, -0.2f, 0.3f);
      p.line(0.2f, -0.58f, 0.2f, 0.3f);
      p.ring(0, 0.55f, 0.32f);
      p.dot(0, 0.55f, 0.17f);
      p.line(0, -0.2f, 0, 0.5f, 1.1f);
      break;
    case Icon::kDrop: {
      std::vector<float> pts;
      pts.push_back(cx);
      pts.push_back(cy - 0.9f * p.s);
      for (int i = 0; i <= 16; ++i) {
        const float a = -0.62f + (2 * kPi - 2 * 0.62f) * i / 16.f + kPi / 2;
        pts.push_back(cx + 0.55f * p.s * std::cos(a + kPi));
        pts.push_back(cy + 0.25f * p.s + 0.55f * p.s * std::sin(a + kPi) * -1.f);
      }
      fillPolygon(c, pts.data(), static_cast<int>(pts.size() / 2), color);
      break;
    }
    case Icon::kRuler:
      p.line(-0.86f, 0, 0.86f, 0);
      p.poly({-0.56f, -0.3f, -0.86f, 0, -0.56f, 0.3f});
      p.poly({0.56f, -0.3f, 0.86f, 0, 0.56f, 0.3f});
      p.line(-0.86f, -0.6f, -0.86f, 0.6f, 0.8f);
      p.line(0.86f, -0.6f, 0.86f, 0.6f, 0.8f);
      break;
    case Icon::kFan:
      p.ring(0, 0, 0.86f, 0.8f);
      p.dot(0, 0, 0.16f);
      for (int i = 0; i < 3; ++i) {
        const float a = i * 2 * kPi / 3;
        p.arc(0.32f * std::sin(a), -0.32f * std::cos(a), 0.32f, a - 1.4f, a + 1.2f, 1.6f);
      }
      break;
    case Icon::kSd:
      p.poly({-0.56f, -0.86f, 0.26f, -0.86f, 0.62f, -0.5f, 0.62f, 0.86f, -0.56f, 0.86f, -0.56f, -0.86f});
      p.line(-0.28f, -0.6f, -0.28f, -0.3f, 0.8f);
      p.line(0.0f, -0.6f, 0.0f, -0.3f, 0.8f);
      p.line(0.28f, -0.5f, 0.28f, -0.3f, 0.8f);
      break;
    case Icon::kBoard:
      p.rrect(-0.5f, -0.88f, 0.5f, 0.88f, 0.12f);
      for (float t : {-0.56f, -0.2f, 0.16f, 0.52f}) {
        p.line(-0.5f, t, -0.82f, t, 0.8f);
        p.line(0.5f, t, 0.82f, t, 0.8f);
      }
      p.rrect(-0.18f, -0.78f, 0.18f, -0.56f, 0.04f);
      break;
    case Icon::kClock:
      p.ring(0, 0, 0.82f);
      p.poly({0, -0.46f, 0, 0, 0.34f, 0.22f});
      break;
    case Icon::kLeft:
      p.poly({0.3f, -0.62f, -0.32f, 0, 0.3f, 0.62f}, 1.3f);
      break;
    case Icon::kRight:
      p.poly({-0.3f, -0.62f, 0.32f, 0, -0.3f, 0.62f}, 1.3f);
      break;
    case Icon::kUp:
      p.poly({-0.62f, 0.3f, 0, -0.32f, 0.62f, 0.3f}, 1.3f);
      break;
    case Icon::kDown:
      p.poly({-0.62f, -0.3f, 0, 0.32f, 0.62f, -0.3f}, 1.3f);
      break;
    case Icon::kBackspace:
      p.poly({-0.92f, 0, -0.44f, -0.6f, 0.86f, -0.6f, 0.86f, 0.6f, -0.44f, 0.6f, -0.92f, 0});
      p.line(-0.06f, -0.26f, 0.46f, 0.26f);
      p.line(0.46f, -0.26f, -0.06f, 0.26f);
      break;
    case Icon::kShift:
      p.poly({0, -0.86f, 0.78f, 0.04f, 0.34f, 0.04f, 0.34f, 0.74f, -0.34f, 0.74f, -0.34f, 0.04f, -0.78f, 0.04f,
              0, -0.86f});
      break;
    case Icon::kAxes:
      p.line(-0.5f, 0.5f, 0.82f, 0.5f);
      p.line(-0.5f, 0.5f, -0.5f, -0.82f);
      p.line(-0.5f, 0.5f, 0.22f, -0.22f);
      p.poly({0.6f, 0.32f, 0.82f, 0.5f, 0.6f, 0.68f});
      p.poly({-0.68f, -0.6f, -0.5f, -0.82f, -0.32f, -0.6f});
      p.dot(0.22f, -0.22f, 0.1f);
      break;
    case Icon::kGauge:
      p.arc(0, 0.22f, 0.82f, -2.2f, 2.2f);
      p.line(0, 0.22f, 0.44f, -0.3f, 1.2f);
      p.dot(0, 0.22f, 0.13f);
      break;
    case Icon::kSpeaker:
      p.fill({-0.86f, -0.28f, -0.4f, -0.28f, 0.1f, -0.74f, 0.1f, 0.74f, -0.4f, 0.28f, -0.86f, 0.28f});
      p.arc(0.1f, 0, 0.38f, kPi / 2 - 0.8f, kPi / 2 + 0.8f, 0.8f);
      p.arc(0.1f, 0, 0.7f, kPi / 2 - 0.8f, kPi / 2 + 0.8f, 0.8f);
      break;
    case Icon::kHeat:
      for (int j = 0; j < 3; ++j) {
        for (int i = 0; i < 3; ++i) {
          const float x0 = -0.84f + i * 0.58f, y0 = -0.84f + j * 0.58f;
          const int a = (i == 1 && j == 1) ? 255 : ((i + j) % 2 ? 170 : 90);
          const int ix = static_cast<int>(p.X(x0)), iy = static_cast<int>(p.Y(y0));
          fillRoundRect(c, ix, iy, static_cast<int>(0.5f * p.s), static_cast<int>(0.5f * p.s),
                        static_cast<int>(0.1f * p.s), color, a);
        }
      }
      break;
    case Icon::kLayers:
      p.poly({0, -0.8f, 0.86f, -0.36f, 0, 0.08f, -0.86f, -0.36f, 0, -0.8f});
      p.poly({-0.86f, 0.06f, 0, 0.5f, 0.86f, 0.06f});
      p.poly({-0.86f, 0.42f, 0, 0.86f, 0.86f, 0.42f});
      break;
    case Icon::kSpectrum:
      p.line(-0.86f, 0.82f, 0.86f, 0.82f);
      for (int i = 0; i < 6; ++i) {
        const float x = -0.66f + i * 0.27f;
        const float h = (i == 2) ? 1.4f : 0.3f + 0.12f * ((i * 7) % 5);
        p.line(x, 0.66f, x, 0.66f - h, 1.3f);
      }
      break;
    case Icon::kLaser:
      p.fillRect(-0.92f, -0.34f, -0.36f, 0.34f, 0.08f);
      p.line(-0.28f, 0, 0.86f, -0.62f, 0.8f);
      p.line(-0.28f, 0, 0.9f, 0, 0.8f);
      p.line(-0.28f, 0, 0.86f, 0.62f, 0.8f);
      p.dot(0.5f, -0.42f, 0.09f);
      p.dot(0.66f, 0.18f, 0.09f);
      p.dot(0.32f, 0.36f, 0.09f);
      break;
    case Icon::kMic:
      p.rrect(-0.28f, -0.92f, 0.28f, 0.24f, 0.28f);
      p.arc(0, -0.12f, 0.56f, kPi / 2, 3 * kPi / 2);
      p.line(0, 0.44f, 0, 0.8f);
      p.line(-0.34f, 0.82f, 0.34f, 0.82f);
      break;
    case Icon::kTimer:
      p.ring(0, 0.12f, 0.72f);
      p.line(0, 0.12f, 0, -0.3f, 1.1f);
      p.line(-0.2f, -0.86f, 0.2f, -0.86f, 1.1f);
      p.line(0, -0.86f, 0, -0.6f);
      break;
  }
}

}  // namespace vm
