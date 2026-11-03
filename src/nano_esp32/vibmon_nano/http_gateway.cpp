#include "http_gateway.h"

#include <ESPmDNS.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiUdp.h>

#include <string>

#include "app_config.h"
#include "app_gateway.h"
#include "event_store.h"
#include "log.h"
#include "net_service.h"
#include "router.h"
#include "settings.h"

namespace http_gateway {
namespace {

const char* kTag = "gw";
WebServer g_server(cfg::kHttpPort);
WiFiUDP g_udp;
uint32_t g_lastPollMs = 0;

std::string configureNet(const vp::JsonValue& b) {
  // Station: {"ssid", "password"}; access point: {"ap_enabled", "ap_password"}.
  std::string ssid, password, apPassword;
  bool apEnabled = g_settings.apEnabled;
  const bool sta = b.getString("ssid", ssid);
  if (sta) {
    b.getString("password", password);
    if (ssid.size() > 32 || password.size() > 63 || (!password.empty() && password.size() < 8)) return "bad_station";
  }
  const bool apKey = b.getBool("ap_enabled", apEnabled);
  const bool apPass = b.getString("ap_password", apPassword);
  if (apPass && (apPassword.size() < 8 || apPassword.size() > 63)) return "bad_ap_password";
  if (!sta && !apKey && !apPass) return "nothing_to_change";
  if (sta) {
    if (!g_settings.saveWifi(ssid, password)) return "nvs_write";
    g_net.configure(ssid, password);
  }
  if (apKey || apPass) {
    if (!apEnabled && !g_settings.apEnabled && !apPass) return std::string();
    // Without a station network the access point is the only way in: keep it.
    if (!apEnabled && g_settings.wifiSsid.empty()) return "ap_needed_without_station";
    if (!g_settings.saveAp(apEnabled, apPass ? apPassword : g_settings.apPassword)) return "nvs_write";
    g_net.configureAp(apEnabled, g_settings.apPassword);
  }
  return std::string();
}

void sendCors() {
  g_server.sendHeader("Access-Control-Allow-Origin", "*");
  g_server.sendHeader("Access-Control-Allow-Headers", "Authorization, Content-Type");
  g_server.sendHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  g_server.sendHeader("Cache-Control", "no-store");
}

void handleAny() {
  sendCors();
  const HTTPMethod m = g_server.method();
  if (m == HTTP_OPTIONS) {
    g_server.send(204);
    return;
  }
  const std::string method = m == HTTP_GET ? "GET" : (m == HTTP_POST ? "POST" : "OTHER");
  const std::string body = g_server.hasArg("plain") ? std::string(g_server.arg("plain").c_str()) : std::string();
  const std::string client = g_server.client().remoteIP().toString().c_str();
  const AppGateway::Response r =
      g_gateway.handle(method, g_server.uri().c_str(), std::string(), body, client, millis());
  g_server.send(r.status, "application/json; charset=utf-8", String(r.body.c_str()));
}

}  // namespace

void begin() {
  AppGateway::Hooks h;
  h.postEvent = [](const char* type, const vp::JsonValue& d) { return g_events.post(type, d); };
  h.axOnline = []() { return router::sessionActive(); };
  h.net = [](vp::JsonValue& o) {
    vp::JsonValue sta = vp::JsonValue::makeObject();
    g_net.describe(sta);
    o.set("station", sta);
    o.set("ap_password_set", !g_settings.apPassword.empty());
    o.set("mdns", (std::string("vibmon-") + g_net.apSsid().substr(7) + ".local").c_str());
  };
  h.configureNet = configureNet;
  h.scan = [](vp::JsonValue& o) { g_net.scan(o); };
  g_gateway.begin(h, VIBMON_NANO_VERSION);
  g_server.onNotFound(handleAny);
  g_server.begin();
  g_udp.begin(cfg::kDiscoveryPort);
  std::string host = "vibmon-" + g_net.apSsid().substr(7);
  for (char& c : host) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
  if (MDNS.begin(host.c_str())) {
    MDNS.addService("vibmon", "tcp", cfg::kHttpPort);
    MDNS.addServiceTxt("vibmon", "tcp", "api", "1");
    MDNS.addServiceTxt("vibmon", "tcp", "kind", "gateway");
  }
  LOGI(kTag, "app gateway: http %u, discovery udp %u, %s.local", cfg::kHttpPort, cfg::kDiscoveryPort, host.c_str());
}

void poll() {
  g_server.handleClient();
  const int n = g_udp.parsePacket();
  if (n > 0) {
    char buf[64];
    const int len = g_udp.read(buf, sizeof(buf) - 1);
    buf[len > 0 ? len : 0] = 0;
    if (len >= 15 && strncmp(buf, "VIBMON_DISCOVER", 15) == 0) {
      const std::string info = vp::jsonSerialize(g_gateway.infoJson(millis()));
      g_udp.beginPacket(g_udp.remoteIP(), g_udp.remotePort());
      g_udp.write(reinterpret_cast<const uint8_t*>(info.data()), info.size());
      g_udp.endPacket();
    }
  }
  const uint32_t now = millis();
  if (now - g_lastPollMs >= 250) {
    g_lastPollMs = now;
    g_gateway.poll(now);
  }
}

}  // namespace http_gateway
