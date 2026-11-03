#include "drv/pdm_mic.h"

#include <cstdio>

#include "drv/pdm_regs.h"

namespace vm {

using namespace pdm_reg;

bool PdmMic::probe(std::string& err) {
  if (io_.read32(kId) != kIdValue) {
    err = "PDM core ID mismatch";
    return false;
  }
  if ((io_.read32(kVersion) >> 16) != (kVersionValue >> 16)) {
    err = "PDM core major version mismatch";
    return false;
  }
  if ((io_.read32(kConfig) & 0xFF) != 32) {
    err = "PDM core does not deliver 32-bit words";
    return false;
  }
  return true;
}

uint32_t PdmMic::clockHz() { return io_.read32(kClkHz); }

void PdmMic::start(const Setup& s) {
  io_.write32(kCtrl, 0);
  uint32_t half = s.halfDiv;
  if (!half) half = (clockHz() + 2000000) / 4000000;
  if (half < 4) half = 4;
  io_.write32(kHalfDiv, half);
  io_.write32(kSampleAt, s.sampleAt && s.sampleAt < half ? s.sampleAt : half * 3 / 4);
  io_.write32(kCtrl, kCtrlEn | (s.phaseHigh ? kCtrlPhaseHigh : 0));
}

void PdmMic::stop() { io_.write32(kCtrl, 0); }

void PdmMic::clear() {
  const uint32_t ctrl = io_.read32(kCtrl);
  io_.write32(kCtrl, (ctrl & (kCtrlEn | kCtrlPhaseHigh)) | kCtrlClear);
}

uint32_t PdmMic::halfDiv() { return io_.read32(kHalfDiv); }

PdmMic::Status PdmMic::status() {
  const uint32_t v = io_.read32(kStatus);
  Status s;
  s.running = v & kStRunning;
  s.overflow = v & kStOverflow;
  s.killed = v & kStKilled;
  s.level = io_.read32(kLevel);
  return s;
}

PdmMic::Snapshot PdmMic::read(std::vector<uint32_t>& out) {
  const bool frozen = io_.read32(kStatus) & kStOverflow;  // nothing more enters the FIFO
  Snapshot s;
  s.words = io_.read32(kWords);
  s.level = io_.read32(kLevelL);
  s.bits = io_.read32(kBitsL);
  s.ts = io_.read32(kTsL);
  s.dropped = io_.read32(kDroppedL);
  for (uint32_t i = 0; i < s.level; ++i) out.push_back(io_.read32(kData));
  if (frozen) io_.write32(kStatus, kStOverflow);  // the words before the gap are taken
  return s;
}

uint32_t PdmMic::now() { return io_.read32(kTs); }

}  // namespace vm
