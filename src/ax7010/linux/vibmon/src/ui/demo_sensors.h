#pragma once

#include <unistd.h>

namespace vm {
// Presentation-only override. Never change SystemState, stored readings or alarms.
inline bool uiSensorDemoEnabled() {
  static const bool enabled = access("/root/vibmon_cam/ui_sensor_demo.enabled", F_OK) == 0;
  return enabled;
}
}  // namespace vm
