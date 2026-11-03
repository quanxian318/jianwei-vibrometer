#include "console.h"

#include <Arduino.h>

#include <string>
#include <vector>

#include <vibproto.h>

#include "app_config.h"
#include "app_gateway.h"
#include "board_pins.h"
#include "console_io.h"
#include "audio_service.h"
#include "event_store.h"
#include "link.h"
#include "log.h"
#include "net_service.h"
#include "nfc_service.h"
#include "power_service.h"
#include "router.h"
#include "sd_service.h"
#include "settings.h"

namespace console {
namespace {

uint32_t g_jobCounter = 0;

void println(const std::string& s) {
  std::string line = s + "\r\n";
  logRaw(line.c_str());
}

void printJson(const vp::JsonValue& v) { println(vp::jsonSerialize(v)); }

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (c == ' ' || c == '\t') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

void cmdStatus() {
  vp::JsonValue s = vp::JsonValue::makeObject();
  s.set("version", VIBMON_NANO_VERSION);
  s.set("boot_id", g_events.bootId());
  vp::JsonValue r = vp::JsonValue::makeObject();
  router::describe(r);
  s.set("session", r);
  vp::JsonValue l = vp::JsonValue::makeObject();
  g_link.describe(l);
  s.set("link", l);
  vp::JsonValue w = vp::JsonValue::makeObject();
  g_net.describe(w);
  s.set("wifi", w);
  s.set("time_valid", g_net.timeValid());
  vp::JsonValue sd = vp::JsonValue::makeObject();
  g_sd.describeShort(sd);
  s.set("sd", sd);
  vp::JsonValue n = vp::JsonValue::makeObject();
  g_nfc.describe(n);
  s.set("nfc", n);
  vp::JsonValue p = vp::JsonValue::makeObject();
  g_power.describe(p);
  s.set("power", p);
  vp::JsonValue e = vp::JsonValue::makeObject();
  g_events.describe(e);
  s.set("events", e);
  vp::JsonValue gw = vp::JsonValue::makeObject();
  g_gateway.describe(gw);
  s.set("gateway", gw);
  printJson(s);
}

void cmdEvents() {
  std::vector<std::string> list;
  uint32_t next = 0;
  bool more = false;
  g_events.collect(0, cfg::kEventQueueCapacity, 1u << 20, list, next, more);
  for (size_t i = 0; i < list.size(); ++i) println(list[i]);
  if (list.empty()) println("(no unacknowledged events)");
}

void handle(const std::string& line) {
  std::vector<std::string> a = split(line);
  if (a.empty()) return;
  const std::string& c = a[0];
  if (c == "help") {
    println("help ping status events sd nfc [on|off] spi | play <asset_id> [volume] | stop | wifi <ssid> [password] | wifi off");
    println("ap on|off | ap pass <8..63 chars> (phone app access point)");
    println("baud <rate> | flow on|off | log error|warn|info|debug | reboot");
  } else if (c == "ping") {
    println("pong");
  } else if (c == "status") {
    cmdStatus();
  } else if (c == "events") {
    cmdEvents();
  } else if (c == "sd") {
    vp::JsonValue d = vp::JsonValue::makeObject();
    g_sd.describe(d);
    printJson(d);
  } else if (c == "nfc") {
    if (a.size() > 2 || (a.size() == 2 && a[1] != "on" && a[1] != "off")) {
      println("usage: nfc [on|off]");
      return;
    }
    if (a.size() == 2) g_nfc.setEnabled(a[1] == "on");
    vp::JsonValue d = vp::JsonValue::makeObject();
    g_nfc.describe(d);
    printJson(d);
  } else if (c == "spi") {
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("sd_cs_high", digitalRead(board_pins::SD_CS) == HIGH);
    d.set("nfc_cs_high", digitalRead(board_pins::PN_CS) == HIGH);
    d.set("miso_high", digitalRead(board_pins::SPI_MISO) == HIGH);
    d.set("nfc_irq_low", digitalRead(board_pins::PN_IRQ) == LOW);
    d.set("nfc_enabled", g_nfc.enabled());
    d.set("sd_mounted", g_sd.mounted());
    d.set("audio_busy", g_audio.busy());
    printJson(d);
  } else if (c == "play" && a.size() >= 2) {
    const VoiceAsset* asset = findVoiceAsset(a[1].c_str());
    if (asset == nullptr) {
      println("unknown asset_id");
      return;
    }
    if (g_power.restricted()) {
      println("refused: power fail / power-off prepared");
      return;
    }
    int vol = a.size() >= 3 ? atoi(a[2].c_str()) : 30;
    char id[24];
    snprintf(id, sizeof(id), "console-%lu", static_cast<unsigned long>(++g_jobCounter));
    AudioJob job;
    EnqueueResult r = g_audio.enqueue(id, asset->id, asset->path, vol, job);
    println(r == EnqueueResult::kQueued ? std::string("queued ") + id : std::string("not queued"));
  } else if (c == "stop") {
    g_audio.stopAll("console_stop", false);
    println("stopped");
  } else if (c == "wifi" && a.size() >= 2) {
    if (a[1] == "off") {
      g_net.configure("", "");
      g_settings.saveWifi("", "");
      println("wifi disabled");
    } else {
      std::string pass = a.size() >= 3 ? a[2] : std::string();
      g_net.configure(a[1], pass);
      println(g_settings.saveWifi(a[1], pass) ? "wifi saved" : "wifi save failed");
    }
  } else if (c == "ap" && a.size() >= 2) {
    if (a[1] == "pass" && a.size() >= 3 && a[2].size() >= 8 && a[2].size() <= 63) {
      const bool ok = g_settings.saveAp(g_settings.apEnabled, a[2]);
      if (ok && g_settings.apEnabled) g_net.configureAp(true, a[2]);
      println(ok ? "ap password saved" : "ap save failed");
    } else if (a[1] == "on" || a[1] == "off") {
      const bool on = a[1] == "on";
      const bool ok = g_settings.saveAp(on, g_settings.apPassword);
      if (ok) g_net.configureAp(on, g_settings.apPassword);
      println(ok ? (on ? "ap on" : "ap off") : "ap save failed");
    } else {
      println("usage: ap on|off | ap pass <8..63 chars>");
    }
  } else if (c == "baud" && a.size() >= 2) {
    uint32_t b = static_cast<uint32_t>(strtoul(a[1].c_str(), nullptr, 10));
    if (b < 9600 || b > 5000000) {
      println("baud out of range");
      return;
    }
    println(g_settings.saveLink(b, g_settings.flowControl) ? "saved; reboot to apply" : "save failed");
  } else if (c == "flow" && a.size() >= 2) {
    bool on = a[1] == "on";
    println(g_settings.saveLink(g_settings.linkBaud, on) ? "saved; reboot to apply" : "save failed");
  } else if (c == "log" && a.size() >= 2) {
    const std::string& l = a[1];
    logSetLevel(l == "error" ? LogLevel::kError : l == "warn" ? LogLevel::kWarn : l == "debug" ? LogLevel::kDebug : LogLevel::kInfo);
    println("ok");
  } else if (c == "reboot") {
    println("rebooting");
    console_io::drainBeforeRestart(100);
    ESP.restart();
  } else {
    println("unknown command; type help");
  }
}

}  // namespace

void poll() {
  console_io::poll(handle);
}

}  // namespace console
