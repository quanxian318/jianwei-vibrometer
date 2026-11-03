#include "console_io.h"

#include <Arduino.h>
#include <string.h>

#include "app_config.h"
#include "board_pins.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace console_io {
namespace {
constexpr size_t kCapacity = 16384;
constexpr size_t kPollBytes = 128;
constexpr size_t kCommandBytes = 160;

struct Output {
  uint8_t bytes[kCapacity];
  size_t head = 0;
  size_t size = 0;

  void clear() { head = size = 0; }
  void append(const char* text, size_t len) {
    // Drop a whole new line on overflow; never splice partial JSON/log lines.
    if (len > kCapacity - size) return;
    const size_t tail = (head + size) % kCapacity;
    const size_t first = len < kCapacity - tail ? len : kCapacity - tail;
    memcpy(bytes + tail, text, first);
    memcpy(bytes, text + first, len - first);
    size += len;
  }
};

struct Input {
  std::string line;
  bool overflow = false;
};

Output g_usbOutput, g_uartOutput;
Input g_usbInput, g_uartInput;
SemaphoreHandle_t g_mutex = nullptr;

template <typename Port>
void readCommands(Port& port, Input& input, Handler handler) {
  for (size_t i = 0; i < kPollBytes && port.available() > 0; ++i) {
    const int ch = port.read();
    if (ch < 0) return;
    if (ch == '\r' || ch == '\n') {
      if (input.overflow) {
        static const char error[] = "error: command too long\r\n";
        write(error, sizeof(error) - 1);
      } else if (!input.line.empty()) {
        handler(input.line);
      } else {
        continue;  // includes the LF after a CRLF command
      }
      input.line.clear();
      input.overflow = false;
      return;  // at most one command per port per loop
    } else if (!input.overflow) {
      if (input.line.size() < kCommandBytes) {
        input.line += static_cast<char>(ch);
      } else {
        input.line.clear();
        input.overflow = true;  // discard through newline, never execute a prefix
      }
    }
  }
}

template <typename Port>
void drain(Port& port, Output& output) {
  size_t budget = kPollBytes;
  while (output.size > 0 && budget > 0) {
    const int room = port.availableForWrite();
    if (room <= 0) return;
    size_t count = output.size;
    if (count > kCapacity - output.head) count = kCapacity - output.head;
    if (count > budget) count = budget;
    if (count > static_cast<size_t>(room)) count = static_cast<size_t>(room);
    const size_t written = port.write(output.bytes + output.head, count);
    output.head = (output.head + written) % kCapacity;
    output.size -= written;
    budget -= written;
    if (written < count) return;
  }
}
}  // namespace

void begin() {
  g_mutex = xSemaphoreCreateMutex();
  Serial.begin(cfg::kDebugBaud);
  Serial.setTxTimeoutMs(0);
  Serial0.setRxBufferSize(1024);
  Serial0.setTxBufferSize(1024);
  Serial0.begin(cfg::kDebugBaud, SERIAL_8N1, board_pins::DEBUG_RX, board_pins::DEBUG_TX);
  Serial0.setHwFlowCtrlMode(UART_HW_FLOWCTRL_DISABLE);
}

void write(const char* text, size_t len) {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  if (Serial) g_usbOutput.append(text, len);
  else g_usbOutput.clear();
  g_uartOutput.append(text, len);
  xSemaphoreGive(g_mutex);
}

void flush() {
  if (!g_mutex) return;
  xSemaphoreTake(g_mutex, portMAX_DELAY);
  if (Serial) drain(Serial, g_usbOutput);
  else g_usbOutput.clear();
  drain(Serial0, g_uartOutput);
  xSemaphoreGive(g_mutex);
}

void poll(Handler handler) {
  // A disconnected USB terminal must not leave half a command for a new one.
  if (Serial) readCommands(Serial, g_usbInput, handler);
  else { g_usbInput.line.clear(); g_usbInput.overflow = false; }
  readCommands(Serial0, g_uartInput, handler);
  flush();
}

void drainBeforeRestart(uint32_t timeoutMs) {
  const uint32_t started = millis();
  do {
    flush();
    delay(1);
  } while (static_cast<uint32_t>(millis() - started) < timeoutMs);
}
}  // namespace console_io
