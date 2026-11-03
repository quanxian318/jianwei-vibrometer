#pragma once

#include <sys/types.h>

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "vibproto.h"

namespace vm {

class ByteChannel {
 public:
  virtual ~ByteChannel() = default;
  virtual ssize_t readSome(uint8_t* buf, size_t n) = 0;   // 0 = nothing available, -1 = error
  virtual ssize_t writeSome(const uint8_t* buf, size_t n) = 0;
};

struct NanoEvent {
  std::string bootId;
  uint32_t id = 0;
  std::string type;
  int64_t uptimeMs = 0;
  vp::JsonValue data;
};

struct NanoReply {
  bool ok = false;
  bool timeout = false;  // no response after all attempts (or link offline)
  std::string code;      // "OK", protocol error code, "TIMEOUT", "OFFLINE", "SESSION_RESET"
  vp::JsonValue data;
};

// AX side of protocol V1. Runs on one thread (the main event loop):
//  * HELLO with a fresh session_id, re-handshake on BAD_SESSION / new boot_id;
//  * at most 8 requests in flight, retries reuse seq and payload bytes;
//  * events processed exactly once in id order (boot_id + event_id), missing
//    ids fetched with DRAIN, contiguous prefix acknowledged with ACK_EVENTS;
//  * offline detection and an optional hardware reset hook (TCA P0.5).
class NanoLink {
 public:
  struct Config {
    int timeoutMs = 300;
    int maxAttempts = 3;
    size_t maxInflight = 8;
    int statusPeriodMs = 2000;
    int drainRetryMs = 200;
    int helloBackoffMs = 1000;
    int offlineResetMs = 10000;  // 0 = never reset the Nano
    int resetSettleMs = 3000;
    std::string axVersion = "vibmon";
  };
  struct Stats {
    uint32_t requests = 0, retries = 0, timeouts = 0, events = 0, duplicates = 0;
    uint32_t gaps = 0, handshakes = 0, resets = 0, staleResponses = 0;
  };
  using ReplyFn = std::function<void(const NanoReply&)>;
  using EventFn = std::function<void(const NanoEvent&)>;
  using OnlineFn = std::function<void(bool online, const std::string& bootId, bool newBoot)>;

  NanoLink(ByteChannel& channel, const Config& cfg);

  void setEventHandler(EventFn fn) { onEvent_ = std::move(fn); }
  void setOnlineHandler(OnlineFn fn) { onOnline_ = std::move(fn); }
  void setResetHook(std::function<void()> fn) { resetHook_ = std::move(fn); }

  void onReadable(int64_t nowMs);
  void tick(int64_t nowMs);
  void setIrqLevel(bool high) { irqHigh_ = high; }

  bool online() const { return state_ == State::kOnline; }
  const std::string& bootId() const { return bootId_; }
  const std::string& sessionId() const { return session_; }
  const vp::JsonValue& lastStatus() const { return lastStatus_; }
  const vp::JsonValue& helloData() const { return helloData_; }
  const Stats& stats() const { return stats_; }
  const vp::StreamDecoder::Stats& decoderStats() const { return decoder_.stats(); }

  // Adds session_id to payload and sends; cb always runs exactly once.
  void request(uint8_t opcode, vp::JsonValue payload, ReplyFn cb);
  // Returns the generated job_id.
  std::string playVoice(const std::string& assetId, int volume, ReplyFn cb);

 private:
  enum class State { kOffline, kHandshake, kOnline };
  struct Pending {
    uint16_t seq = 0;
    uint8_t opcode = 0;
    std::string frame;
    int attempts = 0;
    int64_t deadlineMs = 0;
    ReplyFn cb;
  };

  void startHello(int64_t nowMs);
  void goOffline(int64_t nowMs, const char* why);
  bool send(uint8_t opcode, const vp::JsonValue& payload, ReplyFn cb, int64_t nowMs);
  void writeFrame(const std::string& frame);
  void flushTx();
  void onFrame(const vp::Frame& f, int64_t nowMs);
  void onResponse(const vp::Frame& f, int64_t nowMs);
  void handleEvent(const vp::JsonValue& ev, bool fromDrain, int64_t nowMs);
  void deliverReady();
  void maybeDrain(int64_t nowMs);
  void maybeAck(int64_t nowMs);
  void failAll(const char* code);
  void pumpBacklog();
  uint16_t nextSeq();
  std::string randomHex(int bytes);

  ByteChannel& ch_;
  Config cfg_;
  vp::StreamDecoder decoder_;
  State state_ = State::kOffline;
  std::string session_;
  std::string bootId_;
  uint16_t seq_ = 0;
  std::deque<Pending> inflight_;
  std::deque<std::pair<Pending, vp::JsonValue>> backlog_;  // waiting for an in-flight slot
  std::string txBuf_;
  int64_t nowMs_ = 0;  // time of the current onReadable()/tick() call
  int64_t lastRxMs_ = 0;
  int64_t nextHelloMs_ = 0;
  int64_t offlineSinceMs_ = 0;
  int64_t lastResetMs_ = -1000000;
  int64_t nextStatusMs_ = 0;
  int64_t lastDrainMs_ = 0;
  int consecutiveTimeouts_ = 0;
  bool irqHigh_ = false;
  bool drainInflight_ = false;
  bool ackInflight_ = false;
  bool needDrain_ = false;
  uint32_t lastProcessed_ = 0;
  uint32_t ackedSent_ = 0;
  std::map<uint32_t, vp::JsonValue> outOfOrder_;
  uint32_t jobCounter_ = 0;
  vp::JsonValue lastStatus_;
  vp::JsonValue helloData_;
  Stats stats_;
  EventFn onEvent_;
  OnlineFn onOnline_;
  std::function<void()> resetHook_;
};

}  // namespace vm
