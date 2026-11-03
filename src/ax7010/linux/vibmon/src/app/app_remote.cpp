// Phone app access: the LAN API (AppApi) and the Nano Wi-Fi gateway (protocol
// V1.1 PUBLISH / COMMAND_RESULT and the APP_* events), see
// 15_手机APP_20261025/README.md.
#include <algorithm>
#include <cmath>

#include "app/app.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "app";
// A published topic must fit one frame together with session_id and topic.
constexpr size_t kTopicBudget = 900;

// At most maxBytes, cut at a UTF-8 character boundary.
std::string utf8Prefix(const std::string& s, size_t maxBytes) {
  if (s.size() <= maxBytes) return s;
  size_t n = maxBytes;
  while (n > 0 && (static_cast<unsigned char>(s[n]) & 0xC0) == 0x80) --n;
  return s.substr(0, n);
}

double r4(double v) {
  if (!std::isfinite(v)) return 0;
  char b[24];
  snprintf(b, sizeof(b), "%.4g", v);
  return atof(b);
}

JsonValue nullable(bool valid, double v) { return valid ? JsonValue(r4(v)) : JsonValue(); }
}  // namespace

void Application::startAppApi() {
  if (!cfg_.appApi.enabled) return;
  AppApiHost h;
  h.command = [this](const JsonValue& c) { return handleCommand(c); };
  h.live = [this](bool wave) { return liveJson(wave); };
  h.preview = [this]() -> std::shared_ptr<const PreviewImage> {
    return engine_ ? engine_->live().preview : nullptr;
  };
  h.message = [this](const std::string& m) { setMessage(m); };
  h.points = [this]() {
    JsonValue a = JsonValue::makeArray();
    for (const auto& p : cfg_.points) {
      JsonValue o = JsonValue::makeObject();
      o.set("uid", p.first);
      o.set("id", p.second.id);
      o.set("name", p.second.name);
      a.push(o);
    }
    return a;
  };
  h.recordsDir = cfg_.recordsDir;
  h.deviceId = cfg_.deviceId;
  h.version = std::string("vibmon ") + VIBMON_APP_VERSION;
  appApi_.reset(new AppApi(loop_, cfg_.appApi, h));
  if (!appApi_->start()) {
    LOGW(kTag, "app API not started");
    appApi_.reset();
  }
  loop_.addTimer(500, [this]() {
    if (appApi_) appApi_->tick(monoMs());
    publishToNano(false);
  });
}

JsonValue Application::liveJson(bool wave) {
  const SystemState s = state_.snapshot();
  JsonValue o = JsonValue::makeObject();
  o.set("dev", s.deviceId);
  o.set("ver", s.appVersion);
  o.set("ms", s.measureState);
  o.set("el", r4(s.measureElapsedS));
  o.set("dur", r4(s.measureDurationS > 0 ? s.measureDurationS : s.settings.durationS));
  o.set("cam", s.cameraOk);
  o.set("method", s.method);
  if (s.method == "multipoint") o.set("bl", s.baselineValid);
  // Measuring box: centre (normalised) and size (pixels of the camera frame).
  JsonValue roi = JsonValue::makeArray();
  roi.push(r4(s.roiCx));
  roi.push(r4(s.roiCy));
  roi.push(s.settings.roiW);
  roi.push(s.settings.roiH);
  o.set("roi", roi);
  o.set("cal", s.calibrated);
  o.set("unit", s.calibrated ? "mm" : "px");
  o.set("lv", levelCode(s.alarmLevel));
  o.set("mute", s.alarmMuted);
  JsonValue l = JsonValue::makeObject();
  l.set("on", s.lightOn);
  l.set("pct", r4(s.lightPercent));
  l.set("ok", s.lightAllowed);
  o.set("light", l);
  o.set("laser", s.laserOn);
  JsonValue pt = JsonValue::makeObject();
  pt.set("id", s.pointId);
  pt.set("name", utf8Prefix(s.pointName, 48));
  pt.set("uid", s.pointUid);
  o.set("pt", pt);
  JsonValue p = JsonValue::makeObject();
  p.set("v", r4(s.power.busV));
  p.set("a", r4(s.power.currentA));
  p.set("w", r4(s.power.powerW));
  p.set("warn", s.powerWarn || s.powerDeny || s.overCurrentLatched);
  p.set("pf", s.pfail);
  o.set("pwr", p);
  o.set("tc", nullable(s.envValid, s.tempC));
  o.set("rh", nullable(s.envValid, s.rhPercent));
  o.set("dist", s.distanceValid ? JsonValue(s.distanceMm) : JsonValue());
  o.set("tach", nullable(s.tachValid, s.tachHz));
  o.set("fan", s.fanOn);
  o.set("prn", s.printerState);
  o.set("msg", utf8Prefix(s.message, 150));
  o.set("tv", s.timeValid);
  o.set("utc", static_cast<long long>(realtimeMs()));
  JsonValue n = JsonValue::makeObject();
  n.set("on", s.nanoOnline);
  n.set("wifi", s.wifiState);
  n.set("ip", s.wifiIp);
  n.set("sd", s.sdMounted);
  n.set("nfc", s.nfcReady);
  o.set("nano", n);
  JsonValue st = JsonValue::makeObject();
  st.set("on", stereo_ != nullptr);
  if (stereo_) {
    const StereoService::Stats ss = stereo_->stats();
    st.set("be", ss.backend);
    st.set("vf", r4(ss.lastValid));
  }
  o.set("st", st);
  o.set("ac", s.acousticState);
  JsonValue set = JsonValue::makeObject();
  set.set("dur", r4(s.settings.durationS));
  set.set("warn", r4(s.settings.warnPp));
  set.set("alarm", r4(s.settings.alarmPp));
  set.set("axis", std::string(1, s.settings.axis));
  o.set("set", set);
  if (s.last.have) {
    JsonValue r = JsonValue::makeObject();
    r.set("id", s.last.measurementId);
    r.set("ok", s.last.valid);
    r.set("why", utf8Prefix(s.last.reason, 60));
    r.set("u", s.last.unit);
    r.set("hz", r4(s.last.dominantHz));
    r.set("clr", s.last.peakClear);
    const bool amplitudeOk = s.last.valid && (s.last.method != "multipoint" || s.last.waveformValid);
    r.set("amp", amplitudeOk);
    r.set("pp", nullable(amplitudeOk, s.last.peakToPeak));
    r.set("rms", nullable(amplitudeOk, s.last.rms));
    r.set("lv", levelCode(s.last.level));
    r.set("saved", s.last.saved);
    if (s.last.acousticEnabled) r.set("ac_hz", nullable(s.last.acousticValid && s.last.acousticHz > 0, s.last.acousticHz));
    o.set("last", r);
  }
  if (engine_) {
    const MeasurementEngine::Live live = engine_->live();
    o.set("fps", r4(live.fps));
    o.set("fw", live.frameW);
    o.set("fh", live.frameH);
    if (live.mpWindows > 0) {
      JsonValue mp = JsonValue::makeObject();
      mp.set("win", live.mpWindows);
      mp.set("hz", nullable(live.mpWindow.status == 1, live.mpWindow.frequencyHz));
      o.set("mp", mp);
    }
    if (wave) {
      // Recent tracker output for the live chart, in pixels (mm_per_px converts).
      JsonValue w = JsonValue::makeArray();
      const size_t n0 = live.recent.size() > 300 ? live.recent.size() - 300 : 0;
      for (size_t i = n0; i < live.recent.size(); ++i) w.push(r4(live.recent[i]));
      o.set("wave", w);
      o.set("mmpx", cfg_.measure.mmPerPx > 0 ? JsonValue(cfg_.measure.mmPerPx) : JsonValue());
    }
  }
  return o;
}

void Application::publishTopic(const char* topic, const JsonValue& data) {
  JsonValue p = JsonValue::makeObject();
  p.set("topic", topic);
  p.set("data", data);
  nano_->request(vp::kOpPublish, p, [topic](const NanoReply& r) {
    if (!r.ok && !r.timeout) LOGD(kTag, "publish %s: %s", topic, r.code.c_str());
  });
}

void Application::publishToNano(bool force) {
  if (!cfg_.gateway.enabled || !nano_ || !nano_->online()) return;
  const JsonValue* caps = nano_->helloData().find("capabilities");
  bool capable = false;
  if (caps && caps->isArray())
    for (const JsonValue& c : caps->items())
      if (c.isString() && c.asString() == "app_gateway") capable = true;
  if (!capable) return;
  if (!gatewayCapable_ || nano_->sessionId() != publishedSession_) {
    // New session (or Nano boot): the gateway's cache may be empty.
    gatewayCapable_ = true;
    publishedSession_ = nano_->sessionId();
    publishedResultId_.clear();
    publishedSettings_.clear();
    force = true;
  }
  const int64_t now = monoMs();
  const SystemState s = state_.snapshot();
  // Publish at the period, and at once when something the app shows changes.
  std::string sig = s.measureState + "|" + levelCode(s.alarmLevel) + "|" +
                    s.pointId + "|" + s.last.measurementId + "|" + (s.lightOn ? "L" : "l") + (s.laserOn ? "Z" : "z") +
                    (s.alarmMuted ? "M" : "m") + "|" + s.message + "|" + s.printerState + "|" + s.wifiState + (s.sdMounted ? "S" : "s");
  if (force || sig != lastLiveSig_ || now >= nextStatusPublishMs_) {
    JsonValue live = liveJson(false);
    if (vp::jsonSerialize(live).size() > kTopicBudget) live.set("msg", "");
    publishTopic("status", live);
    lastLiveSig_ = sig;
    nextStatusPublishMs_ = now + cfg_.gateway.statusPeriodMs;
  }
  const std::string settings = vp::jsonSerialize(settingsJson());
  if (settings != publishedSettings_) {
    publishTopic("settings", settingsJson());
    JsonValue pts = JsonValue::makeArray();
    size_t bytes = 0;
    for (const auto& p : cfg_.points) {
      JsonValue o = JsonValue::makeObject();
      o.set("uid", p.first);
      o.set("id", p.second.id);
      o.set("name", utf8Prefix(p.second.name, 40));
      bytes += vp::jsonSerialize(o).size() + 1;
      if (bytes > kTopicBudget - 40) break;
      pts.push(o);
    }
    JsonValue pd = JsonValue::makeObject();
    pd.set("items", pts);
    publishTopic("points", pd);
    publishedSettings_ = settings;
  }
  if (s.last.have && s.last.measurementId != publishedResultId_) {
    publishedResultId_ = s.last.measurementId;
    // Recent records, as many as fit.
    JsonValue list = appRecordList(cfg_.recordsDir, 8, true);
    JsonValue items = JsonValue::makeArray();
    size_t bytes = 0;
    for (const JsonValue& r : list.items()) {
      bytes += vp::jsonSerialize(r).size() + 1;
      if (bytes > kTopicBudget - 40) break;
      items.push(r);
    }
    JsonValue rd = JsonValue::makeObject();
    rd.set("items", items);
    publishTopic("records", rd);
    // Curves of the new result, decimated until they fit one frame.
    if (s.last.saved) {
      for (size_t pts = 48; pts >= 12; pts -= 6) {
        JsonValue d;
        if (!appRecordDetail(cfg_.recordsDir, s.last.measurementId, pts, d)) break;
        JsonValue c = JsonValue::makeObject();
        c.set("id", s.last.measurementId);
        c.set("u", s.last.unit);
        auto round = [](const JsonValue& a) {
          JsonValue o = JsonValue::makeArray();
          if (a.isArray())
            for (const JsonValue& v : a.items()) o.push(r4(v.asDouble()));
          return o;
        };
        c.set("wt", round(jObj(jObj(d, "waveform"), "t")));
        c.set("wv", round(jObj(jObj(d, "waveform"), "v")));
        c.set("sf", round(jObj(jObj(d, "spectrum"), "f")));
        c.set("sa", round(jObj(jObj(d, "spectrum"), "a")));
        if (vp::jsonSerialize(c).size() <= kTopicBudget) {
          publishTopic("curves", c);
          break;
        }
      }
    }
  }
}

void Application::commandResult(int64_t cid, const JsonValue& reply) {
  JsonValue p = JsonValue::makeObject();
  p.set("cid", static_cast<long long>(cid));
  p.set("ok", jBool(reply, "ok", false));
  if (const JsonValue* e = reply.find("error")) p.set("error", *e);
  if (const JsonValue* d = reply.find("data"))
    if (vp::jsonSerialize(*d).size() <= 600) p.set("data", *d);
  nano_->request(vp::kOpCommandResult, p, nullptr);
}

void Application::onAppEvent(const NanoEvent& e) {
  int64_t cid = 0;
  e.data.getInt("cid", cid);
  if (e.type == "APP_CLIENT") {  // a phone started using the gateway
    setMessage("手机APP已连接 " + utf8Prefix(jStr(e.data, "client", ""), 32));
    return;
  }
  // APP_COMMAND: {"cid": n, "cmd": "...", "args": {...}}
  const std::string cmd = jStr(e.data, "cmd", "");
  JsonValue c = JsonValue::makeObject();
  const JsonValue& args = jObj(e.data, "args");
  if (args.isObject())
    for (const auto& m : args.members())
      if (m.key != "cmd") c.set(m.key, m.value);
  c.set("cmd", cmd);
  JsonValue reply;
  if (!appCommandAllowed(cmd, true)) {
    reply = JsonValue::makeObject();
    reply.set("ok", false);
    reply.set("error", "command_not_allowed");
  } else {
    reply = handleCommand(c);
  }
  LOGI(kTag, "app command %s via gateway: %s", cmd.c_str(), jBool(reply, "ok", false) ? "ok" : "failed");
  commandResult(cid, reply);
  publishToNano(true);
}

}  // namespace vm
