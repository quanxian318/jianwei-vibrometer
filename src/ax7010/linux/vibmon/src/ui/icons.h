#pragma once

#include <cstdint>

#include "gfx/canvas.h"

namespace vm {

enum class Icon {
  kNone,
  kLogo,
  kCamera,
  kChart,
  kVision,
  kHistory,
  kChip,
  kGear,
  kWifi,
  kBolt,
  kTag,
  kBell,
  kBellOff,
  kBulb,
  kPrinter,
  kPlay,
  kStop,
  kPower,
  kFit,
  kPlus,
  kMinus,
  kCheck,
  kWarn,
  kCross,
  kThermo,
  kDrop,
  kRuler,
  kFan,
  kSd,
  kBoard,
  kClock,
  kLeft,
  kRight,
  kUp,
  kDown,
  kBackspace,
  kShift,
  kAxes,
  kGauge,
  kSpeaker,
  kHeat,
  kLayers,
  kSpectrum,
  kTimer,
  kLaser,
  kMic,
};

// Vector icon centred at (cx, cy) inside a size x size box.
void drawIcon(Canvas& c, Icon icon, float cx, float cy, float size, uint32_t color);

}  // namespace vm
