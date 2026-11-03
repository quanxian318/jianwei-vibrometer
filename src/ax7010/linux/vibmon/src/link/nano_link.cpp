#include "link/nano_link.h"

#include <random>

#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "nano";
const size_t kMaxTxBacklog = 16384;
const size_t kMaxOutOfOrder = 64;
const uint16_t kSeqRenewAt = 65000;
}  // namespace

NanoLink::NanoLink(ByteChannel& channel, const Config& cfg) : ch_(channel), cfg_(cfg) {}

std::string NanoLink::randomHex(int bytes) {
  static const char kHex[] = "0123456789abcdef";
  std::random_device rd;
  std::string s;
  for (int i = 0; i < bytes; ++i) {
    unsigned v = rd() & 0xFF;
    s += kHex[v >> 4];
    s += kHex[v & 0x0F];
  }
  return s;
}

uint16_t NanoLink::nextSeq() {
  seq_ = seq_ >= 65535 ? 1 : static_cast<uint16_t>(seq_ + 1);
  return seq_;
}

void NanoLink::writeFrame(const std::string& frame) {
  if (txBuf_.size() + frame.size() > kMaxTxBacklog) {
    LOGW(kTag, "TX backlog full (link stalled?), dropping %zu bytes", txBuf_.size());
    txBuf_.clear();
  }
  txBuf_ += frame;
  flushTx();
}

void NanoLink::flushTx() {
  while (!txBuf_.empty()) {
    ssize_t n = ch_.writeSome(reinterpret_cast<const uint8_t*>(txBuf_.data()), txBuf_.size());
    if (n <= 0) break;
    txBuf_.erase(0, static_cast<size_t>(n));
  }
}

bool NanoLink::send(uint8_t opcode, const vp::JsonValue& payload, ReplyFn cb, int64_t nowMs) {
  Pending p;
  p.seq = nextSeq();
  p.opcode = opcode;
  if (!vp::encodeFrame(vp::kKindRequest, opcode, p.seq, payload, p.frame)) {
    NanoReply r;
    r.code = "BAD_ARGUMENT";
    if (cb) cb(r);
    return false;
  }
  p.attempts = 1;
  p.deadlineMs = nowMs + cfg_.timeoutMs;
  p.cb = std::move(cb);
  stats_.requests++;
  const std::string frame = p.frame;
  inflight_.push_back(std::move(p));
  writeFrame(frame);
  return true;
}

void NanoLink::request(uint8_t opcode, vp::JsonValue payload, ReplyFn cb) {
  if (state_ != State::kOnline) {
    NanoReply r;
    r.timeout = true;
    r.code = "OFFLINE";
    if (cb) cb(r);
    return;
  }
  vp::JsonValue p = vp::JsonValue::makeObject();
  p.set("session_id", session_);
  for (const auto& m : payload.members()) {
    if (m.key != "session_id") p.set(m.key, m.value);
  }
  if (inflight_.size() >= cfg_.maxInflight) {
    Pending w;
    w.opcode = opcode;
    w.cb = std::move(cb);
    backlog_.emplace_back(std::move(w), p);
    return;
  }
  send(opcode, p, std::move(cb), nowMs_);
}

void NanoLink::pumpBacklog() {
  while (!backlog_.empty() && inflight_.size() < cfg_.maxInflight && state_ == State::kOnline) {
    auto b = std::move(backlog_.front());
    backlog_.pop_front();
    send(b.first.opcode, b.second, std::move(b.first.cb), nowMs_);
  }
}

std::string NanoLink::playVoice(const std::string& assetId, int volume, ReplyFn cb) {
  std::string job = "v" + std::to_string(++jobCounter_) + "-" + session_.substr(0, 6);
  vp::JsonValue p = vp::JsonValue::makeObject();
  p.set("job_id", job);
  p.set("asset_id", assetId);
  p.set("volume", volume < 0 ? 0 : (volume > 100 ? 100 : volume));
  request(vp::kOpPlayVoice, p, std::move(cb));
  return job;
}

void NanoLink::failAll(const char* code) {
  std::deque<Pending> pend;
  pend.swap(inflight_);
  std::deque<std::pair<Pending, vp::JsonValue>> back;
  back.swap(backlog_);
  NanoReply r;
  r.timeout = true;
  r.code = code;
  for (auto& p : pend) {
    if (p.cb) p.cb(r);
  }
  for (auto& b : back) {
    if (b.first.cb) b.first.cb(r);
  }
  drainInflight_ = false;
  ackInflight_ = false;
}

void NanoLink::startHello(int64_t nowMs) {
  const bool wasOnline = state_ == State::kOnline;
  state_ = State::kHandshake;
  failAll("SESSION_RESET");
  if (wasOnline && onOnline_) onOnline_(false, bootId_, false);
  session_ = randomHex(8);
  seq_ = 0;
  stats_.handshakes++;
  vp::JsonValue p = vp::JsonValue::makeObject();
  p.set("session_id", session_);
  p.set("protocol", 1);
  p.set("ax_version", cfg_.axVersion);
  const std::string sid = session_;
  send(vp::kOpHello, p, [this, sid](const NanoReply& r) {
    if (sid != session_) return;  // superseded by a newer handshake
    if (!r.ok) {
      goOffline(nowMs_, r.code.c_str());
      return;
    }
    std::string boot;
    r.data.getString("nano_boot_id", boot);
    helloData_ = r.data;
    const bool newBoot = boot != bootId_;
    bootId_ = boot;
    int64_t acked = 0;
    const vp::JsonValue* cursor = r.data.find("event_cursor");
    if (cursor) cursor->getInt("acked_through", acked);
    if (newBoot) {
      lastProcessed_ = static_cast<uint32_t>(acked);
      outOfOrder_.clear();
    } else if (static_cast<uint32_t>(acked) > lastProcessed_) {
      lastProcessed_ = static_cast<uint32_t>(acked);
    }
    ackedSent_ = static_cast<uint32_t>(acked);
    state_ = State::kOnline;
    consecutiveTimeouts_ = 0;
    offlineSinceMs_ = 0;
    needDrain_ = true;
    nextStatusMs_ = 0;
    LOGI(kTag, "online: session %s, nano boot %s%s", session_.c_str(), bootId_.c_str(), newBoot ? " (new boot)" : "");
    if (onOnline_) onOnline_(true, bootId_, newBoot);
  }, nowMs);
}

void NanoLink::goOffline(int64_t nowMs, const char* why) {
  const bool wasOnline = state_ == State::kOnline;
  state_ = State::kOffline;
  failAll("OFFLINE");
  nextHelloMs_ = nowMs + cfg_.helloBackoffMs;
  if (offlineSinceMs_ == 0) offlineSinceMs_ = nowMs;
  if (wasOnline) {
    LOGW(kTag, "offline (%s)", why);
    if (onOnline_) onOnline_(false, bootId_, false);
  }
}

void NanoLink::onReadable(int64_t nowMs) {
  nowMs_ = nowMs;
  uint8_t buf[512];
  for (;;) {
    ssize_t n = ch_.readSome(buf, sizeof(buf));
    if (n <= 0) break;
    lastRxMs_ = nowMs;
    decoder_.feed(buf, static_cast<size_t>(n), [this, nowMs](const vp::Frame& f) { onFrame(f, nowMs); });
  }
}

void NanoLink::onFrame(const vp::Frame& f, int64_t nowMs) {
  if (f.kind == vp::kKindResponse) {
    onResponse(f, nowMs);
  } else if (f.kind == vp::kKindEvent && f.opcode == vp::kOpEvent) {
    handleEvent(f.payload, false, nowMs);
    deliverReady();
    maybeAck(nowMs);
  }
}

void NanoLink::onResponse(const vp::Frame& f, int64_t nowMs) {
  auto it = inflight_.begin();
  for (; it != inflight_.end(); ++it) {
    if (it->seq == f.seq) break;
  }
  std::string sid;
  f.payload.getString("session_id", sid);
  if (it == inflight_.end() || it->opcode != f.opcode || sid != session_) {
    stats_.staleResponses++;
    return;
  }
  Pending p = std::move(*it);
  inflight_.erase(it);
  consecutiveTimeouts_ = 0;
  NanoReply r;
  r.ok = false;
  f.payload.getBool("ok", r.ok);
  f.payload.getString("code", r.code);
  const vp::JsonValue* d = f.payload.find("data");
  if (d) r.data = *d;
  if (p.cb) p.cb(r);
  if (!r.ok && r.code == "BAD_SESSION" && state_ == State::kOnline) {
    LOGW(kTag, "Nano rejected the session, handshaking again");
    startHello(nowMs);
    return;
  }
  pumpBacklog();
}

void NanoLink::handleEvent(const vp::JsonValue& ev, bool fromDrain, int64_t nowMs) {
  std::string boot;
  int64_t id = 0;
  if (!ev.getString("nano_boot_id", boot) || !ev.getInt("event_id", id) || id <= 0) return;
  if (state_ != State::kOnline) return;  // drained again after the handshake
  if (boot != bootId_) {
    LOGW(kTag, "event from new Nano boot %s, handshaking again", boot.c_str());
    startHello(nowMs);
    return;
  }
  const uint32_t eid = static_cast<uint32_t>(id);
  if (eid <= lastProcessed_) {
    stats_.duplicates++;
    return;
  }
  if (eid != lastProcessed_ + 1) {
    if (!fromDrain) {
      // Arrived ahead of a missing one: keep it and ask for the gap.
      if (outOfOrder_.size() < kMaxOutOfOrder) outOfOrder_[eid] = ev;
      needDrain_ = true;
      return;
    }
    // DRAIN returns the oldest event still queued: the ones before it were
    // dropped by the Nano (queue overflow). Record the gap and continue.
    stats_.gaps++;
    LOGW(kTag, "event gap: %u..%u lost", lastProcessed_ + 1, eid - 1);
    lastProcessed_ = eid - 1;
  }
  NanoEvent e;
  e.bootId = boot;
  e.id = eid;
  ev.getString("event_type", e.type);
  ev.getInt("uptime_ms", e.uptimeMs);
  const vp::JsonValue* d = ev.find("data");
  if (d) e.data = *d;
  lastProcessed_ = eid;
  stats_.events++;
  if (onEvent_) onEvent_(e);
}

void NanoLink::deliverReady() {
  while (!outOfOrder_.empty()) {
    auto it = outOfOrder_.begin();
    if (it->first <= lastProcessed_) {
      outOfOrder_.erase(it);
      continue;
    }
    if (it->first != lastProcessed_ + 1) break;
    vp::JsonValue ev = it->second;
    outOfOrder_.erase(it);
    handleEvent(ev, false, nowMs_);
  }
}

void NanoLink::maybeDrain(int64_t nowMs) {
  if (state_ != State::kOnline || drainInflight_) return;
  const bool want = needDrain_ || !outOfOrder_.empty() || (irqHigh_ && nowMs - lastDrainMs_ >= cfg_.drainRetryMs);
  if (!want) return;
  needDrain_ = false;
  drainInflight_ = true;
  lastDrainMs_ = nowMs;
  vp::JsonValue p = vp::JsonValue::makeObject();
  p.set("after_event_id", static_cast<unsigned long>(lastProcessed_));
  p.set("limit", 8);
  const std::string sid = session_;
  request(vp::kOpDrainEvents, p, [this, sid](const NanoReply& r) {
    if (sid != session_) return;
    drainInflight_ = false;
    if (!r.ok) return;
    const vp::JsonValue* events = r.data.find("events");
    if (events && events->isArray()) {
      for (const auto& ev : events->items()) handleEvent(ev, true, nowMs_);
    }
    deliverReady();
    bool more = false;
    r.data.getBool("more", more);
    if (more) needDrain_ = true;
    maybeAck(nowMs_);
  });
}

void NanoLink::maybeAck(int64_t nowMs) {
  (void)nowMs;
  if (state_ != State::kOnline || ackInflight_ || lastProcessed_ <= ackedSent_) return;
  ackInflight_ = true;
  const uint32_t through = lastProcessed_;
  vp::JsonValue p = vp::JsonValue::makeObject();
  p.set("nano_boot_id", bootId_);
  p.set("through_event_id", static_cast<unsigned long>(through));
  const std::string sid = session_;
  request(vp::kOpAckEvents, p, [this, sid, through](const NanoReply& r) {
    if (sid != session_) return;
    ackInflight_ = false;
    if (r.ok && through > ackedSent_) ackedSent_ = through;
  });
}

void NanoLink::tick(int64_t nowMs) {
  nowMs_ = nowMs;
  flushTx();
  if (decoder_.hasPartial() && nowMs - lastRxMs_ > 200) decoder_.expirePartial();

  // Retransmit with the same seq and bytes; give up after maxAttempts.
  std::vector<Pending> expired;
  for (auto it = inflight_.begin(); it != inflight_.end();) {
    if (nowMs < it->deadlineMs) {
      ++it;
      continue;
    }
    if (it->attempts < cfg_.maxAttempts) {
      it->attempts++;
      it->deadlineMs = nowMs + cfg_.timeoutMs;
      stats_.retries++;
      writeFrame(it->frame);
      ++it;
    } else {
      expired.push_back(std::move(*it));
      it = inflight_.erase(it);
    }
  }
  for (auto& p : expired) {
    stats_.timeouts++;
    consecutiveTimeouts_++;
    if (p.opcode == vp::kOpDrainEvents) drainInflight_ = false;
    if (p.opcode == vp::kOpAckEvents) ackInflight_ = false;
    NanoReply r;
    r.timeout = true;
    r.code = "TIMEOUT";
    if (p.cb) p.cb(r);
  }
  if (state_ == State::kOnline && consecutiveTimeouts_ >= 2) goOffline(nowMs, "no response");

  switch (state_) {
    case State::kOffline:
      if (nowMs < nextHelloMs_) break;
      if (resetHook_ && cfg_.offlineResetMs > 0 && offlineSinceMs_ != 0 &&
          nowMs - offlineSinceMs_ >= cfg_.offlineResetMs && nowMs - lastResetMs_ >= cfg_.offlineResetMs) {
        LOGW(kTag, "Nano silent for %lld ms, pulsing ESP_RESET", static_cast<long long>(nowMs - offlineSinceMs_));
        resetHook_();
        lastResetMs_ = nowMs;
        stats_.resets++;
        nextHelloMs_ = nowMs + cfg_.resetSettleMs;
        break;
      }
      startHello(nowMs);
      break;
    case State::kHandshake:
      break;
    case State::kOnline:
      if (seq_ >= kSeqRenewAt && inflight_.empty() && backlog_.empty()) {
        startHello(nowMs);  // new session before seq wraps
        break;
      }
      pumpBacklog();
      if (nowMs >= nextStatusMs_) {
        nextStatusMs_ = nowMs + cfg_.statusPeriodMs;
        const std::string sid = session_;
        request(vp::kOpGetStatus, vp::JsonValue::makeObject(), [this, sid](const NanoReply& r) {
          if (sid == session_ && r.ok) lastStatus_ = r.data;
        });
      }
      maybeDrain(nowMs);
      maybeAck(nowMs);
      break;
  }
}

}  // namespace vm
