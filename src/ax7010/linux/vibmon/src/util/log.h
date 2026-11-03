#pragma once

#include <cstdarg>

// Line logger to stderr (captured by the init system / journald).
namespace vm {

enum class LogLevel { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3 };

void logSetLevel(LogLevel level);
LogLevel logLevel();
void logWrite(LogLevel level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));

}  // namespace vm

#define LOGE(tag, ...) ::vm::logWrite(::vm::LogLevel::kError, tag, __VA_ARGS__)
#define LOGW(tag, ...) ::vm::logWrite(::vm::LogLevel::kWarn, tag, __VA_ARGS__)
#define LOGI(tag, ...) ::vm::logWrite(::vm::LogLevel::kInfo, tag, __VA_ARGS__)
#define LOGD(tag, ...)                                                                   \
  do {                                                                                   \
    if (::vm::logLevel() >= ::vm::LogLevel::kDebug) ::vm::logWrite(::vm::LogLevel::kDebug, tag, __VA_ARGS__); \
  } while (0)
