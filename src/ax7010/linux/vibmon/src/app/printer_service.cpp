#include "app/printer_service.h"

#include "drv/escpos.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "printer";
}

PrinterService::PrinterService(const AppConfig::Printer& cfg, BoardService& board, StateStore& state,
                               std::string uartDevice)
    : cfg_(cfg), board_(board), state_(state), uartDevice_(std::move(uartDevice)) {
  thread_ = std::thread(&PrinterService::run, this);
}

PrinterService::~PrinterService() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stop_ = true;
  }
  abort_ = true;
  cv_.notify_all();
  if (thread_.joinable()) thread_.join();
  safeOff();
}

bool PrinterService::submit(PrintJob job, std::string& err) {
  if (!cfg_.enabled) {
    err = "printer_disabled";
    return false;
  }
  if (uartDevice_.empty()) {
    err = "no_printer_uart";
    return false;
  }
  std::lock_guard<std::mutex> lock(mutex_);
  if (busy_ || havejob_) {
    err = "busy";
    return false;
  }
  job_ = std::move(job);
  havejob_ = true;
  busy_ = true;
  abort_ = false;
  cv_.notify_all();
  return true;
}

void PrinterService::abort(const char* reason) {
  if (!busy_) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    abortReason_ = reason;
  }
  abort_ = true;
  cv_.notify_all();
}

void PrinterService::setState(const std::string& s) {
  state_.update([&](SystemState& st) { st.printerState = s; });
}

bool PrinterService::waitMs(int ms) {
  const int64_t end = monoMs() + ms;
  while (monoMs() < end) {
    if (abort_) return false;
    sleepMs(10);
  }
  return !abort_;
}

void PrinterService::safeOff() {
  board_.setPrinterComm(false);
  port_.close();
  board_.setPrinterPower(false);
}

bool PrinterService::execute(const PrintJob& job, std::string& stage) {
  stage = "precheck";
  if (gate_) {
    const std::string why = gate_();
    if (!why.empty()) {
      stage = why;
      return false;
    }
  }
  // Respect the minimum off time measured on the first board.
  const int64_t sinceOff = monoMs() - offSinceMs_;
  if (sinceOff < cfg_.minOffMs && !waitMs(static_cast<int>(cfg_.minOffMs - sinceOff))) return false;

  stage = "power_on";
  setState("power_on");
  if (!board_.setPrinterComm(false) || !board_.setPrinterPower(true)) return false;
  if (!waitMs(cfg_.powerOnWaitMs)) return false;

  if (job.powerOnly) {
    stage = "self_test_power";
    setState(stage);
    return waitMs(20000);  // no UART open and no host bytes; safeOff follows
  }

  stage = "open_uart";
  if (!port_.open(uartDevice_, cfg_.baud, cfg_.ctsFlow)) return false;

  stage = "wait_ready";
  setState("wait_ready");
  if (cfg_.ctsFlow) {
    const int64_t until = monoMs() + cfg_.readyTimeoutMs;
    while (!port_.ctsAsserted()) {  // CTS low (asserted) = printer accepts data
      if (abort_ || monoMs() > until) return false;
      sleepMs(20);
    }
  }
  port_.flushInput();
  port_.flushOutput();

  stage = "comm_on";
  if (!board_.setPrinterComm(true)) return false;
  sleepMs(20);

  stage = "send";
  setState("printing");
  std::string init = escpos::init();
  if (!port_.writeAll(reinterpret_cast<const uint8_t*>(init.data()), init.size(), cfg_.busyTimeoutMs, &abort_)) return false;
  const std::vector<std::string> strips = job.rawText.empty()
      ? escpos::rasterImage(job.bits, job.widthBytes, job.height, cfg_.stripRows)
      : std::vector<std::string>{job.rawText};
  auto drainTx = [&]() {
    const int64_t until = monoMs() + cfg_.busyTimeoutMs;
    while (port_.outputQueued() > 0) {
      if (abort_ || monoMs() > until) return false;
      sleepMs(1);
    }
    return !abort_;
  };
  for (const std::string& s : strips) {
    if (abort_) return false;
    // The kernel holds bytes while CTS is high (busy); writeAll gives up when
    // nothing moves for busyTimeoutMs.
    if (!port_.writeAll(reinterpret_cast<const uint8_t*>(s.data()), s.size(), cfg_.busyTimeoutMs, &abort_)) return false;
    if (cfg_.rasterPauseMs > 0 && job.rawText.empty()) {
      stage = "raster_wait";
      if (!drainTx() || !waitMs(cfg_.rasterPauseMs)) return false;
      stage = "send";
    }
  }
  const std::string feed = cfg_.feedDots > 0 ? escpos::feedDots(cfg_.feedDots)
                                             : escpos::feedLines(cfg_.feedLines);
  if (!port_.writeAll(reinterpret_cast<const uint8_t*>(feed.data()), feed.size(), cfg_.busyTimeoutMs, &abort_)) return false;

  stage = "drain";
  const int64_t drainUntil = monoMs() + cfg_.busyTimeoutMs;
  while (port_.outputQueued() > 0) {
    if (abort_ || monoMs() > drainUntil) return false;
    sleepMs(10);
  }

  // An empty UART queue is not a finished slip: either ask the printer
  // (in-order GS r 1) or wait the configured per-row estimate.
  stage = "wait_finish";
  // The final feed is queued behind the report. Keep printer power on until
  // both image rows and the configured paper advance have had time to finish.
  const int finishWaitMs = (job.height + cfg_.feedDots) * cfg_.msPerRow + cfg_.finishMarginMs;
  if (cfg_.statusQuery == "gs_r") {
    const std::string q = escpos::paperSensorStatus();
    port_.flushInput();
    if (!port_.writeAll(reinterpret_cast<const uint8_t*>(q.data()), q.size(), 1000, &abort_)) return false;
    const int64_t until = monoMs() + finishWaitMs + 2000;
    uint8_t b = 0;
    bool got = false;
    while (monoMs() < until && !abort_) {
      if (port_.waitReadable(50) && port_.readSome(&b, 1) == 1) {
        got = true;
        break;
      }
    }
    if (!got) return false;
    if (escpos::parseGsR1(b).paperOut) {
      stage = "paper_out";
      return false;
    }
  } else {
    if (!waitMs(finishWaitMs)) return false;
    if (cfg_.statusQuery == "dle_eot") {
      const std::string q = escpos::realtimeStatus(4);
      port_.flushInput();
      if (!port_.writeAll(reinterpret_cast<const uint8_t*>(q.data()), q.size(), 1000, &abort_)) return false;
      uint8_t b = 0;
      if (port_.waitReadable(500) && port_.readSome(&b, 1) == 1 && escpos::parseDleEot4(b).paperOut) {
        stage = "paper_out";
        return false;
      }
    }
  }
  stage = "done";
  return true;
}

void PrinterService::run() {
  for (;;) {
    PrintJob job;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stop_ || havejob_; });
      if (stop_) return;
      job = std::move(job_);
      havejob_ = false;
    }
    std::string stage;
    LOGI(kTag, "job %s: %d rows", job.id.c_str(), job.height);
    const bool ok = execute(job, stage);
    // Close in order: communication first, then power.
    board_.setPrinterComm(false);
    port_.close();
    board_.setPrinterPower(false);
    offSinceMs_ = monoMs();
    std::string err;
    if (!ok) {
      std::lock_guard<std::mutex> lock(mutex_);
      err = abort_ ? ("aborted:" + abortReason_) : ("failed_at:" + stage);
      LOGW(kTag, "job %s %s", job.id.c_str(), err.c_str());
    } else {
      LOGI(kTag, "job %s printed", job.id.c_str());
    }
    state_.update([&](SystemState& s) {
      s.printerState = "idle";
      s.printerError = err;
    });
    busy_ = false;
    abort_ = false;
    if (done_) done_(ok, job.id, err);
  }
}

}  // namespace vm
