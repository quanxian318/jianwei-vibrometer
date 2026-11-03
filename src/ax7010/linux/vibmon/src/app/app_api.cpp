#include "app/app_api.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "app/storage.h"
#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "appapi";

std::string randomHex(int bytes) {
  std::vector<uint8_t> b(static_cast<size_t>(bytes));
  std::ifstream f("/dev/urandom", std::ios::binary);
  f.read(reinterpret_cast<char*>(b.data()), bytes);
  if (!f) {
    for (auto& x : b) x = static_cast<uint8_t>(rand());
  }
  static const char* hex = "0123456789abcdef";
  std::string s;
  for (uint8_t x : b) {
    s += hex[x >> 4];
    s += hex[x & 15];
  }
  return s;
}

// Six significant digits are plenty for a plot and keep the JSON small.
double r6(double v) {
  if (!std::isfinite(v)) return 0;
  char b[32];
  snprintf(b, sizeof(b), "%.6g", v);
  return atof(b);
}

HttpResponse json(int status, const JsonValue& v) {
  HttpResponse r;
  r.status = status;
  r.body = vp::jsonSerialize(v);
  return r;
}

HttpResponse error(int status, const std::string& e) {
  JsonValue v = JsonValue::makeObject();
  v.set("ok", false);
  v.set("error", e);
  return json(status, v);
}

JsonValue ok(const JsonValue& data = JsonValue()) {
  JsonValue v = JsonValue::makeObject();
  v.set("ok", true);
  if (!data.isNull()) v.set("data", data);
  return v;
}

// Reduces a series to at most maxPoints by keeping the minimum and maximum of
// each bucket (the envelope of a waveform, the peaks of a spectrum).
void decimate(const std::vector<double>& x, const std::vector<double>& y, size_t maxPoints, bool peaksOnly,
              JsonValue& xs, JsonValue& ys) {
  xs = JsonValue::makeArray();
  ys = JsonValue::makeArray();
  const size_t n = std::min(x.size(), y.size());
  if (n <= maxPoints || maxPoints < 4) {
    for (size_t i = 0; i < n; ++i) {
      xs.push(r6(x[i]));
      ys.push(r6(y[i]));
    }
    return;
  }
  const size_t buckets = peaksOnly ? maxPoints : maxPoints / 2;
  for (size_t b = 0; b < buckets; ++b) {
    const size_t a = b * n / buckets, e = std::max(a + 1, (b + 1) * n / buckets);
    size_t lo = a, hi = a;
    for (size_t i = a; i < e; ++i) {
      if (y[i] < y[lo]) lo = i;
      if (y[i] > y[hi]) hi = i;
    }
    if (peaksOnly) {
      xs.push(r6(x[hi]));
      ys.push(r6(y[hi]));
    } else {
      const size_t f = std::min(lo, hi), s = std::max(lo, hi);
      xs.push(r6(x[f]));
      ys.push(r6(y[f]));
      if (s != f) {
        xs.push(r6(x[s]));
        ys.push(r6(y[s]));
      }
    }
  }
}

std::string bearer(const HttpRequest& r) {
  const std::string a = r.header("authorization");
  if (a.compare(0, 7, "Bearer ") == 0) return a.substr(7);
  return r.param("token");  // <img src> cannot set headers
}
}  // namespace

const char* levelCode(AlarmLevel level) {
  switch (level) {
    case AlarmLevel::kNormal: return "normal";
    case AlarmLevel::kWarn: return "warn";
    case AlarmLevel::kAlarm: return "alarm";
    case AlarmLevel::kUnknown: break;
  }
  return "unknown";
}

bool appCommandAllowed(const std::string& cmd, bool viaGateway) {
  static const char* kBoth[] = {"measure_start", "baseline_start", "measure_stop", "light", "laser", "mute",
                                "set_point",     "print",          "voice",        "set",   "rtc_set", "zoom_reset"};
  for (const char* c : kBoth)
    if (cmd == c) return true;
  // Over the LAN the app also reads full state, moves the box and configures
  // the Nano's Wi-Fi; through the gateway those answers do not fit a frame.
  static const char* kDirect[] = {"status", "settings", "roi", "nano_status", "set_net", "shutdown", "stereo_distance"};
  if (!viaGateway)
    for (const char* c : kDirect)
      if (cmd == c) return true;
  return viaGateway && cmd == "shutdown";
}

bool safeName(const std::string& s) {
  if (s.empty() || s.size() > 96 || s[0] == '.') return false;
  for (char c : s)
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) return false;
  return s.find("..") == std::string::npos;
}

JsonValue appRecordList(const std::string& dir, size_t maxCount, bool compact) {
  JsonValue a = JsonValue::makeArray();
  for (const RecordSummary& r : listRecords(dir, maxCount)) {
    JsonValue o = JsonValue::makeObject();
    o.set("id", r.id);
    o.set("t", r.startLocal);
    o.set("pt", r.pointId);
    if (!compact) o.set("pn", r.pointName);
    o.set("ok", r.valid);
    if (!r.valid && !compact) o.set("why", r.reason);
    o.set("u", r.unit);
    o.set("hz", r6(r.dominantHz));
    o.set("clr", r.peakClear);
    o.set("pp", r.amplitudes ? JsonValue(r6(r.peakToPeak)) : JsonValue());
    o.set("rms", r.amplitudes ? JsonValue(r6(r.rms)) : JsonValue());
    o.set("lv", levelCode(r.level));
    a.push(o);
  }
  return a;
}

bool appRecordDetail(const std::string& dir, const std::string& id, size_t maxPoints, JsonValue& out) {
  JsonValue rec;
  std::vector<double> t, d, f, a;
  if (!safeName(id) || !loadRecord(dir, id, rec, t, d, f, a)) return false;
  out = JsonValue::makeObject();
  out.set("id", id);
  out.set("record", rec);
  JsonValue xs, ys;
  decimate(t, d, maxPoints, false, xs, ys);
  JsonValue w = JsonValue::makeObject();
  w.set("t", xs);
  w.set("v", ys);
  w.set("samples", static_cast<unsigned long>(t.size()));
  out.set("waveform", w);
  decimate(f, a, maxPoints, true, xs, ys);
  JsonValue s = JsonValue::makeObject();
  s.set("f", xs);
  s.set("a", ys);
  s.set("bins", static_cast<unsigned long>(f.size()));
  out.set("spectrum", s);
  std::vector<double> af, aa;
  loadAcousticSpectrum(dir, id, af, aa);
  if (!af.empty()) {
    decimate(af, aa, maxPoints, true, xs, ys);
    JsonValue ac = JsonValue::makeObject();
    ac.set("f", xs);
    ac.set("a", ys);
    out.set("acoustic_spectrum", ac);
  }
  return true;
}

std::string previewBmp(const PreviewImage& p, int maxWidth) {
  if (p.width <= 0 || p.height <= 0) return std::string();
  const bool color = p.bgr.size() == static_cast<size_t>(p.width) * p.height * 3;
  if (!color && p.gray.size() != static_cast<size_t>(p.width) * p.height) return std::string();
  const int step = std::max(1, (p.width + maxWidth - 1) / std::max(16, maxWidth));
  const int w = p.width / step, h = p.height / step;
  const int row = (w * 3 + 3) & ~3;
  const uint32_t size = 54 + static_cast<uint32_t>(row) * h;
  std::string b(size, '\0');
  auto put32 = [&b](size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = static_cast<char>((v >> (8 * i)) & 0xFF);
  };
  b[0] = 'B';
  b[1] = 'M';
  put32(2, size);
  put32(10, 54);
  put32(14, 40);
  put32(18, static_cast<uint32_t>(w));
  put32(22, static_cast<uint32_t>(h));
  b[26] = 1;
  b[28] = 24;
  put32(34, static_cast<uint32_t>(row) * h);
  for (int y = 0; y < h; ++y) {
    char* dst = &b[54 + static_cast<size_t>(h - 1 - y) * row];  // bottom-up
    for (int x = 0; x < w; ++x) {
      const size_t i = static_cast<size_t>(y * step) * p.width + x * step;
      if (color) {
        dst[3 * x] = static_cast<char>(p.bgr[3 * i]);
        dst[3 * x + 1] = static_cast<char>(p.bgr[3 * i + 1]);
        dst[3 * x + 2] = static_cast<char>(p.bgr[3 * i + 2]);
      } else {
        dst[3 * x] = dst[3 * x + 1] = dst[3 * x + 2] = static_cast<char>(p.gray[i]);
      }
    }
  }
  return b;
}

AppApi::AppApi(EventLoop& loop, const AppApiConfig& cfg, AppApiHost host)
    : loop_(loop), cfg_(cfg), host_(std::move(host)) {}

AppApi::~AppApi() {
  http_.reset();
  if (udpFd_ >= 0) {
    loop_.removeFd(udpFd_);
    ::close(udpFd_);
  }
}

bool AppApi::start() {
  loadTokens();
  http_.reset(new HttpServer(loop_, [this](const HttpRequest& r) { return handle(r); }));
  if (!http_->listen(cfg_.httpPort)) {
    http_.reset();
    return false;
  }
  if (cfg_.discoveryPort >= 0) {
    udpFd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    int one = 1;
    setsockopt(udpFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons(static_cast<uint16_t>(cfg_.discoveryPort));
    if (udpFd_ < 0 || bind(udpFd_, reinterpret_cast<sockaddr*>(&a), sizeof(a)) != 0) {
      LOGW(kTag, "discovery port %d: %s", cfg_.discoveryPort, strerror(errno));
      if (udpFd_ >= 0) ::close(udpFd_);
      udpFd_ = -1;
    } else {
      socklen_t len = sizeof(a);
      getsockname(udpFd_, reinterpret_cast<sockaddr*>(&a), &len);
      udpPort_ = ntohs(a.sin_port);
      loop_.addFd(udpFd_, EPOLLIN, [this](uint32_t) { onDiscovery(); });
    }
  }
  LOGI(kTag, "app API on http port %d, discovery udp %d, %zu paired", http_->port(), udpPort_, tokens_.size());
  return true;
}

void AppApi::tick(int64_t nowMs) {
  if (http_) http_->sweep(nowMs);
  if (!pin_.empty() && nowMs > pinUntilMs_) pin_.clear();
}

JsonValue AppApi::info() const {
  JsonValue o = JsonValue::makeObject();
  o.set("service", "vibmon");
  o.set("api", 1);
  o.set("kind", "ax7010");
  o.set("device_id", host_.deviceId);
  o.set("name", cfg_.name.empty() ? host_.deviceId : cfg_.name);
  o.set("version", host_.version);
  o.set("http_port", http_ ? http_->port() : cfg_.httpPort);
  o.set("pairing", cfg_.requirePairing);
  return o;
}

void AppApi::onDiscovery() {
  for (;;) {
    char buf[256];
    sockaddr_in from{};
    socklen_t len = sizeof(from);
    const ssize_t n = recvfrom(udpFd_, buf, sizeof(buf) - 1, 0, reinterpret_cast<sockaddr*>(&from), &len);
    if (n < 0) return;
    buf[n] = 0;
    if (strncmp(buf, "VIBMON_DISCOVER", 15) != 0) continue;
    const std::string reply = vp::jsonSerialize(info());
    sendto(udpFd_, reply.data(), reply.size(), 0, reinterpret_cast<sockaddr*>(&from), len);
  }
}

std::string AppApi::newPin(int64_t nowMs) {
  char b[8];
  const std::string r = randomHex(4);
  snprintf(b, sizeof(b), "%06lu", strtoul(r.c_str(), nullptr, 16) % 1000000UL);
  pin_ = b;
  pinUntilMs_ = nowMs + cfg_.pinTimeoutS * 1000LL;
  pinFailures_ = 0;
  lastPinMs_ = nowMs;
  if (host_.message) host_.message("手机APP配对码 " + pin_);
  return pin_;
}

bool AppApi::checkPin(const std::string& pin, int64_t nowMs, std::string& err) {
  if (pin_.empty() || nowMs > pinUntilMs_) {
    err = "pin_expired";
    return false;
  }
  if (pin != pin_) {
    // A few typing mistakes are fine; guessing is not.
    if (++pinFailures_ >= 5) pin_.clear();
    err = "pin_wrong";
    return false;
  }
  pin_.clear();
  return true;
}

std::string AppApi::issueToken(const std::string& client, const std::string& via) {
  Token t;
  t.token = randomHex(16);
  t.client = client.substr(0, 48);
  t.via = via;
  t.createdMs = realtimeMs();
  tokens_.push_back(t);
  while (static_cast<int>(tokens_.size()) > std::max(1, cfg_.maxTokens)) tokens_.erase(tokens_.begin());
  if (!saveTokens()) LOGW(kTag, "cannot save %s", cfg_.tokensFile.c_str());
  if (host_.message) host_.message("手机APP已连接 " + t.client);
  return t.token;
}

bool AppApi::tokenValid(const std::string& token) const {
  if (token.size() != 32) return false;
  for (const Token& t : tokens_)
    if (t.token == token) return true;
  return false;
}

bool AppApi::authorized(const HttpRequest& r) const { return !cfg_.requirePairing || tokenValid(bearer(r)); }

void AppApi::loadTokens() {
  tokens_.clear();
  JsonValue root;
  if (cfg_.tokensFile.empty() || !loadJsonFile(cfg_.tokensFile, root, nullptr)) return;
  const JsonValue* a = root.find("tokens");
  if (!a || !a->isArray()) return;
  for (size_t i = 0; i < a->size(); ++i) {
    Token t;
    t.token = jStr(a->at(i), "token", "");
    t.client = jStr(a->at(i), "client", "");
    t.via = jStr(a->at(i), "via", "");
    t.createdMs = jInt(a->at(i), "created_ms", 0);
    if (t.token.size() == 32) tokens_.push_back(t);
  }
}

bool AppApi::saveTokens() {
  if (cfg_.tokensFile.empty()) return true;
  JsonValue a = JsonValue::makeArray();
  for (const Token& t : tokens_) {
    JsonValue o = JsonValue::makeObject();
    o.set("token", t.token);
    o.set("client", t.client);
    o.set("via", t.via);
    o.set("created_ms", static_cast<long long>(t.createdMs));
    a.push(o);
  }
  JsonValue root = JsonValue::makeObject();
  root.set("tokens", a);
  const size_t slash = cfg_.tokensFile.rfind('/');
  if (slash != std::string::npos && slash > 0) makeDirs(cfg_.tokensFile.substr(0, slash));
  return writeFileAtomic(cfg_.tokensFile, jsonPretty(root));
}

HttpResponse AppApi::handle(const HttpRequest& r) {
  const std::string& p = r.path;
  const int64_t now = monoMs();
  if (p.compare(0, 8, "/api/v1/") != 0) return error(404, "not_found");
  const std::string route = p.substr(8);
  const bool get = r.method == "GET", post = r.method == "POST";
  if (route == "info" && get) return json(200, ok(info()));
  if (route == "pair/request" && post) {
    if (now - lastPinMs_ < 3000 && !pin_.empty()) return error(429, "wait");
    newPin(now);
    JsonValue d = JsonValue::makeObject();
    d.set("expires_s", cfg_.pinTimeoutS);
    d.set("shown_on", "device_screen");
    return json(200, ok(d));
  }
  if (route == "pair" && post) {
    JsonValue body;
    if (!vp::jsonParse(r.body, body, nullptr, 8) || !body.isObject()) return error(400, "bad_json");
    std::string err;
    if (cfg_.requirePairing && !checkPin(jStr(body, "pin", ""), now, err)) return error(403, err);
    JsonValue d = info();
    d.set("token", issueToken(jStr(body, "client", "phone"), "lan"));
    return json(200, ok(d));
  }
  if (!authorized(r)) return error(401, "unpaired");

  if (route == "unpair" && post) {
    const std::string t = bearer(r);
    tokens_.erase(std::remove_if(tokens_.begin(), tokens_.end(), [&t](const Token& x) { return x.token == t; }),
                  tokens_.end());
    saveTokens();
    return json(200, ok());
  }
  if (route == "live" && get) return json(200, ok(host_.live(r.param("wave") != "0")));
  if ((route == "status" || route == "settings") && get) {
    JsonValue c = JsonValue::makeObject();
    c.set("cmd", route);
    return json(200, host_.command(c));
  }
  if (route == "points" && get) return json(200, ok(host_.points ? host_.points() : JsonValue::makeArray()));
  if (route == "command" && post) {
    JsonValue c;
    if (!vp::jsonParse(r.body, c, nullptr, 8) || !c.isObject()) return error(400, "bad_json");
    const std::string cmd = jStr(c, "cmd", "");
    if (!appCommandAllowed(cmd, false)) return error(403, "command_not_allowed");
    const JsonValue reply = host_.command(c);
    return json(jBool(reply, "ok", false) ? 200 : 409, reply);
  }
  if (route == "records" && get) {
    const int limit = std::max(1, std::min(500, atoi(r.param("limit").empty() ? "100" : r.param("limit").c_str())));
    return json(200, ok(appRecordList(host_.recordsDir, static_cast<size_t>(limit), false)));
  }
  if (route.compare(0, 8, "records/") == 0 && get) {
    const std::string rest = route.substr(8);
    const size_t slash = rest.find('/');
    const std::string id = HttpServer::urlDecode(rest.substr(0, slash));
    if (!safeName(id)) return error(400, "bad_id");
    if (slash == std::string::npos) {
      JsonValue d;
      const int maxPoints = std::max(64, std::min(20000, atoi(r.param("points").empty() ? "2000" : r.param("points").c_str())));
      if (!appRecordDetail(host_.recordsDir, id, static_cast<size_t>(maxPoints), d)) return error(404, "no_record");
      return json(200, ok(d));
    }
    // Raw files for export: /records/<id>/files/<name>
    const std::string tail = rest.substr(slash + 1);
    if (tail.compare(0, 6, "files/") != 0) return error(404, "not_found");
    const std::string name = HttpServer::urlDecode(tail.substr(6));
    if (!safeName(name)) return error(400, "bad_name");
    std::string data;
    if (!readTextFile(host_.recordsDir + "/" + id + "/" + name, data)) return error(404, "no_file");
    HttpResponse resp;
    const size_t dot = name.rfind('.');
    const std::string ext = dot == std::string::npos ? std::string() : name.substr(dot + 1);
    resp.contentType = ext == "json" ? "application/json; charset=utf-8"
                       : ext == "csv" ? "text/csv; charset=utf-8"
                       : ext == "wav" ? "audio/wav"
                                      : "application/octet-stream";
    resp.headers["Content-Disposition"] = "attachment; filename=\"" + id + "_" + name + "\"";
    resp.body = std::move(data);
    return resp;
  }
  if (route == "preview.bmp" && get) {
    std::shared_ptr<const PreviewImage> img = host_.preview ? host_.preview() : nullptr;
    const int w = std::max(64, std::min(640, atoi(r.param("w").empty() ? "320" : r.param("w").c_str())));
    std::string bmp = img ? previewBmp(*img, w) : std::string();
    if (bmp.empty()) return error(503, "no_preview");
    HttpResponse resp;
    resp.contentType = "image/bmp";
    resp.headers["X-Frame"] = std::to_string(static_cast<unsigned long long>(img->frame));
    resp.body = std::move(bmp);
    return resp;
  }
  if (get || post) return error(404, "not_found");
  return error(405, "method");
}

}  // namespace vm
