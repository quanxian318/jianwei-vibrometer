#pragma once

#include <cstdint>

namespace vm {
namespace theme {

// 1024x600 layout: status bar on top, navigation rail on the left, pages in
// the content area.
constexpr int kW = 1024, kH = 600;
constexpr int kBarH = 48;
constexpr int kRailW = 92;
constexpr int kGap = 8;
constexpr int kCx = kRailW + kGap;   // content area
constexpr int kCy = kBarH + kGap;
constexpr int kCw = kW - kCx - kGap;
constexpr int kCh = kH - kCy - kGap;
constexpr int kRadius = 12;

// Dark palette.
constexpr uint32_t kBg = 0x0A0F15;
constexpr uint32_t kBar = 0x101822;
constexpr uint32_t kRail = 0x0D141C;
constexpr uint32_t kBgTop = 0x0E1621;  // page background, vertical gradient to kBg
constexpr uint32_t kBarTop = 0x172230;  // status bar gradient to kBar
constexpr uint32_t kCard = 0x151E29;
constexpr uint32_t kCardEdge = 0x31435A;  // top edge of cards, light from above
constexpr uint32_t kCardHi = 0x1B2735;
constexpr uint32_t kBorder = 0x243142;
constexpr uint32_t kText = 0xE8EEF4;
constexpr uint32_t kText2 = 0x98A6B5;
constexpr uint32_t kText3 = 0x5E6B79;
constexpr uint32_t kAccent = 0x3AA0FF;
constexpr uint32_t kAccentDim = 0x1D4F80;
constexpr uint32_t kGreen = 0x25C26E;
constexpr uint32_t kAmber = 0xF5A524;
constexpr uint32_t kRed = 0xEF4D4D;
constexpr uint32_t kViolet = 0x9B7BFF;
constexpr uint32_t kNeutral = 0x26364A;
constexpr uint32_t kDisabled = 0x1E2833;
constexpr uint32_t kGrid = 0x223040;

}  // namespace theme
}  // namespace vm
