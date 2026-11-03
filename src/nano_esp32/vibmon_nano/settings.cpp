#include "settings.h"

#include <Preferences.h>

#include "app_config.h"
#include "log.h"

Settings g_settings;

namespace {
const char* kNs = "vibmon";

std::string readString(Preferences& p, const char* key, const char* def) {
  String v = p.getString(key, def);
  return std::string(v.c_str());
}
}  // namespace

void Settings::load() {
  Preferences p;
  if (!p.begin(kNs, true)) {
    // Namespace does not exist yet: defaults.
    linkBaud = cfg::kDefaultLinkBaud;
    flowControl = cfg::kDefaultFlowControl;
    ntp1 = cfg::kDefaultNtp1;
    ntp2 = cfg::kDefaultNtp2;
    ntp3 = cfg::kDefaultNtp3;
    apEnabled = true;
    apPassword = cfg::kDefaultApPassword;
    return;
  }
  wifiSsid = readString(p, "ssid", "");
  wifiPassword = readString(p, "pass", "");
  linkBaud = p.getUInt("baud", cfg::kDefaultLinkBaud);
  flowControl = p.getBool("flow", cfg::kDefaultFlowControl);
  ntp1 = readString(p, "ntp1", cfg::kDefaultNtp1);
  ntp2 = readString(p, "ntp2", cfg::kDefaultNtp2);
  ntp3 = readString(p, "ntp3", cfg::kDefaultNtp3);
  apEnabled = p.getBool("ap_on", true);
  apPassword = readString(p, "ap_pass", cfg::kDefaultApPassword);
  p.end();
  if (apPassword.size() < 8 || apPassword.size() > 63) apPassword = cfg::kDefaultApPassword;
  if (linkBaud < 9600 || linkBaud > 5000000) linkBaud = cfg::kDefaultLinkBaud;
}

bool Settings::saveWifi(const std::string& ssid, const std::string& password) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putString("ssid", ssid.c_str()) == ssid.size() || ssid.empty();
  ok = (p.putString("pass", password.c_str()) == password.size() || password.empty()) && ok;
  p.end();
  if (ok) {
    wifiSsid = ssid;
    wifiPassword = password;
  }
  return ok;
}

bool Settings::saveLink(uint32_t baud, bool flow) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putUInt("baud", baud) == sizeof(uint32_t);
  ok = p.putBool("flow", flow) == sizeof(uint8_t) && ok;
  p.end();
  if (ok) {
    linkBaud = baud;
    flowControl = flow;
  }
  return ok;
}

bool Settings::saveAp(bool enabled, const std::string& password) {
  Preferences p;
  if (!p.begin(kNs, false)) return false;
  bool ok = p.putBool("ap_on", enabled) == sizeof(uint8_t);
  ok = p.putString("ap_pass", password.c_str()) == password.size() && ok;
  p.end();
  if (ok) {
    apEnabled = enabled;
    apPassword = password;
  }
  return ok;
}
