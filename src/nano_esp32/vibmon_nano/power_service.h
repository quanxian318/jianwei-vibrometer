#pragma once
#include <Arduino.h>

#include <string>

#include <vibproto.h>

// PFAIL_N (A6, low = 12 V bus below ~10.8 V). The ISR switches the speaker
// amplifier off at once; the loop reports POWER_FAIL and blocks new audio and
// persistent writes. Release needs 1 s of stable high level.
class PowerService {
 public:
  void begin();
  void poll();
  bool setAmplifierEnabled(bool on);
  bool failActive() const { return active_; }
  bool powerOffPrepared() const { return prepared_; }
  bool restricted() const { return active_ || prepared_; }
  void preparePowerOff(const std::string& reason);
  void clearPrepared();
  void describe(vp::JsonValue& obj) const;

 private:
  bool active_ = false;
  bool prepared_ = false;
  uint32_t highSinceMs_ = 0;
  uint32_t lastFallCount_ = 0;
  std::string preparedReason_;
};

extern PowerService g_power;
