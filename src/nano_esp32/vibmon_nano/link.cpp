#include "link.h"

#include "app_config.h"
#include "board_pins.h"
#include "log.h"

Link g_link;

namespace {
const char* kTag = "link";
volatile uint32_t s_uartErrors = 0;

void onUartError(hardwareSerial_error_t) { s_uartErrors = s_uartErrors + 1; }

void dispatch(Link::FrameHandler handler, const vp::Frame& f) {
  if (handler) handler(f);
}
}  // namespace

void Link::begin(uint32_t baud, bool flowControl, FrameHandler handler) {
  handler_ = handler;
  baud_ = baud;
  flow_ = flowControl;
  Serial1.setRxBufferSize(cfg::kUartRxBuffer);
  Serial1.setTxBufferSize(cfg::kUartTxBuffer);
  Serial1.begin(baud, SERIAL_8N1, board_pins::AX_RX, board_pins::AX_TX);
  if (flowControl) {
    // RTS (D4) low = Nano can receive; CTS (D5) low = AX can receive.
    Serial1.setPins(board_pins::AX_RX, board_pins::AX_TX, board_pins::AX_CTS, board_pins::AX_RTS);
    Serial1.setHwFlowCtrlMode(UART_HW_FLOWCTRL_CTS_RTS, cfg::kRtsThreshold);
  }
  Serial1.onReceiveError(onUartError);
  lastRxMs_ = millis();
  LOGI(kTag, "UART1 %lu 8N1, flow control %s", static_cast<unsigned long>(baud), flowControl ? "RTS/CTS" : "off");
}

void Link::poll() {
  uint8_t buf[256];
  const uint32_t now = millis();
  int avail = Serial1.available();
  while (avail > 0) {
    size_t n = Serial1.read(buf, avail > static_cast<int>(sizeof(buf)) ? sizeof(buf) : static_cast<size_t>(avail));
    if (n == 0) break;
    rxBytes_ += n;
    lastRxMs_ = now;
    FrameHandler h = handler_;
    decoder_.feed(buf, n, [h](const vp::Frame& f) { dispatch(h, f); });
    avail = Serial1.available();
  }
  if (decoder_.hasPartial() && now - lastRxMs_ > cfg::kPartialFrameTimeoutMs) {
    decoder_.expirePartial();
    LOGW(kTag, "partial frame dropped after %lu ms", static_cast<unsigned long>(cfg::kPartialFrameTimeoutMs));
  }
  uartErrors_ = s_uartErrors;
}

bool Link::send(const std::string& frame) {
  if (queuedBytes_ + frame.size() > cfg::kTxQueueBytes) {
    txDrops_++;
    return false;
  }
  queue_.push_back(frame);
  queuedBytes_ += frame.size();
  txFrames_++;
  return true;
}

void Link::flushTx() {
  while (!queue_.empty()) {
    const std::string& f = queue_.front();
    int room = Serial1.availableForWrite();
    if (room <= 0) return;
    size_t left = f.size() - frontOffset_;
    size_t n = left < static_cast<size_t>(room) ? left : static_cast<size_t>(room);
    size_t w = Serial1.write(reinterpret_cast<const uint8_t*>(f.data()) + frontOffset_, n);
    frontOffset_ += w;
    if (frontOffset_ < f.size()) return;
    queuedBytes_ -= f.size();
    queue_.pop_front();
    frontOffset_ = 0;
  }
}

void Link::describe(vp::JsonValue& obj) const {
  const vp::StreamDecoder::Stats& s = decoder_.stats();
  obj.set("baud", static_cast<unsigned long>(baud_));
  obj.set("flow_control", flow_);
  obj.set("rx_frames", static_cast<unsigned long>(s.frames));
  obj.set("crc_errors", static_cast<unsigned long>(s.crcErrors));
  obj.set("header_errors", static_cast<unsigned long>(s.headerErrors));
  obj.set("json_errors", static_cast<unsigned long>(s.jsonErrors));
  obj.set("partial_timeouts", static_cast<unsigned long>(s.partialTimeouts));
  obj.set("uart_errors", static_cast<unsigned long>(uartErrors_));
  obj.set("tx_drops", static_cast<unsigned long>(txDrops_));
}
