#pragma once
#include <Arduino.h>

#include <string>

#include <vibproto.h>

// Station-mode Wi-Fi with bounded timeouts and exponential back-off, plus
// SNTP. Nothing here blocks the loop; state changes become WIFI_STATE and
// TIME_SYNC events. "Connected to an AP" is reported separately from "time
// valid" because an AP without internet still lets the station associate.
class NetService {
 public:
  void begin();
  void poll();
  // SET_NET: empty ssid switches Wi-Fi off.
  void configure(const std::string& ssid, const std::string& password);
  // Access point for the phone app (kept on beside the station when enabled).
  void configureAp(bool enabled, const std::string& password);
  const std::string& apSsid() const { return apSsid_; }
  void describeAp(vp::JsonValue& obj) const;
  // Asynchronous scan for the app's network list: starts one when none is
  // running and returns the last results ("scanning" while it runs).
  void scan(vp::JsonValue& obj);
  bool timeValid() const { return timeValid_; }
  void describe(vp::JsonValue& obj) const;  // GET_STATUS "wifi"
  void timeInfo(vp::JsonValue& obj) const;  // GET_TIME data

 private:
  enum class State : uint8_t { kOff, kConnecting, kConnected, kWaitRetry };
  void startConnect();
  void enter(State s, const char* reason);
  void startSntp();
  void startAp();
  static const char* stateName(State s);

  State state_ = State::kOff;
  std::string ssid_;
  std::string password_;
  uint32_t deadlineMs_ = 0;
  uint32_t retryAtMs_ = 0;
  uint32_t backoffMs_ = 0;
  uint32_t attempts_ = 0;
  bool sntpStarted_ = false;
  bool apOn_ = false;
  std::string apSsid_;
  bool timeValid_ = false;
  int64_t lastSyncUptimeMs_ = 0;
};

extern NetService g_net;
