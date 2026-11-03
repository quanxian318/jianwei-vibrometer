#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/board_service.h"
#include "app/config.h"
#include "app/state.h"
#include "hw/serial_port.h"

namespace vm {

struct PrintJob {
  std::string id;  // measurement id the slip belongs to
  std::string rawText;  // optional ASCII diagnostic; reports use bitmap data
  bool powerOnly = false;  // timed power window for the printer's own FEED self-test
  std::vector<uint8_t> bits;
  int widthBytes = 48;
  int height = 0;
};

// EM5820H power/communication sequence (主载板方案 §9, 成品指南 §12.8):
// PRN_COM_EN=0 -> PRN_PWR_EN=1 -> start-up wait -> CTS ready -> flush ->
// PRN_COM_EN=1 -> CTS-paced raster strips -> wait for completion ->
// PRN_COM_EN=0 -> PRN_PWR_EN=0 -> minimum off time. Every step is bounded
// and a failure records the stage; nothing is retried automatically.
class PrinterService {
 public:
  using DoneFn = std::function<void(bool ok, const std::string& jobId, const std::string& error)>;
  // Asked before power-on: lights off, power budget and power-fail checks.
  using GateFn = std::function<std::string()>;  // "" = allowed, otherwise the reason

  PrinterService(const AppConfig::Printer& cfg, BoardService& board, StateStore& state, std::string uartDevice);
  ~PrinterService();

  void setDoneHandler(DoneFn fn) { done_ = std::move(fn); }
  void setGate(GateFn fn) { gate_ = std::move(fn); }
  bool submit(PrintJob job, std::string& err);
  void abort(const char* reason);  // power fail / shutdown
  bool busy() const { return busy_; }

 private:
  void run();
  bool execute(const PrintJob& job, std::string& stage);
  bool waitMs(int ms);  // false when aborted
  void setState(const std::string& s);
  void safeOff();

  const AppConfig::Printer& cfg_;
  BoardService& board_;
  StateStore& state_;
  std::string uartDevice_;
  SerialPort port_;
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool stop_ = false;
  bool havejob_ = false;
  PrintJob job_;
  std::atomic<bool> busy_{false};
  std::atomic<bool> abort_{false};
  std::string abortReason_;
  int64_t offSinceMs_ = -1000000;
  DoneFn done_;
  GateFn gate_;
};

}  // namespace vm
