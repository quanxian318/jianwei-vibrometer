#pragma once

#include <cstdint>
#include <cmath>

#include "app/board_service.h"
#include "app/config.h"
#include "app/state.h"

namespace vm {

// External tower light and buzzer (J302 via TCA P1.0..P1.3). Keeps three
// separate facts: the level from the latest result, whether it was muted with
// K101 (or the app, which can also undo it), and the physical outputs. Muting silences the buzzer only; the light
// stays until a later result clears the condition.
class AlarmService {
 public:
  AlarmService(BoardService& board, const AppConfig::Alarm& cfg) : board_(board), cfg_(cfg) {}
  void setLevel(AlarmLevel level, int64_t nowMs);
  void mute(bool on = true) { muted_ = on; }
  void suspend(bool on) { suspended_ = on; }  // power fail / shutdown: everything off
  void tick(int64_t nowMs);
  AlarmLevel level() const { return level_; }
  bool muted() const { return muted_; }
  bool outputsOk() const { return outputsOk_; }
  static AlarmLevel classifyResult(const ResultSummary& r, const AppConfig::Alarm& cfg) {
    if (!r.have || !r.valid || r.unit != "mm" ||
        (r.method == "multipoint" && !r.waveformValid)) return AlarmLevel::kUnknown;
    return classify(r.peakToPeak, cfg.warnPp, cfg.alarmPp, cfg.binaryThreshold);
  }
  static AlarmLevel classify(double peakToPeak, double warn, double alarm, bool binary = false) {
    if (!std::isfinite(peakToPeak) || peakToPeak < 0 || !std::isfinite(warn) ||
        !std::isfinite(alarm) || warn <= 0 || alarm <= 0) return AlarmLevel::kUnknown;
    if (binary) return peakToPeak > alarm ? AlarmLevel::kAlarm : AlarmLevel::kNormal;
    return peakToPeak >= alarm ? AlarmLevel::kAlarm : (peakToPeak >= warn ? AlarmLevel::kWarn : AlarmLevel::kNormal);
  }

 private:
  BoardService& board_;
  const AppConfig::Alarm& cfg_;
  AlarmLevel level_ = AlarmLevel::kUnknown;
  bool muted_ = false;
  bool suspended_ = false;
  int64_t levelSinceMs_ = 0;
  int lastOut_ = -1;
  int64_t lastRefreshMs_ = -1000;
  bool outputsOk_ = false;
};

}  // namespace vm
