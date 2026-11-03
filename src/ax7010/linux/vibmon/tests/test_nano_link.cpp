// NanoLink against an in-process fake Nano that follows protocol V1 (session,
// seq replay cache, event queue, DRAIN/ACK), with frame loss and reboots.
#include <cstdio>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "link/nano_link.h"
#include "test_util.h"

using namespace vm;

namespace {

struct Wire : ByteChannel {
  std::string* rx = nullptr;  // bytes waiting for this end
  std::string* tx = nullptr;  // bytes sent by this end
  ssize_t readSome(uint8_t* buf, size_t n) override {
    if (rx->empty()) return 0;
    size_t k = std::min(n, rx->size());
    memcpy(buf, rx->data(), k);
    rx->erase(0, k);
    return static_cast<ssize_t>(k);
  }
  ssize_t writeSome(const uint8_t* buf, size_t n) override {
    tx->append(reinterpret_cast<const char*>(buf), n);
    return static_cast<ssize_t>(n);
  }
};

class FakeNano {
 public:
  std::string bootId = "boot-1";
  std::string session;
  std::map<uint16_t, std::pair<std::string, std::string>> cache;  // seq -> (payload, response frame)
  int maxSeq = -1;
  std::deque<std::pair<uint32_t, std::string>> queue;  // event id, json
  uint32_t nextId = 1;
  uint32_t acked = 0;
  bool silent = false;
  int dropResponses = 0;
  bool dropEvents = false;
  int plays = 0;
  std::string* toAx = nullptr;
  vp::StreamDecoder dec;

  void reboot(const std::string& id) {
    bootId = id;
    session.clear();
    cache.clear();
    maxSeq = -1;
    queue.clear();
    nextId = 1;
    acked = 0;
  }

  bool irq() const { return !queue.empty(); }

  uint32_t post(const char* type, int value, bool proactive) {
    vp::JsonValue ev = vp::JsonValue::makeObject();
    ev.set("nano_boot_id", bootId);
    ev.set("event_id", static_cast<unsigned long>(nextId));
    ev.set("event_type", type);
    ev.set("uptime_ms", 1000);
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("value", value);
    ev.set("data", d);
    std::string json = vp::jsonSerialize(ev);
    queue.emplace_back(nextId, json);
    if (queue.size() > 32) queue.pop_front();
    if (proactive && !dropEvents && !session.empty()) {
      std::string f;
      vp::encodeFrameRaw(vp::kKindEvent, vp::kOpEvent, 0, json, f);
      *toAx += f;
    }
    return nextId++;
  }

  void feed(const std::string& bytes) {
    if (silent) return;
    dec.feed(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size(), [this](const vp::Frame& f) { handle(f); });
  }

 private:
  void reply(const vp::Frame& req, const std::string& sid, bool ok, const char* code, const vp::JsonValue& data,
             bool cacheIt) {
    vp::JsonValue p = vp::JsonValue::makeObject();
    p.set("session_id", sid);
    p.set("ok", ok);
    p.set("code", code);
    if (!data.isNull()) p.set("data", data);
    std::string f;
    vp::encodeFrame(vp::kKindResponse, req.opcode, req.seq, p, f);
    if (cacheIt) cache[req.seq] = std::make_pair(req.payloadJson, f);
    if (dropResponses > 0) {
      dropResponses--;
      return;
    }
    *toAx += f;
  }

  void handle(const vp::Frame& req) {
    std::string sid;
    req.payload.getString("session_id", sid);
    if (req.opcode == vp::kOpHello) {
      auto it = cache.find(req.seq);
      if (it != cache.end() && it->second.first == req.payloadJson && sid == session) {
        *toAx += it->second.second;
        return;
      }
      session = sid;
      cache.clear();
      maxSeq = req.seq;
      vp::JsonValue d = vp::JsonValue::makeObject();
      d.set("nano_boot_id", bootId);
      vp::JsonValue cur = vp::JsonValue::makeObject();
      cur.set("acked_through", static_cast<unsigned long>(acked));
      d.set("event_cursor", cur);
      reply(req, sid, true, "OK", d, true);
      return;
    }
    if (session.empty() || sid != session) {
      reply(req, sid, false, "BAD_SESSION", vp::JsonValue(), false);
      return;
    }
    auto it = cache.find(req.seq);
    if (it != cache.end()) {
      if (it->second.first == req.payloadJson) {
        *toAx += it->second.second;
      } else {
        reply(req, sid, false, "SEQ_CONFLICT", vp::JsonValue(), false);
      }
      return;
    }
    if (req.seq <= maxSeq) {
      reply(req, sid, false, "SEQ_CONFLICT", vp::JsonValue(), false);
      return;
    }
    maxSeq = req.seq;
    vp::JsonValue d = vp::JsonValue::makeObject();
    switch (req.opcode) {
      case vp::kOpGetStatus:
        d.set("uptime_ms", 1);
        reply(req, sid, true, "OK", d, true);
        break;
      case vp::kOpPlayVoice:
        plays++;
        d.set("state", "QUEUED");
        reply(req, sid, true, "OK", d, true);
        break;
      case vp::kOpDrainEvents: {
        int64_t after = 0, limit = 0;
        req.payload.getInt("after_event_id", after);
        req.payload.getInt("limit", limit);
        vp::JsonValue evs = vp::JsonValue::makeArray();
        bool more = false;
        uint32_t next = static_cast<uint32_t>(after);
        for (const auto& e : queue) {
          if (e.first <= after) continue;
          if (static_cast<int64_t>(evs.size()) >= limit) {
            more = true;
            break;
          }
          vp::JsonValue v;
          vp::jsonParse(e.second, v);
          evs.push(v);
          next = e.first;
        }
        d.set("events", evs);
        d.set("next_after", static_cast<unsigned long>(next));
        d.set("more", more);
        reply(req, sid, true, "OK", d, true);
        break;
      }
      case vp::kOpAckEvents: {
        int64_t through = 0;
        std::string boot;
        req.payload.getInt("through_event_id", through);
        req.payload.getString("nano_boot_id", boot);
        if (boot != bootId || through >= nextId) {
          reply(req, sid, false, "BAD_ARGUMENT", vp::JsonValue(), true);
          break;
        }
        while (!queue.empty() && queue.front().first <= through) queue.pop_front();
        if (through > acked) acked = static_cast<uint32_t>(through);
        reply(req, sid, true, "OK", d, true);
        break;
      }
      default:
        reply(req, sid, false, "UNSUPPORTED", vp::JsonValue(), true);
    }
  }
};

struct Rig {
  std::string toNano, toAx;
  Wire axEnd;
  FakeNano nano;
  NanoLink* link = nullptr;
  int64_t now = 0;
  std::vector<uint32_t> seen;
  std::vector<std::string> seenBoots;
  int onlineCount = 0, offlineCount = 0, resets = 0;
  bool lastNewBoot = false;

  explicit Rig(NanoLink::Config cfg = NanoLink::Config()) {
    axEnd.rx = &toAx;
    axEnd.tx = &toNano;
    nano.toAx = &toAx;
    link = new NanoLink(axEnd, cfg);
    link->setEventHandler([this](const NanoEvent& e) {
      seen.push_back(e.id);
      seenBoots.push_back(e.bootId);
    });
    link->setOnlineHandler([this](bool on, const std::string&, bool nb) {
      if (on) {
        onlineCount++;
        lastNewBoot = nb;
      } else {
        offlineCount++;
      }
    });
    link->setResetHook([this]() {
      resets++;
      nano.silent = false;
      nano.reboot("boot-after-reset");
    });
  }
  ~Rig() { delete link; }

  void run(int ms) {
    for (int t = 0; t < ms; t += 10) {
      now += 10;
      link->tick(now);
      std::string out;
      out.swap(toNano);
      nano.feed(out);
      link->setIrqLevel(nano.irq());
      link->onReadable(now);
    }
  }
};

void testHandshakeAndEvents() {
  Rig r;
  r.run(200);
  CHECK(r.link->online());
  CHECK(r.link->bootId() == "boot-1");
  for (int i = 0; i < 5; ++i) r.nano.post("NFC_TAG", i, true);
  r.run(300);
  CHECK(r.seen.size() == 5);
  for (size_t i = 0; i < r.seen.size(); ++i) CHECK(r.seen[i] == i + 1);
  CHECK(r.nano.queue.empty());  // all acknowledged
  CHECK(r.nano.acked == 5);
}

void testLostEventsAreDrained() {
  Rig r;
  r.run(200);
  r.nano.dropEvents = true;  // proactive frames lost, IRQ still high
  for (int i = 0; i < 12; ++i) r.nano.post("WIFI_STATE", i, true);
  r.nano.dropEvents = false;
  r.run(600);
  CHECK(r.seen.size() == 12);
  CHECK(r.nano.queue.empty());
  // Duplicate proactive copy of an acknowledged event is ignored.
  std::string dup;
  vp::JsonValue ev = vp::JsonValue::makeObject();
  ev.set("nano_boot_id", "boot-1");
  ev.set("event_id", 3);
  ev.set("event_type", "NFC_TAG");
  ev.set("uptime_ms", 1);
  ev.set("data", vp::JsonValue::makeObject());
  vp::encodeFrame(vp::kKindEvent, vp::kOpEvent, 0, ev, dup);
  r.toAx += dup;
  r.run(50);
  CHECK(r.seen.size() == 12);
  CHECK(r.link->stats().duplicates >= 1);
}

void testRetryKeepsSeq() {
  Rig r;
  r.run(200);
  int callbacks = 0;
  bool ok = false;
  r.nano.dropResponses = 1;
  r.link->playVoice("measure_done", 30, [&](const NanoReply& rep) {
    callbacks++;
    ok = rep.ok;
  });
  r.run(1000);
  CHECK(callbacks == 1);
  CHECK(ok);
  CHECK(r.nano.plays == 1);  // the retry hit the replay cache
  CHECK(r.link->stats().retries >= 1);
}

void testOfflineResetAndNewBoot() {
  NanoLink::Config cfg;
  cfg.offlineResetMs = 2000;
  cfg.resetSettleMs = 300;
  Rig r(cfg);
  r.run(200);
  CHECK(r.link->online());
  r.nano.post("BOOT", 0, true);
  r.run(200);
  CHECK(r.seen.size() == 1);
  r.nano.silent = true;
  int timeouts = 0;
  r.link->request(vp::kOpGetStatus, vp::JsonValue::makeObject(), [&](const NanoReply& rep) {
    if (rep.timeout) timeouts++;
  });
  r.run(6000);
  CHECK(timeouts == 1);
  CHECK(r.offlineCount >= 1);
  CHECK(r.resets == 1);
  CHECK(r.link->online());
  CHECK(r.link->bootId() == "boot-after-reset");
  CHECK(r.lastNewBoot);
  // Event ids restart at 1 for the new boot and are delivered again.
  r.nano.post("BOOT", 1, true);
  r.run(300);
  CHECK(!r.seen.empty() && r.seen.back() == 1 && r.seenBoots.back() == "boot-after-reset");
}

void testSilentRebootDetected() {
  Rig r;
  r.run(200);
  r.nano.reboot("boot-2");  // Nano restarted without the AX noticing
  int bad = 0;
  r.link->request(vp::kOpGetStatus, vp::JsonValue::makeObject(), [&](const NanoReply& rep) {
    if (rep.code == "BAD_SESSION") bad++;
  });
  r.run(500);
  CHECK(bad == 1);
  CHECK(r.link->online());
  CHECK(r.link->bootId() == "boot-2");
}

void testNanoQueueOverflowGap() {
  Rig r;
  r.run(200);
  r.nano.dropEvents = true;
  for (int i = 0; i < 40; ++i) r.nano.post("SD_STATE", i, true);  // queue keeps the newest 32
  r.nano.dropEvents = false;
  r.run(1000);
  CHECK(r.seen.size() == 32);
  CHECK(!r.seen.empty() && r.seen.front() == 9 && r.seen.back() == 40);
  CHECK(r.link->stats().gaps == 1);
  CHECK(r.nano.queue.empty());
}

}  // namespace

int main() {
  testHandshakeAndEvents();
  testLostEventsAreDrained();
  testRetryKeepsSeq();
  testOfflineResetAndNewBoot();
  testSilentRebootDetected();
  testNanoQueueOverflowGap();
  return testSummary("nano_link");
}
