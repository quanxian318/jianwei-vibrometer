#include "util/log.h"

#include <cstdio>
#include <ctime>
#include <mutex>

namespace vm {
namespace {
LogLevel g_level = LogLevel::kInfo;
std::mutex g_mutex;
}  // namespace

void logSetLevel(LogLevel level) { g_level = level; }

LogLevel logLevel() { return g_level; }

void logWrite(LogLevel level, const char* tag, const char* fmt, ...) {
  if (static_cast<int>(level) > static_cast<int>(g_level)) return;
  static const char kLetters[] = {'E', 'W', 'I', 'D'};
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  char msg[512];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(msg, sizeof(msg), fmt, ap);
  va_end(ap);
  std::lock_guard<std::mutex> lock(g_mutex);
  fprintf(stderr, "[%6lld.%03ld] %c %s: %s\n", static_cast<long long>(ts.tv_sec), ts.tv_nsec / 1000000,
          kLetters[static_cast<int>(level)], tag, msg);
}

}  // namespace vm
