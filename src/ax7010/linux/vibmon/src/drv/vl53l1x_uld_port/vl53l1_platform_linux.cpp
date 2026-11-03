// Linux implementation of the VL53L1X ULD platform functions.
// Compiled only when ST's ULD sources are present (HAVE_VL53L1X_ULD).
#ifdef HAVE_VL53L1X_ULD

#include <cstring>
#include <map>
#include <mutex>

#include "hw/i2c_bus.h"
#include "util/clock.h"
#include "vl53l1_platform.h"
#include "vl53l1_platform_bus.h"

namespace {
std::mutex g_mutex;
std::map<uint16_t, vm::I2cBus*> g_buses;

vm::I2cBus* busFor(uint16_t dev) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_buses.find(dev);
  return it == g_buses.end() ? nullptr : it->second;
}

int8_t writeIndexed(uint16_t dev, uint16_t index, const uint8_t* data, uint32_t count) {
  vm::I2cBus* bus = busFor(dev);
  if (!bus || count > 256) return -1;
  uint8_t buf[258];
  buf[0] = static_cast<uint8_t>(index >> 8);
  buf[1] = static_cast<uint8_t>(index & 0xFF);
  memcpy(buf + 2, data, count);
  return bus->writeBytes(static_cast<uint8_t>(dev), buf, count + 2) ? 0 : -1;
}

int8_t readIndexed(uint16_t dev, uint16_t index, uint8_t* data, uint32_t count) {
  vm::I2cBus* bus = busFor(dev);
  if (!bus) return -1;
  const uint8_t idx[2] = {static_cast<uint8_t>(index >> 8), static_cast<uint8_t>(index & 0xFF)};
  return bus->transfer(static_cast<uint8_t>(dev), idx, 2, data, count) ? 0 : -1;
}
}  // namespace

void vl53l1PlatformAttach(uint16_t dev, vm::I2cBus* bus) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_buses[dev] = bus;
}

extern "C" {

int8_t VL53L1_WriteMulti(uint16_t dev, uint16_t index, uint8_t* pdata, uint32_t count) {
  return writeIndexed(dev, index, pdata, count);
}

int8_t VL53L1_ReadMulti(uint16_t dev, uint16_t index, uint8_t* pdata, uint32_t count) {
  return readIndexed(dev, index, pdata, count);
}

int8_t VL53L1_WrByte(uint16_t dev, uint16_t index, uint8_t data) { return writeIndexed(dev, index, &data, 1); }

int8_t VL53L1_WrWord(uint16_t dev, uint16_t index, uint16_t data) {
  const uint8_t b[2] = {static_cast<uint8_t>(data >> 8), static_cast<uint8_t>(data & 0xFF)};
  return writeIndexed(dev, index, b, 2);
}

int8_t VL53L1_WrDWord(uint16_t dev, uint16_t index, uint32_t data) {
  const uint8_t b[4] = {static_cast<uint8_t>(data >> 24), static_cast<uint8_t>(data >> 16),
                        static_cast<uint8_t>(data >> 8), static_cast<uint8_t>(data & 0xFF)};
  return writeIndexed(dev, index, b, 4);
}

int8_t VL53L1_RdByte(uint16_t dev, uint16_t index, uint8_t* pdata) { return readIndexed(dev, index, pdata, 1); }

int8_t VL53L1_RdWord(uint16_t dev, uint16_t index, uint16_t* pdata) {
  uint8_t b[2];
  if (readIndexed(dev, index, b, 2) != 0) return -1;
  *pdata = static_cast<uint16_t>((b[0] << 8) | b[1]);
  return 0;
}

int8_t VL53L1_RdDWord(uint16_t dev, uint16_t index, uint32_t* pdata) {
  uint8_t b[4];
  if (readIndexed(dev, index, b, 4) != 0) return -1;
  *pdata = (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) |
           (static_cast<uint32_t>(b[2]) << 8) | b[3];
  return 0;
}

int8_t VL53L1_WaitMs(uint16_t, int32_t wait_ms) {
  vm::sleepMs(wait_ms);
  return 0;
}

}  // extern "C"

#endif  // HAVE_VL53L1X_ULD
