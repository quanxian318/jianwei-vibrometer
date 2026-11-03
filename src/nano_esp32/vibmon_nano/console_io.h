#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string>

// USB CDC and J605 UART0 share commands and mirrored output. Only loop()
// drains the ports; other tasks may enqueue complete log lines.
namespace console_io {
using Handler = void (*)(const std::string&);
void begin();
void poll(Handler handler);
void write(const char* text, size_t len);
void flush();
void drainBeforeRestart(uint32_t timeoutMs);
}  // namespace console_io
