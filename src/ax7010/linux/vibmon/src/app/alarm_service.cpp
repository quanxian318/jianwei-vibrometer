#include "app/alarm_service.h"
#include "util/log.h"

namespace vm {

void AlarmService::setLevel(AlarmLevel level, int64_t nowMs) {
  // Called once per finished measurement: every new result sounds again.
  muted_ = false;
  levelSinceMs_ = nowMs;
  level_ = level;
}

void AlarmService::tick(int64_t nowMs) {
  if (!board_.tcaReady()) { outputsOk_ = false; return; }
  bool r = false, y = false, g = false, buzz = false;
  if (!suspended_) {
    switch (level_) {
      case AlarmLevel::kUnknown: break;
      case AlarmLevel::kNormal: g = true; break;
      case AlarmLevel::kWarn:
        y = true;
        buzz = cfg_.buzzer && !muted_ && nowMs - levelSinceMs_ < 300;  // one short beep
        break;
      case AlarmLevel::kAlarm:
        r = true;
        buzz = cfg_.buzzer && !muted_ && ((nowMs - levelSinceMs_) % 1000) < 500;
        break;
    }
  }
  const int out = (r ? 1 : 0) | (y ? 2 : 0) | (g ? 4 : 0) | (buzz ? 8 : 0);
  if (outputsOk_ && out == lastOut_ && nowMs - lastRefreshMs_ < 1000) return;
  const bool wasOk = outputsOk_;
  outputsOk_ = board_.setAlarmOutputs(r, y, g, buzz);
  lastRefreshMs_ = nowMs;
  if (outputsOk_) lastOut_ = out;
  if (wasOk && !outputsOk_) LOGW("alarm", "tower/buzzer write or register readback failed; retrying");
  if (!wasOk && outputsOk_) LOGI("alarm", "tower/buzzer outputs confirmed: 0x%x", out);
}

}  // namespace vm
