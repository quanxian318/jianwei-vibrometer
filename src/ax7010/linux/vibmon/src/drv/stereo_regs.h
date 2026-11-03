#pragma once

#include <cstdint>

// Register map of the PL stereo matcher wrapper ax7010/pl/stereo/rtl/stereo_axi.v
// (a unit test compares these values with the localparams in that file) and
// the AXI DMA simple-mode registers it is paired with (add_stereo.tcl).
namespace vm {
namespace stereo_reg {
constexpr uint32_t kId = 0x000;       // 0x53544552 "STER"
constexpr uint32_t kVersion = 0x004;  // 0x00010000
constexpr uint32_t kConfig = 0x008;   // [7:0] D, [15:8] P1, [23:16] P2, [27:24] UNIQ, [31:28] LR tolerance
constexpr uint32_t kMaxW = 0x00C;
constexpr uint32_t kWidth = 0x010;
constexpr uint32_t kHeight = 0x014;
constexpr uint32_t kCtrl = 0x018;     // bit0 START, bit1 SOFT_RESET
constexpr uint32_t kStatus = 0x01C;   // bit0 BUSY, bit1 DONE (w1c), bit2 OVERFLOW (w1c), bit3 BAD_SIZE (w1c)
constexpr uint32_t kIrqEn = 0x020;
constexpr uint32_t kCycles = 0x024;
constexpr uint32_t kFrames = 0x028;

constexpr uint32_t kIdValue = 0x53544552;
constexpr uint32_t kVersionValue = 0x00010000;
constexpr uint32_t kCtrlStart = 1u << 0, kCtrlSoftReset = 1u << 1;
constexpr uint32_t kStBusy = 1u << 0, kStDone = 1u << 1, kStOverflow = 1u << 2, kStBadSize = 1u << 3;
}  // namespace stereo_reg

namespace axidma {
constexpr uint32_t kMm2sCr = 0x00, kMm2sSr = 0x04, kMm2sSa = 0x18, kMm2sLength = 0x28;
constexpr uint32_t kS2mmCr = 0x30, kS2mmSr = 0x34, kS2mmDa = 0x48, kS2mmLength = 0x58;
constexpr uint32_t kCrRun = 1u << 0, kCrReset = 1u << 2;
constexpr uint32_t kSrHalted = 1u << 0, kSrIdle = 1u << 1, kSrErrors = 0x70;  // internal, slave, decode errors
}  // namespace axidma
}  // namespace vm
