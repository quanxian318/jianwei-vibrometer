#include "net_service.h"

#include <WiFi.h>
#include <sys/time.h>

#include "app_config.h"
#include "esp_sntp.h"
#include "esp_timer.h"
#include "event_store.h"
#include "log.h"
#include "settings.h"

NetService g_net;

namespace {
const char* kTag = "net";
volatile uint8_t s_lastReason = 0;
volatile bool s_syncPending = false;
volatile int64_t s_syncUnixMs = 0;
volatile int64_t s_syncUptimeMs = 0;

void onWifiEvent(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (event == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) s_lastReason = info.wifi_sta_disconnected.reason;
}

void onTimeSync(struct timeval* tv) {
  s_syncUnixMs = static_cast<int64_t>(tv->tv_sec) * 1000 + tv->tv_usec / 1000;
  s_syncUptimeMs = esp_timer_get_time() / 1000;
  s_syncPending = true;
}

int64_t uptimeMs() { return esp_timer_get_time() / 1000; }
}  // namespace

const char* NetService::stateName(State s) {
  switch (s) {
    case State::kOff: return "OFF";
    case State::kConnecting: return "CONNECTING";
    case State::kConnected: return "CONNECTED";
    case State::kWaitRetry: return "DISCONNECTED";
  }
  return "UNKNOWN";
}

void NetService::begin() {
  WiFi.persistent(false);  // credentials live in our own NVS namespace
  WiFi.setHostname("vibmon-nano");
  WiFi.mode(g_settings.apEnabled ? WIFI_AP_STA : WIFI_STA);
  WiFi.setAutoReconnect(false);
  uint8_t mac[6] = {0};
  WiFi.macAddress(mac);
  char name[16];
  snprintf(name, sizeof(name), "VIBMON-%02X%02X", mac[4], mac[5]);
  apSsid_ = name;
  if (g_settings.apEnabled) startAp();
  WiFi.onEvent(onWifiEvent);
  sntp_set_time_sync_notification_cb(onTimeSync);
  ssid_ = g_settings.wifiSsid;
  password_ = g_settings.wifiPassword;
  backoffMs_ = cfg::kWifiBackoffMinMs;
  if (ssid_.empty()) {
    enter(State::kOff, "not_configured");
  } else {
    startConnect();
  }
}

void NetService::configure(const std::string& ssid, const std::string& password) {
  ssid_ = ssid;
  password_ = password;
  attempts_ = 0;
  backoffMs_ = cfg::kWifiBackoffMinMs;
  WiFi.disconnect(false, false);
  if (ssid_.empty()) {
    enter(State::kOff, "disabled");
  } else {
    startConnect();
  }
}

void NetService::startAp() {
  // Fixed address so the app finds the gateway without discovery.
  WiFi.softAPConfig(IPAddress(192, 168, 4, 1), IPAddress(192, 168, 4, 1), IPAddress(255, 255, 255, 0));
  apOn_ = WiFi.softAP(apSsid_.c_str(), g_settings.apPassword.c_str(), 6, 0, 4);
  LOGI(kTag, "access point %s %s", apSsid_.c_str(), apOn_ ? "on, 192.168.4.1" : "failed");
}

void NetService::configureAp(bool enabled, const std::string& password) {
  if (enabled) {
    WiFi.mode(WIFI_AP_STA);
    if (apOn_) WiFi.softAPdisconnect(false);
    startAp();
  } else {
    WiFi.softAPdisconnect(true);
    apOn_ = false;
    WiFi.mode(WIFI_STA);
    LOGI(kTag, "access point off");
  }
  (void)password;  // already stored in g_settings by the caller
}

void NetService::describeAp(vp::JsonValue& obj) const {
  obj.set("enabled", apOn_);
  obj.set("ssid", apSsid_);
  obj.set("ip", apOn_ ? vp::JsonValue(WiFi.softAPIP().toString().c_str()) : vp::JsonValue());
  obj.set("clients", apOn_ ? static_cast<int>(WiFi.softAPgetStationNum()) : 0);
}

void NetService::scan(vp::JsonValue& obj) {
  const int16_t n = WiFi.scanComplete();
  if (n == WIFI_SCAN_RUNNING) {
    obj.set("scanning", true);
    return;
  }
  vp::JsonValue list = vp::JsonValue::makeArray();
  for (int16_t i = 0; i < n && i < 20; ++i) {
    vp::JsonValue o = vp::JsonValue::makeObject();
    o.set("ssid", WiFi.SSID(i).c_str());
    o.set("rssi", static_cast<int>(WiFi.RSSI(i)));
    o.set("secure", WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    list.push(o);
  }
  obj.set("networks", list);
  // Results are read once; the next request starts a fresh scan.
  WiFi.scanDelete();
  WiFi.scanNetworks(true);
  obj.set("scanning", n < 0);
}

void NetService::startConnect() {
  attempts_++;
  s_lastReason = 0;
  WiFi.disconnect(false, false);
  WiFi.begin(ssid_.c_str(), password_.empty() ? nullptr : password_.c_str());
  deadlineMs_ = millis() + cfg::kWifiConnectTimeoutMs;
  LOGI(kTag, "connecting to \"%s\" (attempt %lu)", ssid_.c_str(), static_cast<unsigned long>(attempts_));
  enter(State::kConnecting, nullptr);
}

void NetService::enter(State s, const char* reason) {
  state_ = s;
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("state", stateName(s));
  d.set("ssid", ssid_);
  if (s == State::kConnected) {
    d.set("ip", WiFi.localIP().toString().c_str());
    d.set("rssi", static_cast<int>(WiFi.RSSI()));
  }
  if (reason) d.set("reason", reason);
  if (s_lastReason) d.set("wifi_reason", static_cast<unsigned>(s_lastReason));
  g_events.post("WIFI_STATE", d);
}

void NetService::startSntp() {
  if (sntpStarted_) return;
  configTime(0, 0, g_settings.ntp1.c_str(), g_settings.ntp2.c_str(), g_settings.ntp3.c_str());
  sntpStarted_ = true;
  LOGI(kTag, "SNTP started");
}

void NetService::poll() {
  const uint32_t now = millis();
  const wl_status_t st = WiFi.status();
  switch (state_) {
    case State::kOff:
      break;
    case State::kConnecting:
      if (st == WL_CONNECTED) {
        backoffMs_ = cfg::kWifiBackoffMinMs;
        LOGI(kTag, "connected, ip %s", WiFi.localIP().toString().c_str());
        enter(State::kConnected, nullptr);
        startSntp();
      } else if (static_cast<int32_t>(now - deadlineMs_) >= 0 || st == WL_CONNECT_FAILED || st == WL_NO_SSID_AVAIL) {
        WiFi.disconnect(false, false);
        retryAtMs_ = now + backoffMs_;
        LOGW(kTag, "connect failed (status %d, reason %u), retry in %lu s", static_cast<int>(st),
             static_cast<unsigned>(s_lastReason), static_cast<unsigned long>(backoffMs_ / 1000));
        backoffMs_ = backoffMs_ * 2 > cfg::kWifiBackoffMaxMs ? cfg::kWifiBackoffMaxMs : backoffMs_ * 2;
        enter(State::kWaitRetry, st == WL_NO_SSID_AVAIL ? "no_ssid" : "connect_failed");
      }
      break;
    case State::kConnected:
      if (st != WL_CONNECTED) {
        retryAtMs_ = now + cfg::kWifiBackoffMinMs;
        LOGW(kTag, "link lost (reason %u)", static_cast<unsigned>(s_lastReason));
        enter(State::kWaitRetry, "link_lost");
      }
      break;
    case State::kWaitRetry:
      if (static_cast<int32_t>(now - retryAtMs_) >= 0) startConnect();
      break;
  }
  if (s_syncPending) {
    s_syncPending = false;
    timeValid_ = true;
    lastSyncUptimeMs_ = s_syncUptimeMs;
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("unix_ms", static_cast<long long>(s_syncUnixMs));
    d.set("sample_uptime_ms", static_cast<long long>(s_syncUptimeMs));
    d.set("uncertainty_ms", static_cast<unsigned long>(cfg::kNtpBaseUncertaintyMs));
    d.set("source", "ntp");
    g_events.post("TIME_SYNC", d);
    LOGI(kTag, "time synchronised");
  }
}

void NetService::describe(vp::JsonValue& obj) const {
  obj.set("state", stateName(state_));
  obj.set("ssid", ssid_.empty() ? vp::JsonValue() : vp::JsonValue(ssid_));
  if (state_ == State::kConnected) {
    obj.set("ip", WiFi.localIP().toString().c_str());
    obj.set("rssi", static_cast<int>(WiFi.RSSI()));
  }
  obj.set("attempts", static_cast<unsigned long>(attempts_));
  vp::JsonValue ap = vp::JsonValue::makeObject();
  describeAp(ap);
  obj.set("ap", ap);
}

void NetService::timeInfo(vp::JsonValue& obj) const {
  struct timeval tv;
  gettimeofday(&tv, nullptr);
  const int64_t up = uptimeMs();
  obj.set("valid", timeValid_);
  if (timeValid_) {
    const int64_t unixMs = static_cast<int64_t>(tv.tv_sec) * 1000 + tv.tv_usec / 1000;
    const int64_t age = up - lastSyncUptimeMs_;
    obj.set("unix_ms", static_cast<long long>(unixMs));
    obj.set("uncertainty_ms", static_cast<long long>(cfg::kNtpBaseUncertaintyMs + age / 20000));  // 50 ppm
    obj.set("source", "ntp");
    obj.set("last_sync_age_ms", static_cast<long long>(age));
  } else {
    obj.set("unix_ms", vp::JsonValue());
    obj.set("uncertainty_ms", vp::JsonValue());
    obj.set("source", "none");
  }
  obj.set("sample_uptime_ms", static_cast<long long>(up));
}
