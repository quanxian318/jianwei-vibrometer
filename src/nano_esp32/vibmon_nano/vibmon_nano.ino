// Vision vibration meter - Arduino Nano ESP32 (ABX00083) coprocessor firmware.
// Board package: "Arduino ESP32 Boards" (arduino:esp32:nano_nora), Pin
// Numbering "By Arduino pin (default)". Needs the shared library
// src/shared/vibproto (see ../README.md for the build command).
//
// Services: AX link (protocol V1.1), PN532 NFC, MicroSD + I2S voice, Wi-Fi/NTP,
// power-fail input, phone app gateway (access point, HTTP API, discovery). Everything runs from loop() except audio playback, which
// has its own task so SD reads and I2S writes never stall the link.

#include <SPI.h>
#include <vibproto.h>

#include "app_config.h"
#include "audio_service.h"
#include "board_pins.h"
#include "console.h"
#include "esp_random.h"
#include "esp_system.h"
#include "event_store.h"
#include "http_gateway.h"
#include "link.h"
#include "log.h"
#include "net_service.h"
#include "nfc_service.h"
#include "power_service.h"
#include "router.h"
#include "sd_service.h"
#include "settings.h"

namespace {

const char* kTag = "main";

std::string makeBootId() {
  char buf[17];
  snprintf(buf, sizeof(buf), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
           static_cast<unsigned long>(esp_random()));
  return std::string(buf);
}

const char* resetReasonName(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON: return "power_on";
    case ESP_RST_EXT: return "external";
    case ESP_RST_SW: return "software";
    case ESP_RST_PANIC: return "panic";
    case ESP_RST_INT_WDT: return "interrupt_watchdog";
    case ESP_RST_TASK_WDT: return "task_watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep_sleep";
    case ESP_RST_BROWNOUT: return "brownout";
    case ESP_RST_SDIO: return "sdio";
    default: return "unknown";
  }
}

void safeOutputs() {
  // Order from the development notes: amplifier off, both chip selects
  // released, event IRQ low - before anything else touches the buses.
  pinMode(board_pins::AMP_EN, OUTPUT);
  digitalWrite(board_pins::AMP_EN, LOW);
  pinMode(board_pins::SD_CS, OUTPUT);
  digitalWrite(board_pins::SD_CS, HIGH);
  pinMode(board_pins::PN_CS, OUTPUT);
  digitalWrite(board_pins::PN_CS, HIGH);
  pinMode(board_pins::AX_EVENT_IRQ, OUTPUT);
  digitalWrite(board_pins::AX_EVENT_IRQ, LOW);
}

void onLinkFrame(const vp::Frame& frame) { router::onFrame(frame); }

bool g_irqLevel = false;

}  // namespace

void setup() {
  safeOutputs();
  logBegin();
  delay(50);

  g_settings.load();
  const std::string bootId = makeBootId();
  g_events.begin(bootId);
  router::begin(bootId);
  const char* reason = resetReasonName(esp_reset_reason());
  LOGI(kTag, "vibmon nano %s (%s) boot %s, reset %s", VIBMON_NANO_VERSION, VIBMON_NANO_PROFILE, bootId.c_str(), reason);

  g_power.begin();
  SPI.begin(board_pins::SPI_SCLK, board_pins::SPI_MISO, board_pins::SPI_MOSI, -1);
  if (!g_audio.begin()) {
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("module", "audio");
    d.set("code", "I2S_INIT_FAILED");
    g_events.post("FAULT", d);
  }
  g_sd.begin();
  g_nfc.begin();
  g_link.begin(g_settings.linkBaud, g_settings.flowControl, onLinkFrame);

  vp::JsonValue boot = vp::JsonValue::makeObject();
  boot.set("nano_version", VIBMON_NANO_VERSION);
  boot.set("reset_reason", reason);
  boot.set("link_baud", static_cast<unsigned long>(g_settings.linkBaud));
  g_events.post("BOOT", boot);

  g_net.begin();
  http_gateway::begin();
  enableLoopWDT();
}

void loop() {
  g_power.poll();
  g_link.poll();
  g_sd.poll();
  g_nfc.poll();
  g_net.poll();
  http_gateway::poll();
  console::poll();
  router::pumpEvents();
  g_link.flushTx();

  // IRQ stays high while any event is unacknowledged (level, not pulses).
  const bool irq = g_events.hasUnacked();
  if (irq != g_irqLevel) {
    g_irqLevel = irq;
    digitalWrite(board_pins::AX_EVENT_IRQ, irq ? HIGH : LOW);
  }
  delay(1);
}
