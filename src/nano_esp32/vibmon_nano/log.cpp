#include "log.h"

#include <stdarg.h>

#include "console_io.h"

namespace {
LogLevel g_level = LogLevel::kInfo;
}  // namespace

void logBegin() {
  console_io::begin();
}

void logSetLevel(LogLevel level) { g_level = level; }

LogLevel logGetLevel() { return g_level; }

void logPrintf(LogLevel level, const char* tag, const char* fmt, ...) {
  if (static_cast<uint8_t>(level) > static_cast<uint8_t>(g_level)) return;
  static const char kLetters[] = {'E', 'W', 'I', 'D'};
  char line[256];
  int n = snprintf(line, sizeof(line), "[%10lu] %c %s: ", static_cast<unsigned long>(millis()),
                   kLetters[static_cast<uint8_t>(level)], tag);
  if (n < 0) return;
  va_list ap;
  va_start(ap, fmt);
  int m = vsnprintf(line + n, sizeof(line) - n - 2, fmt, ap);
  va_end(ap);
  if (m < 0) return;
  size_t len = static_cast<size_t>(n + m);
  if (len > sizeof(line) - 3) len = sizeof(line) - 3;
  line[len++] = '\r';
  line[len++] = '\n';
  line[len] = 0;
  console_io::write(line, len);
}

void logRaw(const char* text) {
  console_io::write(text, strlen(text));
}
