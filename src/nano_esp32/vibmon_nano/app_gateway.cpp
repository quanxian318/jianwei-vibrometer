#include "app_gateway.h"

#include <stdlib.h>

AppGateway g_gateway;

namespace {

const char* const kTopics[] = {"status", "settings", "points", "records", "curves"};

AppGateway::Response reply(int status, const vp::JsonValue& v) {
  AppGateway::Response r;
  r.status = status;
  r.body = vp::jsonSerialize(v);
  return r;
}

AppGateway::Response okData(const vp::JsonValue& data) {
  vp::JsonValue v = vp::JsonValue::makeObject();
  v.set("ok", true);
  v.set("data", data);
  return reply(200, v);
}

AppGateway::Response error(int status, const char* code) {
  vp::JsonValue v = vp::JsonValue::makeObject();
  v.set("ok", false);
  v.set("error", code);
  return reply(status, v);
}

vp::JsonValue parse(const std::string& json) {
  vp::JsonValue v;
  if (!vp::jsonParse(json, v, nullptr, 12)) return vp::JsonValue();
  return v;
}

}  // namespace

void AppGateway::begin(const Hooks& hooks, const std::string& nanoVersion) {
  hooks_ = hooks;
  nanoVersion_ = nanoVersion;
}

bool AppGateway::store(const std::string& topic, const vp::JsonValue& data, uint32_t nowMs) {
  bool known = false;
  for (const char* t : kTopics) known = known || topic == t;
  if (!known || !data.isObject()) return false;
  Topic& t = topics_[topic];
  t.json = vp::jsonSerialize(data);
  t.atMs = nowMs;
  published_++;
  return true;
}

bool AppGateway::complete(int64_t cid, const vp::JsonValue& result) {
  auto it = commands_.find(cid);
  if (it == commands_.end()) return false;
  vp::JsonValue r = vp::JsonValue::makeObject();
  bool ok = false;
  result.getBool("ok", ok);
  r.set("ok", ok);
  if (const vp::JsonValue* e = result.find("error")) r.set("error", *e);
  if (const vp::JsonValue* d = result.find("data")) r.set("data", *d);
  it->second.done = true;
  it->second.resultJson = vp::jsonSerialize(r);
  return true;
}

void AppGateway::poll(uint32_t nowMs) {
  for (auto& c : commands_) {
    if (!c.second.done && nowMs - c.second.atMs > kCommandTimeoutMs) {
      c.second.done = true;
      c.second.resultJson = "{\"ok\":false,\"error\":\"timeout\"}";
      timeouts_++;
    }
  }
  // Answered commands are kept a while for the phone to read, then dropped.
  for (auto it = commands_.begin(); it != commands_.end();) {
    if (it->second.done && nowMs - it->second.atMs > 2 * kCommandTimeoutMs) {
      it = commands_.erase(it);
    } else {
      ++it;
    }
  }
}

void AppGateway::clear() { topics_.clear(); }

bool AppGateway::fresh(uint32_t nowMs) const {
  const Topic* s = topic("status");
  return s && hooks_.axOnline && hooks_.axOnline() && nowMs - s->atMs < kStaleMs;
}

const AppGateway::Topic* AppGateway::topic(const char* name) const {
  auto it = topics_.find(name);
  return it == topics_.end() ? nullptr : &it->second;
}

vp::JsonValue AppGateway::infoJson(uint32_t nowMs) const {
  vp::JsonValue o = vp::JsonValue::makeObject();
  o.set("service", "vibmon");
  o.set("api", 1);
  o.set("kind", "gateway");
  const Topic* s = topic("status");
  const vp::JsonValue st = s ? parse(s->json) : vp::JsonValue();
  std::string dev = "vibmon";
  std::string ver;
  st.getString("dev", dev);
  st.getString("ver", ver);
  o.set("device_id", dev);
  o.set("name", dev);
  o.set("version", ver);
  o.set("gateway_version", nanoVersion_);
  o.set("http_port", 80);
  o.set("pairing", false);
  o.set("ax_online", fresh(nowMs));
  return o;
}

void AppGateway::noteClient(const std::string& client, uint32_t nowMs) {
  if (client.empty()) return;
  auto it = clients_.find(client);
  const bool fresh = it == clients_.end() || nowMs - it->second > 600000;
  if (clients_.size() > 16 && it == clients_.end()) clients_.clear();
  clients_[client] = nowMs;
  if (fresh && hooks_.postEvent && hooks_.axOnline && hooks_.axOnline()) {
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("client", client);
    hooks_.postEvent("APP_CLIENT", d);
  }
}

AppGateway::Response AppGateway::command(const std::string& body, uint32_t nowMs) {
  vp::JsonValue req;
  if (!vp::jsonParse(body, req, nullptr, 6) || !req.isObject()) return error(400, "bad_json");
  std::string cmd;
  if (!req.getString("cmd", cmd) || cmd.empty() || cmd.size() > 32) return error(400, "bad_cmd");
  if (!hooks_.axOnline || !hooks_.axOnline()) return error(503, "ax_offline");
  vp::JsonValue args = vp::JsonValue::makeObject();
  for (const auto& m : req.members())
    if (m.key != "cmd") args.set(m.key, m.value);
  if (vp::jsonSerialize(args).size() > kMaxArgsBytes) return error(413, "args_too_large");
  size_t pending = 0;
  for (const auto& c : commands_) pending += c.second.done ? 0 : 1;
  if (pending >= kMaxCommands / 2) return error(429, "busy");
  if (commands_.size() >= kMaxCommands) commands_.erase(commands_.begin());
  const int64_t cid = nextCid_++;
  vp::JsonValue ev = vp::JsonValue::makeObject();
  ev.set("cid", static_cast<long long>(cid));
  ev.set("cmd", cmd);
  ev.set("args", args);
  if (!hooks_.postEvent || hooks_.postEvent("APP_COMMAND", ev) == 0) return error(500, "event_failed");
  Command& c = commands_[cid];
  c.atMs = nowMs;
  forwarded_++;
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("pending", true);
  d.set("cid", static_cast<long long>(cid));
  Response r = okData(d);
  r.status = 202;
  return r;
}

AppGateway::Response AppGateway::record(const std::string& id) const {
  // Only the newest measurement has curves on the gateway; older ones are
  // listed but their files stay on the AX (LAN connection).
  const Topic* c = topic("curves");
  const vp::JsonValue curves = c ? parse(c->json) : vp::JsonValue();
  std::string cid;
  if (!curves.getString("id", cid) || cid != id) return error(404, "lan_only");
  vp::JsonValue result = vp::JsonValue::makeObject();
  const Topic* r = topic("records");
  const vp::JsonValue recs = r ? parse(r->json) : vp::JsonValue();
  const vp::JsonValue* items = recs.find("items");
  if (items && items->isArray())
    for (const vp::JsonValue& it : items->items()) {
      std::string rid;
      if (it.getString("id", rid) && rid == id) result = it;
    }
  vp::JsonValue o = vp::JsonValue::makeObject();
  o.set("id", id);
  o.set("summary", result);
  o.set("partial", true);
  vp::JsonValue w = vp::JsonValue::makeObject();
  if (const vp::JsonValue* v = curves.find("wt")) w.set("t", *v);
  if (const vp::JsonValue* v = curves.find("wv")) w.set("v", *v);
  o.set("waveform", w);
  vp::JsonValue s = vp::JsonValue::makeObject();
  if (const vp::JsonValue* v = curves.find("sf")) s.set("f", *v);
  if (const vp::JsonValue* v = curves.find("sa")) s.set("a", *v);
  o.set("spectrum", s);
  return okData(o);
}

AppGateway::Response AppGateway::handle(const std::string& method, const std::string& path,
                                        const std::string& query, const std::string& body,
                                        const std::string& client, uint32_t nowMs) {
  (void)query;
  requests_++;
  if (path.compare(0, 8, "/api/v1/") != 0) return error(404, "not_found");
  const std::string route = path.substr(8);
  const bool get = method == "GET", post = method == "POST";
  if (route == "info" && get) return okData(infoJson(nowMs));
  noteClient(client, nowMs);
  if ((route == "live" || route == "status") && get) {
    const Topic* s = topic("status");
    if (!s) return error(503, hooks_.axOnline && hooks_.axOnline() ? "no_data" : "ax_offline");
    vp::JsonValue d = parse(s->json);
    vp::JsonValue gw = vp::JsonValue::makeObject();
    gw.set("age_ms", static_cast<unsigned long>(nowMs - s->atMs));
    gw.set("ax_online", fresh(nowMs));
    d.set("gw", gw);
    return okData(d);
  }
  if (get && (route == "settings" || route == "points" || route == "records")) {
    const Topic* t = topic(route.c_str());
    if (!t) return error(503, "no_data");
    vp::JsonValue d = parse(t->json);
    if (route == "settings") return okData(d);
    const vp::JsonValue* items = d.find("items");
    return okData(items ? *items : vp::JsonValue::makeArray());
  }
  if (get && route.compare(0, 8, "records/") == 0) return record(route.substr(8));
  if (post && route == "command") return command(body, nowMs);
  if (get && route.compare(0, 8, "command/") == 0) {
    const int64_t cid = atoll(route.c_str() + 8);
    auto it = commands_.find(cid);
    if (it == commands_.end()) return error(404, "unknown_command");
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("done", it->second.done);
    if (it->second.done) d.set("result", parse(it->second.resultJson));
    return okData(d);
  }
  if (get && route == "net") {
    vp::JsonValue d = vp::JsonValue::makeObject();
    if (hooks_.net) hooks_.net(d);
    return okData(d);
  }
  if (post && route == "net") {
    vp::JsonValue req;
    if (!vp::jsonParse(body, req, nullptr, 4) || !req.isObject()) return error(400, "bad_json");
    const std::string err = hooks_.configureNet ? hooks_.configureNet(req) : std::string("unsupported");
    if (!err.empty()) return error(400, err.c_str());
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("accepted", true);
    return okData(d);
  }
  if (get && route == "net/scan") {
    vp::JsonValue d = vp::JsonValue::makeObject();
    if (hooks_.scan) hooks_.scan(d);
    return okData(d);
  }
  if (route == "preview.bmp" || route.find("/files/") != std::string::npos) return error(404, "lan_only");
  return error(get || post ? 404 : 405, get || post ? "not_found" : "method");
}

void AppGateway::describe(vp::JsonValue& obj) const {
  obj.set("requests", static_cast<unsigned long>(requests_));
  obj.set("published", static_cast<unsigned long>(published_));
  obj.set("forwarded", static_cast<unsigned long>(forwarded_));
  obj.set("timeouts", static_cast<unsigned long>(timeouts_));
  obj.set("topics", static_cast<unsigned long>(topics_.size()));
  obj.set("clients", static_cast<unsigned long>(clients_.size()));
}
