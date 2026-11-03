#include "drv/vl53l1x.h"

#ifdef HAVE_VL53L1X_ULD
#include "VL53L1X_api.h"
#include "vl53l1x_uld_port/vl53l1_platform_bus.h"
#endif

namespace vm {

bool Vl53l1x::readModelId(uint16_t& id) {
  const uint8_t index[2] = {0x01, 0x0F};  // 16-bit register index, big endian
  uint8_t b[2];
  if (!bus_.transfer(addr_, index, 2, b, 2)) return false;
  id = static_cast<uint16_t>((b[0] << 8) | b[1]);
  return true;
}

#ifdef HAVE_VL53L1X_ULD

bool Vl53l1x::driverAvailable() { return true; }

bool Vl53l1x::start(int timingBudgetMs, int periodMs, bool longRange) {
  vl53l1PlatformAttach(addr_, &bus_);
  uint8_t booted = 0;
  for (int i = 0; i < 100 && !booted; ++i) {
    if (VL53L1X_BootState(addr_, &booted) != 0) booted = 0;
    if (!booted) VL53L1_WaitMs(addr_, 2);
  }
  if (!booted) return false;
  if (VL53L1X_SensorInit(addr_) != 0) return false;
  if (VL53L1X_SetDistanceMode(addr_, longRange ? 2 : 1) != 0) return false;
  if (VL53L1X_SetTimingBudgetInMs(addr_, static_cast<uint16_t>(timingBudgetMs)) != 0) return false;
  if (VL53L1X_SetInterMeasurementInMs(addr_, static_cast<uint32_t>(periodMs)) != 0) return false;
  if (VL53L1X_StartRanging(addr_) != 0) return false;
  running_ = true;
  return true;
}

bool Vl53l1x::stop() {
  if (!running_) return true;
  running_ = false;
  return VL53L1X_StopRanging(addr_) == 0;
}

bool Vl53l1x::poll(TofReading& r, bool& ready) {
  ready = false;
  if (!running_) return false;
  uint8_t dr = 0;
  if (VL53L1X_CheckForDataReady(addr_, &dr) != 0) return false;
  if (!dr) return true;
  uint8_t status = 0;
  uint16_t mm = 0, signal = 0, ambient = 0;
  if (VL53L1X_GetRangeStatus(addr_, &status) != 0 || VL53L1X_GetDistance(addr_, &mm) != 0 ||
      VL53L1X_GetSignalRate(addr_, &signal) != 0 || VL53L1X_GetAmbientRate(addr_, &ambient) != 0) {
    return false;
  }
  VL53L1X_ClearInterrupt(addr_);
  r.rangeStatus = status;
  r.distanceMm = mm;
  r.signalKcps = signal;
  r.ambientKcps = ambient;
  r.valid = status == 0;
  ready = true;
  return true;
}

#else

bool Vl53l1x::driverAvailable() { return false; }
bool Vl53l1x::start(int, int, bool) { return false; }
bool Vl53l1x::stop() { return true; }
bool Vl53l1x::poll(TofReading&, bool& ready) {
  ready = false;
  return false;
}

#endif

}  // namespace vm
