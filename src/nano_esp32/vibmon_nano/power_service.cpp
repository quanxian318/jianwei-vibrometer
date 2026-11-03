#include "power_service.h"

#include "app_config.h"
#include "audio_service.h"
#include "board_pins.h"
#include "event_store.h"
#include "log.h"
#include "sd_service.h"
#include "soc/gpio_reg.h"
#include "soc/soc.h"

PowerService g_power;

namespace {
const char* kTag = "power";
volatile uint32_t s_fallCount = 0;
portMUX_TYPE s_powerMux = portMUX_INITIALIZER_UNLOCKED;
bool s_ampBlocked = true;

void IRAM_ATTR pfailIsr() {
  const uint32_t level = (REG_READ(GPIO_IN_REG) >> board_pins::gpio_raw::PFAIL_N) & 1u;
  if (level == 0) {
    portENTER_CRITICAL_ISR(&s_powerMux);
    s_ampBlocked = true;
    REG_WRITE(GPIO_OUT_W1TC_REG, 1u << board_pins::gpio_raw::AMP_EN);
    s_fallCount = s_fallCount + 1;
    portEXIT_CRITICAL_ISR(&s_powerMux);
  }
}
}  // namespace

void PowerService::begin() {
  pinMode(board_pins::PFAIL_N, INPUT);  // comparator output, 10k pull-up to 3V3_D
  attachInterrupt(board_pins::PFAIL_N, pfailIsr, CHANGE);
  portENTER_CRITICAL(&s_powerMux);
  s_ampBlocked = digitalRead(board_pins::PFAIL_N) == LOW;
  portEXIT_CRITICAL(&s_powerMux);
  lastFallCount_ = s_fallCount;
  if (digitalRead(board_pins::PFAIL_N) == LOW) {
    active_ = true;
    LOGW(kTag, "PFAIL_N low at boot");
  }
}

bool PowerService::setAmplifierEnabled(bool on) {
  portENTER_CRITICAL(&s_powerMux);
  const bool enabled = on && !s_ampBlocked &&
      ((REG_READ(GPIO_IN_REG) >> board_pins::gpio_raw::PFAIL_N) & 1u);
  REG_WRITE(enabled ? GPIO_OUT_W1TS_REG : GPIO_OUT_W1TC_REG, 1u << board_pins::gpio_raw::AMP_EN);
  portEXIT_CRITICAL(&s_powerMux);
  return enabled;
}

void PowerService::poll() {
  const uint32_t now = millis();
  const bool low = digitalRead(board_pins::PFAIL_N) == LOW;
  const uint32_t falls = s_fallCount;
  const bool newFall = falls != lastFallCount_;
  lastFallCount_ = falls;
  if ((low || newFall) && !active_) {
    active_ = true;
    highSinceMs_ = 0;
    LOGW(kTag, "power fail asserted");
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("active", true);
    g_events.post("POWER_FAIL", d);
    g_audio.stopAll("power_fail", true);
  }
  if (!active_) return;
  if (low || newFall) {
    highSinceMs_ = 0;
    return;
  }
  if (highSinceMs_ == 0) {
    highSinceMs_ = now == 0 ? 1 : now;
    return;
  }
  if (now - highSinceMs_ >= cfg::kPfailReleaseMs) {
    portENTER_CRITICAL(&s_powerMux);
    const bool released = ((REG_READ(GPIO_IN_REG) >> board_pins::gpio_raw::PFAIL_N) & 1u) &&
        s_fallCount == lastFallCount_;
    if (released) {
      active_ = false;
      s_ampBlocked = prepared_;
    }
    portEXIT_CRITICAL(&s_powerMux);
    if (!released) {
      highSinceMs_ = 0;
      return;
    }
    LOGI(kTag, "power fail released");
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("active", false);
    g_events.post("POWER_FAIL", d);
  }
}

void PowerService::preparePowerOff(const std::string& reason) {
  prepared_ = true;
  portENTER_CRITICAL(&s_powerMux);
  s_ampBlocked = true;
  REG_WRITE(GPIO_OUT_W1TC_REG, 1u << board_pins::gpio_raw::AMP_EN);
  portEXIT_CRITICAL(&s_powerMux);
  preparedReason_ = reason;
  g_audio.stopAll("power_off", false);
  g_sd.allowMount(false);
  LOGI(kTag, "prepared for power off (%s)", reason.c_str());
}

void PowerService::clearPrepared() {
  if (!prepared_) return;
  prepared_ = false;
  portENTER_CRITICAL(&s_powerMux);
  s_ampBlocked = active_ || digitalRead(board_pins::PFAIL_N) == LOW;
  portEXIT_CRITICAL(&s_powerMux);
  preparedReason_.clear();
  g_sd.allowMount(true);
}

void PowerService::describe(vp::JsonValue& obj) const {
  obj.set("power_fail", active_);
  obj.set("power_off_prepared", prepared_);
}
