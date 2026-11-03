#pragma once
#include <Arduino.h>

// Queued USB CDC + J605 logging. loop() drains each port without waiting for
// a terminal; whole new lines are dropped only when that port's queue is full.
// Never call from an ISR. Passwords and binary frames are not logged.
enum class LogLevel : uint8_t { kError = 0, kWarn = 1, kInfo = 2, kDebug = 3 };

void logBegin();
void logSetLevel(LogLevel level);
LogLevel logGetLevel();
void logPrintf(LogLevel level, const char* tag, const char* fmt, ...) __attribute__((format(printf, 3, 4)));
// Writes a raw line (console replies) with the same non-blocking rule.
void logRaw(const char* text);

#define LOGE(tag, ...) logPrintf(LogLevel::kError, tag, __VA_ARGS__)
#define LOGW(tag, ...) logPrintf(LogLevel::kWarn, tag, __VA_ARGS__)
#define LOGI(tag, ...) logPrintf(LogLevel::kInfo, tag, __VA_ARGS__)
#define LOGD(tag, ...) logPrintf(LogLevel::kDebug, tag, __VA_ARGS__)
