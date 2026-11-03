// Actual pseudo-terminal backpressure: cancellation must interrupt a blocked print write.
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "hw/serial_port.h"
#include "util/clock.h"
#include "test_util.h"
using namespace vm;
int main() {
  int master = posix_openpt(O_RDWR | O_NOCTTY);
  CHECK(master >= 0);
  CHECK(grantpt(master) == 0 && unlockpt(master) == 0);
  SerialPort port;
  CHECK(port.open(ptsname(master), 9600, false));
  std::vector<uint8_t> data(4 * 1024 * 1024, 0x55);
  std::atomic<bool> abort{false};
  bool sent = true;
  auto start = monoMs();
  std::thread worker([&]() { sent = port.writeAll(data.data(), data.size(), 10000, &abort); });
  sleepMs(60);
  abort = true;
  worker.join();
  CHECK(!sent);
  CHECK(monoMs() - start < 1000);
  port.close();
  close(master);
  return testSummary("serial");
}
