#pragma once

#include <cstdint>
#include <string>

namespace vm {

int64_t monoNs();
inline int64_t monoMs() { return monoNs() / 1000000; }
int64_t realtimeMs();  // UTC
bool setRealtimeMs(int64_t unixMs);
void sleepMs(int ms);
// Lowers (positive nice) the calling thread's CPU priority, e.g. for display-only work.
void setThreadNice(int nice);

struct CivilTime {
  int year = 1970, month = 1, day = 1;
  int hour = 0, minute = 0, second = 0;
  int weekday = 4;  // 0 = Sunday
};

// Proleptic Gregorian conversions on 64-bit values (no time_t / 2038 limits).
CivilTime civilFromUnix(int64_t unixSec);
int64_t unixFromCivil(const CivilTime& c);

// "2026-10-21T08:15:30.123Z"
std::string formatUtcIso(int64_t unixMs);
// strftime-like subset: %Y %m %d %H %M %S, applied after adding tzOffsetMin.
std::string formatLocal(int64_t unixMs, int tzOffsetMin, const char* fmt);

}  // namespace vm
