#include <Arduino.h>
#include <cassert>
#include <iostream>
#include <set>
#include <thread>
#include <vector>

#include "console_io.h"

TestSerial Serial, Serial0;
uint32_t testMillis = 0;
std::vector<std::string> commands;

void command(const std::string& line) {
  commands.push_back(line);
  const std::string reply = "reply:" + line + "\r\n";
  console_io::write(reply.data(), reply.size());
}

void pump(size_t loops, size_t usbRoom = 64, size_t uartRoom = 64) {
  for (size_t i = 0; i < loops; ++i) {
    Serial.quota = usbRoom; Serial0.quota = uartRoom;
    console_io::poll(command);
  }
}

void clean() {
  pump(600);
  Serial.input.clear(); Serial0.input.clear();
  Serial.output.clear(); Serial0.output.clear(); commands.clear();
}

int main() {
  console_io::begin();
  assert(Serial.baud == 115200 && Serial.timeout == 0);
  assert(Serial0.baud == 115200 && Serial0.config == SERIAL_8N1);
  assert(Serial0.rxPin == D0 && Serial0.txPin == D1);
  assert(Serial0.flow == UART_HW_FLOWCTRL_DISABLE);
  assert(Serial0.rxBuffer == 1024 && Serial0.txBuffer == 1024);

  // Independent command assembly, CR/LF/CRLF and mirrored replies.
  Serial.input = "sta"; Serial0.input = "ping\r\n";
  pump(2); assert(commands == std::vector<std::string>{"ping"});
  Serial.input = "tus\n"; pump(2);
  assert((commands == std::vector<std::string>{"ping", "status"}));
  assert(Serial.output == "reply:ping\r\nreply:status\r\n");
  assert(Serial0.output == Serial.output);
  clean();

  // UART remains useful with no USB terminal attached.
  Serial.connected = false; Serial0.input = "help\n"; pump(3);
  assert(Serial.output.empty() && Serial0.output == "reply:help\r\n");
  Serial.connected = true; clean();

  // Regression: status JSON longer than the 64-byte TinyUSB TX FIFO survives.
  const std::string status = "{\"status\":\"" + std::string(2500, 's') + "\"}\r\n";
  console_io::write(status.data(), status.size()); pump(500, 64, 7);
  assert(Serial.output == status && Serial0.output == status);
  clean();

  // A stalled USB FIFO cannot block J605 or commands; it drains when resumed.
  console_io::write(status.data(), status.size()); pump(50, 0, 64);
  assert(Serial.output.empty() && Serial0.output == status);
  Serial0.input = "ping\n"; pump(2, 0, 64);
  assert(commands == std::vector<std::string>{"ping"});
  pump(100);
  assert(Serial.output == status + "reply:ping\r\n");
  clean();

  // Ring wrap and whole-message overflow preserve existing JSON/log bytes.
  const std::string first(12000, 'a'), second(8000, 'b'), tooBig(17000, 'x');
  console_io::write(first.data(), first.size()); pump(150);
  console_io::write(second.data(), second.size());
  console_io::write(tooBig.data(), tooBig.size()); pump(500);
  assert(Serial.output == first + second && Serial0.output == first + second);
  clean();

  // Overlong input must not execute a truncated command such as "reboot".
  Serial0.input = "reboot" + std::string(180, ' ') + "\r\nping\n";
  pump(8);
  assert(commands == std::vector<std::string>{"ping"});
  assert(Serial0.output == "error: command too long\r\nreply:ping\r\n");
  clean();

  // A disconnected USB terminal must lose both its partial command and backlog.
  Serial.input = "reb"; pump(1);
  console_io::write(status.data(), status.size()); pump(1, 0, 64);
  Serial.connected = false; pump(1);
  Serial.connected = true; Serial.input = "oot\n"; pump(100);
  assert(commands == std::vector<std::string>{"oot"});
  assert(Serial.output == "reply:oot\r\n");
  clean();

  // Partial writes must advance by the actual byte count only.
  Serial.maxWrite = 3; Serial0.maxWrite = 2;
  console_io::write(status.data(), status.size()); pump(1400);
  assert(Serial.output == status && Serial0.output == status);
  Serial.maxWrite = Serial0.maxWrite = 16384; clean();

  // Audio-task/main-task producers cannot interleave bytes within a line.
  std::set<std::string> expected;
  std::vector<std::thread> producers;
  for (int p = 0; p < 4; ++p) {
    for (int i = 0; i < 100; ++i) expected.insert("log-" + std::to_string(p) + "-" + std::to_string(i));
    producers.emplace_back([p]() {
      for (int i = 0; i < 100; ++i) {
        const std::string line = "log-" + std::to_string(p) + "-" + std::to_string(i) + "\n";
        console_io::write(line.data(), line.size());
      }
    });
  }
  for (auto& producer : producers) producer.join();
  pump(100);
  assert(Serial.output == Serial0.output);
  std::set<std::string> actual;
  size_t pos = 0;
  while (pos < Serial.output.size()) {
    const size_t end = Serial.output.find('\n', pos); assert(end != std::string::npos);
    assert(actual.insert(Serial.output.substr(pos, end - pos)).second); pos = end + 1;
  }
  assert(actual == expected);
  std::cout << "console_io: 10 scenarios passed (400 concurrent log lines)\n";
}
