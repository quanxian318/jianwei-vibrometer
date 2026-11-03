#include "router.h"

#include "app_config.h"
#include "app_gateway.h"
#include "audio_service.h"
#include "esp_timer.h"
#include "event_store.h"
#include "link.h"
#include "log.h"
#include "net_service.h"
#include "nfc_service.h"
#include "power_service.h"
#include "sd_service.h"
#include "settings.h"

namespace router {
namespace {

const char* kTag = "router";

struct CacheEntry {
  bool used = false;
  uint16_t seq = 0;
  uint8_t opcode = 0;
  uint32_t hash = 0;
  uint32_t length = 0;
  std::string response;  // complete encoded response frame
};

CacheEntry g_cache[cfg::kSeqCacheSize];
size_t g_cacheNext = 0;
std::string g_bootId;
std::string g_session;
bool g_sessionActive = false;
int32_t g_maxSeq = -1;

struct Counters {
  uint32_t requests = 0;
  uint32_t replays = 0;
  uint32_t badSession = 0;
  uint32_t seqConflicts = 0;
  uint32_t unexpectedKind = 0;
} g_counters;

struct Result {
  bool ok = true;
  const char* code = "OK";
  vp::JsonValue data;  // null = omitted
};

Result fail(const char* code, const char* reason = nullptr) {
  Result r;
  r.ok = false;
  r.code = code;
  if (reason) {
    r.data = vp::JsonValue::makeObject();
    r.data.set("reason", reason);
  }
  return r;
}

uint32_t fnv1a(const std::string& s) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < s.size(); ++i) {
    h ^= static_cast<uint8_t>(s[i]);
    h *= 16777619u;
  }
  return h;
}

CacheEntry* cacheFind(uint16_t seq) {
  for (size_t i = 0; i < cfg::kSeqCacheSize; ++i) {
    if (g_cache[i].used && g_cache[i].seq == seq) return &g_cache[i];
  }
  return nullptr;
}

void cacheStore(const vp::Frame& req, const std::string& response) {
  CacheEntry& e = g_cache[g_cacheNext];
  g_cacheNext = (g_cacheNext + 1) % cfg::kSeqCacheSize;
  e.used = true;
  e.seq = req.seq;
  e.opcode = req.opcode;
  e.hash = fnv1a(req.payloadJson);
  e.length = static_cast<uint32_t>(req.payloadJson.size());
  e.response = response;
}

void cacheClear() {
  for (size_t i = 0; i < cfg::kSeqCacheSize; ++i) {
    g_cache[i].used = false;
    g_cache[i].response.clear();
  }
  g_cacheNext = 0;
}

bool sameRequest(const CacheEntry& e, const vp::Frame& req) {
  return e.opcode == req.opcode && e.length == req.payloadJson.size() && e.hash == fnv1a(req.payloadJson);
}

std::string buildResponse(const vp::Frame& req, const std::string& sid, const Result& r) {
  vp::JsonValue p = vp::JsonValue::makeObject();
  p.set("session_id", sid);
  p.set("ok", r.ok);
  p.set("code", r.code);
  if (!r.data.isNull()) p.set("data", r.data);
  std::string frame;
  if (!vp::encodeFrame(vp::kKindResponse, req.opcode, req.seq, p, frame)) {
    vp::JsonValue small = vp::JsonValue::makeObject();
    small.set("session_id", sid);
    small.set("ok", false);
    small.set("code", "INTERNAL");
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("reason", "response_too_large");
    small.set("data", d);
    vp::encodeFrame(vp::kKindResponse, req.opcode, req.seq, small, frame);
  }
  return frame;
}

bool getString(const vp::JsonValue& p, const char* key, std::string& out, size_t minLen, size_t maxLen) {
  if (!p.getString(key, out)) return false;
  return out.size() >= minLen && out.size() <= maxLen;
}

bool getRange(const vp::JsonValue& p, const char* key, int64_t& out, int64_t lo, int64_t hi) {
  if (!p.getInt(key, out)) return false;
  return out >= lo && out <= hi;
}

vp::JsonValue jobData(const AudioJob& job) {
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("job_id", job.jobId);
  d.set("asset_id", job.assetId);
  d.set("state", jobStateName(job.state));
  if (!job.reason.empty()) d.set("reason", job.reason);
  if (job.state != JobState::kQueued && job.state != JobState::kPlaying) {
    d.set("played_ms", static_cast<unsigned long>(job.playedMs));
    d.set("underruns", static_cast<unsigned long>(job.underruns));
  }
  return d;
}

// ---- command handlers -------------------------------------------------------

Result handleHello(const vp::Frame& req, std::string& sid) {
  const vp::JsonValue& p = req.payload;
  std::string axVersion;
  int64_t protocol = 0;
  if (!getString(p, "session_id", sid, 1, cfg::kMaxIdLength)) return fail("BAD_ARGUMENT", "session_id");
  if (!p.getInt("protocol", protocol)) return fail("BAD_ARGUMENT", "protocol");
  if (!getString(p, "ax_version", axVersion, 0, cfg::kMaxIdLength)) return fail("BAD_ARGUMENT", "ax_version");
  if (protocol != 1) {
    Result r = fail("UNSUPPORTED", "protocol");
    vp::JsonValue supported = vp::JsonValue::makeArray();
    supported.push(1);
    r.data.set("supported", supported);
    return r;
  }
  // A handshake always starts a fresh session: no job of the old one continues.
  g_audio.resetSession();
  g_power.clearPrepared();
  g_gateway.clear();
  cacheClear();
  g_session = sid;
  g_sessionActive = true;
  g_maxSeq = req.seq;
  LOGI(kTag, "session %s (ax %s)", sid.c_str(), axVersion.c_str());

  Result r;
  r.data = vp::JsonValue::makeObject();
  r.data.set("nano_boot_id", g_bootId);
  r.data.set("nano_version", VIBMON_NANO_VERSION);
  r.data.set("protocol", 1);
  r.data.set("profile", VIBMON_NANO_PROFILE);
  vp::JsonValue caps = vp::JsonValue::makeArray();
  caps.push("nfc");
  caps.push("sd");
  caps.push("audio");
  caps.push("wifi");
  caps.push("ntp");
  caps.push("power_fail");
  caps.push("app_gateway");
  r.data.set("capabilities", caps);
  vp::JsonValue cursor = vp::JsonValue::makeObject();
  g_events.describe(cursor);
  r.data.set("event_cursor", cursor);
  vp::JsonValue link = vp::JsonValue::makeObject();
  link.set("baud", static_cast<unsigned long>(g_link.baud()));
  link.set("flow_control", g_link.flowControl());
  r.data.set("link", link);
  return r;
}

Result handleGetStatus() {
  Result r;
  r.data = vp::JsonValue::makeObject();
  r.data.set("uptime_ms", static_cast<long long>(esp_timer_get_time() / 1000));
  vp::JsonValue wifi = vp::JsonValue::makeObject();
  g_net.describe(wifi);
  r.data.set("wifi", wifi);
  r.data.set("time_valid", g_net.timeValid());
  vp::JsonValue sd = vp::JsonValue::makeObject();
  g_sd.describeShort(sd);
  r.data.set("sd", sd);
  vp::JsonValue nfc = vp::JsonValue::makeObject();
  g_nfc.describe(nfc);
  r.data.set("nfc", nfc);
  AudioJob job;
  r.data.set("audio_job", g_audio.current(job) ? jobData(job) : vp::JsonValue());
  r.data.set("audio_ready", g_audio.driverOk());
  r.data.set("power_fail", g_power.failActive());
  r.data.set("power_off_prepared", g_power.powerOffPrepared());
  r.data.set("queue_overflow", static_cast<unsigned long>(g_events.overflowCount()));
  vp::JsonValue ev = vp::JsonValue::makeObject();
  g_events.describe(ev);
  r.data.set("events", ev);
  vp::JsonValue link = vp::JsonValue::makeObject();
  g_link.describe(link);
  r.data.set("link", link);
  vp::JsonValue gw = vp::JsonValue::makeObject();
  g_gateway.describe(gw);
  r.data.set("gateway", gw);
  return r;
}

Result handleSetNet(const vp::Frame& req) {
  const vp::JsonValue& p = req.payload;
  std::string ssid, password;
  bool persist = false;
  if (!getString(p, "ssid", ssid, 0, 32)) return fail("BAD_ARGUMENT", "ssid");
  if (!getString(p, "password", password, 0, 63)) return fail("BAD_ARGUMENT", "password");
  if (!password.empty() && password.size() < 8) return fail("BAD_ARGUMENT", "password");
  if (!p.getBool("persist", persist)) return fail("BAD_ARGUMENT", "persist");
  if (persist && g_power.restricted()) return fail("POWER_FAIL");
  g_net.configure(ssid, password);
  bool saved = false;
  if (persist) {
    saved = g_settings.saveWifi(ssid, password);
    if (!saved) return fail("INTERNAL", "nvs_write");
  }
  LOGI(kTag, "network set to \"%s\"%s", ssid.c_str(), persist ? " (saved)" : "");
  Result r;
  r.data = vp::JsonValue::makeObject();
  r.data.set("accepted", true);
  r.data.set("persisted", saved);
  return r;
}

Result handleGetTime() {
  Result r;
  r.data = vp::JsonValue::makeObject();
  g_net.timeInfo(r.data);
  return r;
}

Result handlePlayVoice(const vp::Frame& req) {
  const vp::JsonValue& p = req.payload;
  std::string jobId, assetId;
  int64_t volume = 0;
  if (!getString(p, "job_id", jobId, 1, cfg::kMaxIdLength)) return fail("BAD_ARGUMENT", "job_id");
  if (!getString(p, "asset_id", assetId, 1, cfg::kMaxIdLength)) return fail("BAD_ARGUMENT", "asset_id");
  if (!getRange(p, "volume", volume, 0, 100)) return fail("BAD_ARGUMENT", "volume");
  AudioJob job;
  if (g_audio.find(jobId, job)) {
    // Same job_id again: report its state, never play it twice.
    Result r;
    r.data = jobData(job);
    r.data.set("duplicate", true);
    return r;
  }
  if (g_power.restricted()) return fail("POWER_FAIL");
  const VoiceAsset* asset = findVoiceAsset(assetId.c_str());
  if (asset == nullptr) return fail("NO_ASSET", "unknown_asset");
  if (!g_sd.mounted()) return fail("NO_SD");
  if (!g_sd.exists(asset->path)) return fail("NO_ASSET", "file_missing");
  switch (g_audio.enqueue(jobId, assetId, asset->path, static_cast<int>(volume), job)) {
    case EnqueueResult::kQueued:
    case EnqueueResult::kDuplicate: {
      Result r;
      r.data = jobData(job);
      return r;
    }
    case EnqueueResult::kBusy:
      return fail("BUSY", "queue_full");
    case EnqueueResult::kNoDriver:
      return fail("INTERNAL", "i2s_unavailable");
  }
  return fail("INTERNAL");
}

Result handleStopVoice(const vp::Frame& req) {
  std::string jobId;
  if (!getString(req.payload, "job_id", jobId, 1, cfg::kMaxIdLength)) return fail("BAD_ARGUMENT", "job_id");
  AudioJob job;
  if (!g_audio.stop(jobId, job)) return fail("BAD_ARGUMENT", "unknown_job");
  Result r;
  r.data = jobData(job);
  return r;
}

Result handleGetSdInfo() {
  Result r;
  r.data = vp::JsonValue::makeObject();
  g_sd.describe(r.data);
  return r;
}

std::string handleDrain(const vp::Frame& req, bool& handled) {
  handled = false;
  int64_t after = 0, limit = 0;
  if (!getRange(req.payload, "after_event_id", after, 0, 0xFFFFFFFFll) ||
      !getRange(req.payload, "limit", limit, 1, 8)) {
    return std::string();
  }
  // Built as text so stored event objects are embedded byte for byte.
  std::string prefix = "{\"session_id\":";
  vp::jsonAppendQuoted(g_session, prefix);
  prefix += ",\"ok\":true,\"code\":\"OK\",\"data\":{\"events\":[";
  const size_t suffixMax = 48;  // ],"next_after":4294967295,"more":false}}
  std::vector<std::string> events;
  uint32_t nextAfter = 0;
  bool more = false;
  const size_t budget = vp::kMaxPayload - prefix.size() - suffixMax;
  g_events.collect(static_cast<uint32_t>(after), static_cast<size_t>(limit), budget, events, nextAfter, more);
  std::string body = prefix;
  for (size_t i = 0; i < events.size(); ++i) {
    if (i) body += ',';
    body += events[i];
  }
  char tail[64];
  snprintf(tail, sizeof(tail), "],\"next_after\":%lu,\"more\":%s}}", static_cast<unsigned long>(nextAfter),
           more ? "true" : "false");
  body += tail;
  std::string frame;
  if (!vp::encodeFrameRaw(vp::kKindResponse, req.opcode, req.seq, body, frame)) return std::string();
  handled = true;
  return frame;
}

Result handleAck(const vp::Frame& req) {
  std::string boot;
  int64_t through = 0;
  if (!getString(req.payload, "nano_boot_id", boot, 1, cfg::kMaxIdLength)) return fail("BAD_ARGUMENT", "nano_boot_id");
  if (!getRange(req.payload, "through_event_id", through, 0, 0xFFFFFFFFll)) {
    return fail("BAD_ARGUMENT", "through_event_id");
  }
  if (boot != g_bootId) {
    Result r = fail("BAD_ARGUMENT", "boot_mismatch");
    r.data.set("nano_boot_id", g_bootId);
    return r;
  }
  if (!g_events.ack(static_cast<uint32_t>(through))) {
    Result r = fail("BAD_ARGUMENT", "beyond_last_event");
    r.data.set("last_event_id", static_cast<unsigned long>(g_events.lastId()));
    return r;
  }
  Result r;
  r.data = vp::JsonValue::makeObject();
  r.data.set("acked_through", static_cast<unsigned long>(g_events.ackedThrough()));
  r.data.set("pending", static_cast<unsigned long>(g_events.pendingCount()));
  return r;
}

Result handlePreparePowerOff(const vp::Frame& req) {
  std::string reason;
  const vp::JsonValue* v = req.payload.find("reason");
  if (v != nullptr && (!v->isString() || v->asString().size() > cfg::kMaxIdLength)) {
    return fail("BAD_ARGUMENT", "reason");
  }
  if (v != nullptr) reason = v->asString();
  g_power.preparePowerOff(reason);
  Result r;
  r.data = vp::JsonValue::makeObject();
  r.data.set("prepared", true);
  return r;
}

// PUBLISH (0x60): {"topic", "data"} snapshot for the phone app gateway.
Result handlePublish(const vp::Frame& req) {
  std::string topic;
  const vp::JsonValue* data = req.payload.find("data");
  if (!getString(req.payload, "topic", topic, 1, 16)) return fail("BAD_ARGUMENT", "topic");
  if (data == nullptr || !data->isObject()) return fail("BAD_ARGUMENT", "data");
  if (!g_gateway.store(topic, *data, millis())) return fail("BAD_ARGUMENT", "unknown_topic");
  return Result();
}

// COMMAND_RESULT (0x62): {"cid", "ok", "error"?, "data"?} for an APP_COMMAND.
Result handleCommandResult(const vp::Frame& req) {
  int64_t cid = 0;
  if (!getRange(req.payload, "cid", cid, 1, 0x7FFFFFFF)) return fail("BAD_ARGUMENT", "cid");
  if (!g_gateway.complete(cid, req.payload)) return fail("BAD_ARGUMENT", "unknown_cid");
  return Result();
}

void reply(const vp::Frame& req, const std::string& sid, const Result& r, bool cache) {
  std::string frame = buildResponse(req, sid, r);
  if (cache) cacheStore(req, frame);
  g_link.send(frame);
}

}  // namespace

void begin(const std::string& bootId) { g_bootId = bootId; }

bool sessionActive() { return g_sessionActive; }

const std::string& sessionId() { return g_session; }

void onFrame(const vp::Frame& req) {
  if (req.kind != vp::kKindRequest) {
    g_counters.unexpectedKind++;
    return;
  }
  g_counters.requests++;
  std::string sid;
  req.payload.getString("session_id", sid);

  if (req.opcode == vp::kOpHello) {
    CacheEntry* e = cacheFind(req.seq);
    if (g_sessionActive && e != nullptr && sameRequest(*e, req) && sid == g_session) {
      g_counters.replays++;
      g_link.send(e->response);
      return;
    }
    std::string newSid;
    Result r = handleHello(req, newSid);
    reply(req, r.ok ? newSid : sid, r, r.ok);
    return;
  }

  if (!g_sessionActive || sid != g_session) {
    g_counters.badSession++;
    reply(req, sid, fail("BAD_SESSION"), false);
    return;
  }
  CacheEntry* e = cacheFind(req.seq);
  if (e != nullptr) {
    if (sameRequest(*e, req)) {
      g_counters.replays++;
      g_link.send(e->response);
    } else {
      g_counters.seqConflicts++;
      reply(req, sid, fail("SEQ_CONFLICT"), false);
    }
    return;
  }
  if (static_cast<int32_t>(req.seq) <= g_maxSeq) {
    // Seq already used in this session and no longer cached: never re-execute.
    g_counters.seqConflicts++;
    reply(req, sid, fail("SEQ_CONFLICT", "outside_cache_window"), false);
    return;
  }
  g_maxSeq = req.seq;

  switch (req.opcode) {
    case vp::kOpGetStatus: reply(req, sid, handleGetStatus(), true); break;
    case vp::kOpSetNet: reply(req, sid, handleSetNet(req), true); break;
    case vp::kOpGetTime: reply(req, sid, handleGetTime(), true); break;
    case vp::kOpPlayVoice: reply(req, sid, handlePlayVoice(req), true); break;
    case vp::kOpStopVoice: reply(req, sid, handleStopVoice(req), true); break;
    case vp::kOpGetSdInfo: reply(req, sid, handleGetSdInfo(), true); break;
    case vp::kOpDrainEvents: {
      bool handled = false;
      std::string frame = handleDrain(req, handled);
      if (!handled) {
        reply(req, sid, fail("BAD_ARGUMENT", "after_event_id/limit"), true);
      } else {
        cacheStore(req, frame);
        g_link.send(frame);
      }
      break;
    }
    case vp::kOpAckEvents: reply(req, sid, handleAck(req), true); break;
    case vp::kOpPreparePowerOff: reply(req, sid, handlePreparePowerOff(req), true); break;
    case vp::kOpPublish: reply(req, sid, handlePublish(req), true); break;
    case vp::kOpCommandResult: reply(req, sid, handleCommandResult(req), true); break;
    default: reply(req, sid, fail("UNSUPPORTED", "opcode"), true); break;
  }
}

void pumpEvents() {
  if (!g_sessionActive) return;
  while (g_link.txQueuedBytes() < 2048) {
    std::string json;
    uint32_t id = 0;
    if (!g_events.nextUnsent(json, id)) return;
    std::string frame;
    if (!vp::encodeFrameRaw(vp::kKindEvent, vp::kOpEvent, 0, json, frame)) {
      g_events.markSent(id);  // cannot happen for <= 384-byte events; skip rather than loop
      continue;
    }
    if (!g_link.send(frame)) return;
    g_events.markSent(id);
  }
}

void describe(vp::JsonValue& obj) {
  obj.set("session_active", g_sessionActive);
  obj.set("session_id", g_sessionActive ? vp::JsonValue(g_session) : vp::JsonValue());
  obj.set("requests", static_cast<unsigned long>(g_counters.requests));
  obj.set("replays", static_cast<unsigned long>(g_counters.replays));
  obj.set("bad_session", static_cast<unsigned long>(g_counters.badSession));
  obj.set("seq_conflicts", static_cast<unsigned long>(g_counters.seqConflicts));
}

}  // namespace router
