#include "drv/ds3231.h"

#include "util/clock.h"

namespace vm {

namespace {
constexpr uint8_t kRegSeconds = 0x00, kRegControl = 0x0E, kRegStatus = 0x0F, kRegTempMsb = 0x11;
constexpr uint8_t kStatusOsf = 0x80;
}  // namespace

bool Ds3231::configure1Hz() {
  if (!bus_.writeReg8(addr_, kRegControl, 0x00)) return false;
  uint8_t status = 0;
  if (!bus_.readReg8(addr_, kRegStatus, status)) return false;
  // Keep OSF as is (it marks an invalid time); clear EN32kHz and alarm flags.
  return bus_.writeReg8(addr_, kRegStatus, static_cast<uint8_t>(status & kStatusOsf));
}

bool Ds3231::readTime(int64_t& unixSec, bool& valid) {
  uint8_t r[7];
  uint8_t status = 0;
  if (!bus_.readRegs(addr_, kRegSeconds, r, 7) || !bus_.readReg8(addr_, kRegStatus, status)) return false;
  CivilTime c;
  c.second = fromBcd(r[0] & 0x7F);
  c.minute = fromBcd(r[1] & 0x7F);
  if (r[2] & 0x40) {  // 12-hour mode written by another tool
    int h = fromBcd(r[2] & 0x1F) % 12;
    c.hour = (r[2] & 0x20) ? h + 12 : h;
  } else {
    c.hour = fromBcd(r[2] & 0x3F);
  }
  c.day = fromBcd(r[4] & 0x3F);
  c.month = fromBcd(r[5] & 0x1F);
  c.year = 2000 + fromBcd(r[6]) + ((r[5] & 0x80) ? 100 : 0);
  valid = (status & kStatusOsf) == 0 && c.second < 60 && c.minute < 60 && c.hour < 24 && c.day >= 1 &&
          c.day <= 31 && c.month >= 1 && c.month <= 12;
  unixSec = unixFromCivil(c);
  return true;
}

bool Ds3231::writeTime(int64_t unixSec) {
  CivilTime c = civilFromUnix(unixSec);
  if (c.year < 2000 || c.year > 2199) return false;
  const int yy = (c.year - 2000) % 100;
  uint8_t b[8];
  b[0] = kRegSeconds;
  b[1] = toBcd(c.second);
  b[2] = toBcd(c.minute);
  b[3] = toBcd(c.hour);  // bit 6 = 0: 24-hour mode
  b[4] = static_cast<uint8_t>(c.weekday + 1);
  b[5] = toBcd(c.day);
  b[6] = static_cast<uint8_t>(toBcd(c.month) | (c.year >= 2100 ? 0x80 : 0x00));
  b[7] = toBcd(yy);
  if (!bus_.writeBytes(addr_, b, sizeof(b))) return false;
  return bus_.writeReg8(addr_, kRegStatus, 0x00);  // clear OSF, 32 kHz output off
}

bool Ds3231::readTemperature(double& celsius) {
  uint8_t t[2];
  if (!bus_.readRegs(addr_, kRegTempMsb, t, 2)) return false;
  celsius = static_cast<int8_t>(t[0]) + (t[1] >> 6) * 0.25;
  return true;
}

}  // namespace vm
