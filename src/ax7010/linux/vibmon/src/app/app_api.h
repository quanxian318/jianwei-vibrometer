#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/http_server.h"
#include "app/state.h"
#include "measure/session.h"
#include "util/event_loop.h"
#include "util/json_util.h"

namespace vm {

// Phone app access to vibmond (API v1, 15_手机APP_20261025/README.md):
//  * HTTP on the LAN (Ethernet of the AX7010);
//  * UDP discovery: "VIBMON_DISCOVER" to discoveryPort, unicast JSON reply;
//  * optional pairing (require_pairing): the app asks for a code, the device
//    screen shows six digits for pinTimeoutS, the app sends them back and
//    receives a token. Off by default: the device is for private use on a
//    trusted network, so every request is accepted.
// Everything runs on the main loop; commands go through the same handler as
// the touch UI and the control socket.
struct AppApiConfig {
  bool enabled = true;
  int httpPort = 8080;
  int discoveryPort = 48300;
  std::string name;               // shown in the app; empty = device id
  bool requirePairing = false;    // private use: open on the LAN unless enabled
  std::string tokensFile = "/home/root/vibmon/app_tokens.json";
  int pinTimeoutS = 120;
  int maxTokens = 8;
};

struct AppApiHost {
  std::function<JsonValue(const JsonValue&)> command;  // {"cmd":...} -> {"ok":...}
  std::function<JsonValue(bool wave)> live;            // compact live state (also published to the Nano)
  std::function<std::shared_ptr<const PreviewImage>()> preview;
  std::function<void(const std::string&)> message;     // device screen message line
  std::function<JsonValue()> points;                   // configured NFC measuring points
  std::string recordsDir, deviceId, version;
};

// Commands the app may send; the rest of the control socket stays local.
bool appCommandAllowed(const std::string& cmd, bool viaGateway);
// Level code for the app ("normal" | "warn" | "alarm" | "unknown").
const char* levelCode(AlarmLevel level);
// record.json + decimated waveform/spectrum for the record page.
bool appRecordDetail(const std::string& dir, const std::string& id, size_t maxPoints, JsonValue& out);
JsonValue appRecordList(const std::string& dir, size_t maxCount, bool compact);
// Valid record id / file name: letters, digits, "-", "_", "." (not leading).
bool safeName(const std::string& s);
// 24-bit BMP of the preview scaled to at most maxWidth pixels wide.
std::string previewBmp(const PreviewImage& p, int maxWidth);

class AppApi {
 public:
  AppApi(EventLoop& loop, const AppApiConfig& cfg, AppApiHost host);
  ~AppApi();
  bool start();
  int httpPort() const { return http_ ? http_->port() : 0; }
  int discoveryPort() const { return udpPort_; }
  void tick(int64_t nowMs);

  // Optional LAN pairing; the code is shown on the device screen.
  std::string newPin(int64_t nowMs);
  bool checkPin(const std::string& pin, int64_t nowMs, std::string& err);
  std::string issueToken(const std::string& client, const std::string& via);
  bool tokenValid(const std::string& token) const;
  JsonValue info() const;

  HttpResponse handle(const HttpRequest& r);  // public for tests

 private:
  void onDiscovery();
  void loadTokens();
  bool saveTokens();
  bool authorized(const HttpRequest& r) const;

  struct Token {
    std::string token, client, via;
    int64_t createdMs = 0;
  };
  EventLoop& loop_;
  AppApiConfig cfg_;
  AppApiHost host_;
  std::unique_ptr<HttpServer> http_;
  int udpFd_ = -1, udpPort_ = 0;
  std::vector<Token> tokens_;
  std::string pin_;
  int64_t pinUntilMs_ = 0;
  int pinFailures_ = 0;
  int64_t lastPinMs_ = -1000000;
};

}  // namespace vm
