#include "util/clock.h"

#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>

namespace vm {

int64_t monoNs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

int64_t realtimeMs() {
  timespec ts{};
  clock_gettime(CLOCK_REALTIME, &ts);
  return static_cast<int64_t>(ts.tv_sec) * 1000LL + ts.tv_nsec / 1000000;
}

bool setRealtimeMs(int64_t unixMs) {
  timespec ts{};
  ts.tv_sec = static_cast<time_t>(unixMs / 1000);
  ts.tv_nsec = static_cast<long>((unixMs % 1000) * 1000000);
  return clock_settime(CLOCK_REALTIME, &ts) == 0;
}

void sleepMs(int ms) {
  timespec ts{};
  ts.tv_sec = ms / 1000;
  ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
  while (nanosleep(&ts, &ts) != 0) {
  }
}

void setThreadNice(int nice) {
  setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), nice);
}

// Algorithms from H. Hinnant, "chrono-Compatible Low-Level Date Algorithms".
namespace {
int64_t daysFromCivil(int64_t y, unsigned m, unsigned d) {
  y -= m <= 2;
  const int64_t era = (y >= 0 ? y : y - 399) / 400;
  const unsigned yoe = static_cast<unsigned>(y - era * 400);
  const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int64_t& y, unsigned& m, unsigned& d) {
  z += 719468;
  const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
  const unsigned doe = static_cast<unsigned>(z - era * 146097);
  const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  y = static_cast<int64_t>(yoe) + era * 400;
  const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const unsigned mp = (5 * doy + 2) / 153;
  d = doy - (153 * mp + 2) / 5 + 1;
  m = mp < 10 ? mp + 3 : mp - 9;
  y += m <= 2;
}
}  // namespace

CivilTime civilFromUnix(int64_t unixSec) {
  int64_t days = unixSec >= 0 ? unixSec / 86400 : -((-unixSec + 86399) / 86400);
  int64_t rem = unixSec - days * 86400;
  CivilTime c;
  int64_t y;
  unsigned m, d;
  civilFromDays(days, y, m, d);
  c.year = static_cast<int>(y);
  c.month = static_cast<int>(m);
  c.day = static_cast<int>(d);
  c.hour = static_cast<int>(rem / 3600);
  c.minute = static_cast<int>((rem % 3600) / 60);
  c.second = static_cast<int>(rem % 60);
  c.weekday = static_cast<int>(((days % 7) + 11) % 7);  // 1970-01-01 was a Thursday
  return c;
}

int64_t unixFromCivil(const CivilTime& c) {
  return daysFromCivil(c.year, static_cast<unsigned>(c.month), static_cast<unsigned>(c.day)) * 86400 +
         c.hour * 3600 + c.minute * 60 + c.second;
}

std::string formatUtcIso(int64_t unixMs) {
  int64_t sec = unixMs >= 0 ? unixMs / 1000 : -((-unixMs + 999) / 1000);
  int ms = static_cast<int>(unixMs - sec * 1000);
  CivilTime c = civilFromUnix(sec);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", c.year, c.month, c.day, c.hour, c.minute,
           c.second, ms);
  return buf;
}

std::string formatLocal(int64_t unixMs, int tzOffsetMin, const char* fmt) {
  int64_t local = unixMs + static_cast<int64_t>(tzOffsetMin) * 60000;
  int64_t sec = local >= 0 ? local / 1000 : -((-local + 999) / 1000);
  CivilTime c = civilFromUnix(sec);
  std::string out;
  char buf[16];
  for (const char* p = fmt; *p; ++p) {
    if (*p != '%' || p[1] == 0) {
      out += *p;
      continue;
    }
    ++p;
    switch (*p) {
      case 'Y': snprintf(buf, sizeof(buf), "%04d", c.year); break;
      case 'm': snprintf(buf, sizeof(buf), "%02d", c.month); break;
      case 'd': snprintf(buf, sizeof(buf), "%02d", c.day); break;
      case 'H': snprintf(buf, sizeof(buf), "%02d", c.hour); break;
      case 'M': snprintf(buf, sizeof(buf), "%02d", c.minute); break;
      case 'S': snprintf(buf, sizeof(buf), "%02d", c.second); break;
      default: buf[0] = '%'; buf[1] = *p; buf[2] = 0; break;
    }
    out += buf;
  }
  return out;
}

}  // namespace vm
