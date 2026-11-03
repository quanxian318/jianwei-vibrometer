#pragma once
#include <Arduino.h>

#include <deque>
#include <string>

#include <vibproto.h>

// UART1 transport to the AX7010 (D2 TX, D3 RX, D4 RTS, D5 CTS, 8N1).
// Writes never block: frames wait in a bounded queue while CTS holds the
// line. Received bytes go through vp::StreamDecoder with a 200 ms
// partial-frame deadline.
class Link {
 public:
  typedef void (*FrameHandler)(const vp::Frame& frame);

  void begin(uint32_t baud, bool flowControl, FrameHandler handler);
  void poll();     // read + decode
  void flushTx();  // move queued bytes into the UART without blocking
  // Queues one encoded frame; false when the queue is full (frame dropped).
  bool send(const std::string& frame);
  bool txIdle() const { return queue_.empty(); }
  size_t txQueuedBytes() const { return queuedBytes_; }
  uint32_t baud() const { return baud_; }
  bool flowControl() const { return flow_; }
  void describe(vp::JsonValue& obj) const;

 private:
  vp::StreamDecoder decoder_;
  FrameHandler handler_ = nullptr;
  std::deque<std::string> queue_;
  size_t frontOffset_ = 0;
  size_t queuedBytes_ = 0;
  uint32_t lastRxMs_ = 0;
  uint32_t baud_ = 0;
  bool flow_ = true;
  uint32_t txDrops_ = 0;
  uint32_t rxBytes_ = 0;
  uint32_t txFrames_ = 0;
  uint32_t uartErrors_ = 0;
};

extern Link g_link;
