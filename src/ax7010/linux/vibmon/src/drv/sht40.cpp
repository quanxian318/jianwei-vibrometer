#include "drv/sht40.h"

#include "util/clock.h"

namespace vm {

uint8_t Sht40::crc8(const uint8_t* data, size_t n) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < n; ++i) {
    crc ^= data[i];
    for (int b = 0; b < 8; ++b) crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31) : static_cast<uint8_t>(crc << 1);
  }
  return crc;
}

bool Sht40::softReset() {
  const uint8_t cmd = 0x94;
  if (!bus_.writeBytes(addr_, &cmd, 1)) return false;
  sleepMs(2);
  return true;
}

bool Sht40::readWords(uint8_t cmd, int waitMs, uint16_t& a, uint16_t& b) {
  if (!bus_.writeBytes(addr_, &cmd, 1)) return false;
  sleepMs(waitMs);
  uint8_t r[6];
  if (!bus_.readBytes(addr_, r, 6)) return false;
  if (crc8(r, 2) != r[2] || crc8(r + 3, 2) != r[5]) return false;
  a = static_cast<uint16_t>((r[0] << 8) | r[1]);
  b = static_cast<uint16_t>((r[3] << 8) | r[4]);
  return true;
}

bool Sht40::readSerial(uint32_t& serial) {
  uint16_t hi = 0, lo = 0;
  if (!readWords(0x89, 2, hi, lo)) return false;
  serial = (static_cast<uint32_t>(hi) << 16) | lo;
  return true;
}

bool Sht40::measure(double& tempC, double& rhPercent) {
  uint16_t t = 0, rh = 0;
  if (!readWords(0xFD, 10, t, rh)) return false;  // high repeatability: max 8.3 ms
  tempC = tempFromRaw(t);
  rhPercent = rhFromRaw(rh);
  return true;
}

}  // namespace vm
