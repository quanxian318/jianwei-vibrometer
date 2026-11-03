#pragma once

#include <cstdint>

namespace vm {

// Register map of the PL PDM microphone receiver (ax7010/pl/audio/rtl/pdm_mic.v,
// checked against the RTL by tests/test_acoustic.cpp).
namespace pdm_reg {
constexpr uint32_t kId = 0x000;
constexpr uint32_t kVersion = 0x004;
constexpr uint32_t kClkHz = 0x008;
constexpr uint32_t kCtrl = 0x00C;      // EN, PHASE, CLEAR (pulse)
constexpr uint32_t kHalfDiv = 0x010;   // system clocks per PDM clock half period (>= 4)
constexpr uint32_t kSampleAt = 0x014;  // clocks after the edge opening the valid half (< HALF_DIV)
constexpr uint32_t kStatus = 0x018;    // flags below; writing OVERFLOW clears it and DROPPED
constexpr uint32_t kLevel = 0x01C;     // words in the FIFO
constexpr uint32_t kData = 0x020;      // pops a word: 32 PDM bits, bit 0 = oldest
constexpr uint32_t kWords = 0x024;     // words completed since enable; latches the four below
constexpr uint32_t kLevelL = 0x028;
constexpr uint32_t kBitsL = 0x02C;     // bits of the word being filled
constexpr uint32_t kTsL = 0x030;       // clock counter
constexpr uint32_t kTs = 0x034;        // free-running clock counter
constexpr uint32_t kDropped = 0x038;   // words dropped since OVERFLOW was last cleared
constexpr uint32_t kDroppedL = 0x03C;
constexpr uint32_t kConfig = 0x040;    // {FIFO_LOG2, bits per word}

constexpr uint32_t kIdValue = 0x50444D31;  // "PDM1"
constexpr uint32_t kVersionValue = 0x00020000;

constexpr uint32_t kCtrlEn = 1u << 0;
constexpr uint32_t kCtrlPhaseHigh = 1u << 1;  // data valid while mic_clk is high (L/R = VDD)
constexpr uint32_t kCtrlClear = 1u << 8;

constexpr uint32_t kStRunning = 1u << 0;
constexpr uint32_t kStOverflow = 1u << 1;  // FIFO frozen: words are dropped until cleared
constexpr uint32_t kStKilled = 1u << 2;
}  // namespace pdm_reg

}  // namespace vm
