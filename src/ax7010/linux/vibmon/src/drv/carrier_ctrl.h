#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

#include "hw/uio.h"

// Driver for the carrier_ctrl PL core (ax7010/pl/rtl/carrier_ctrl.v).
namespace vm {

namespace creg {
constexpr uint32_t kId = 0x000, kVersion = 0x004, kClkHz = 0x008, kScratch = 0x00C;
constexpr uint32_t kTsLo = 0x010, kTsHi = 0x014;
constexpr uint32_t kInRaw = 0x020, kKeyState = 0x024, kEncCount = 0x028, kEncErrors = 0x02C;
constexpr uint32_t kDebounceMs = 0x030, kLongMs = 0x034, kEncFilterUs = 0x038;
constexpr uint32_t kPwmPeriod = 0x040, kPwmDuty0 = 0x044, kPwmDuty1 = 0x048, kPwmCtrl = 0x04C, kPwmStatus = 0x050;
constexpr uint32_t kWdgTimeoutMs = 0x060, kWdgKick = 0x064, kWdgStatus = 0x068;
constexpr uint32_t kWsCtrl = 0x080, kWsStatus = 0x084, kWsResetUs = 0x088, kWsColor0 = 0x090, kWsFault0 = 0x0B0;
constexpr uint32_t kEvtStatus = 0x100, kEvtInfo = 0x104, kEvtTsLo = 0x108, kEvtTsHi = 0x10C, kEvtPop = 0x110;
constexpr uint32_t kEvtDropped = 0x114, kEvtClear = 0x118, kEvtEnRise = 0x120, kEvtEnFall = 0x124;
constexpr uint32_t kIrqCtrl = 0x128, kIrqStatus = 0x12C;
constexpr uint32_t kIdValue = 0x56424331;  // "VBC1"
constexpr uint32_t kMapSize = 0x1000;
}  // namespace creg

// Event source ids (also bit positions in the edge enable masks).
enum CarrierSource : uint8_t {
  kSrcImuInt1 = 0,
  kSrcImuInt2 = 1,
  kSrcAdxlDrdy = 2,
  kSrcTofInt = 3,
  kSrcRtc1Hz = 4,
  kSrcTach = 5,
  kSrcPfailN = 6,
  kSrcExpIntN = 7,
  kSrcEspIrq = 8,
  kSrcPrnCts = 9,
  kSrcEspCts = 10,
  kSrcKey = 16,
  kSrcEncSw = 17,
  kSrcKeyLong = 18,
  kSrcEncSwLong = 19,
  kSrcEncStep = 20,
};

struct CarrierEvent {
  uint8_t source = 0;
  bool level = false;  // level after the edge; direction (true = up) for kSrcEncStep
  uint16_t seq = 0;
  uint64_t ticks = 0;  // PL timestamp of the edge
};

struct PanelState {
  bool keyPressed = false;
  bool encSwPressed = false;
  int32_t encCount = 0;
  uint32_t encErrors = 0;
  uint32_t inRaw = 0;
};

class CarrierCtrl {
 public:
  explicit CarrierCtrl(RegIo& io) : io_(io) {}

  bool probe(uint32_t* version = nullptr);
  uint32_t clockHz();
  uint64_t timestamp();  // thread-safe 64-bit snapshot

  PanelState panel();
  uint32_t inputs() { return io_.read32(creg::kInRaw); }
  void configurePanel(int debounceMs, int longMs, int encFilterUs);

  void setPwmFrequency(uint32_t hz);
  void setPwmPercent(int channel, double percent);
  void setPwmEnable(bool ch0, bool ch1);
  void setPwmKills(bool pfailKill, bool watchdogKill);
  uint32_t pwmStatus() { return io_.read32(creg::kPwmStatus); }
  void clearPwmKills() { io_.write32(creg::kPwmStatus, 0x30); }

  void setLeds(const uint32_t rgb[5]);
  void setFaultFrame(const uint32_t rgb[5]);
  void sendLeds() { io_.write32(creg::kWsCtrl, 1); }
  uint32_t ledStatus() { return io_.read32(creg::kWsStatus); }
  void setLedResetUs(uint32_t us) { io_.write32(creg::kWsResetUs, us); }

  void setWatchdog(uint32_t timeoutMs) { io_.write32(creg::kWdgTimeoutMs, timeoutMs); }
  void kickWatchdog() { io_.write32(creg::kWdgKick, 1); }
  bool watchdogExpired() { return (io_.read32(creg::kWdgStatus) & 1u) != 0; }

  void configureEvents(uint32_t riseMask, uint32_t fallMask);
  void enableIrq(bool on) { io_.write32(creg::kIrqCtrl, on ? 1u : 0u); }
  void clearEvents() { io_.write32(creg::kEvtClear, 1); }
  // Pops up to max events (thread-safe). Returns the number appended.
  size_t popEvents(std::vector<CarrierEvent>& out, size_t max);
  uint32_t droppedEvents() { return io_.read32(creg::kEvtDropped); }

 private:
  RegIo& io_;
  std::mutex tsMutex_;
  std::mutex evtMutex_;
  uint32_t clkHz_ = 0;
  uint32_t period_ = 0;
  uint32_t ctrl_ = 0x30;  // pfail kill + watchdog kill enabled (hardware reset value)
  bool haveSeq_ = false;
  uint16_t lastSeq_ = 0;
};

}  // namespace vm
