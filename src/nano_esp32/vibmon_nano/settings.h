#pragma once
#include <Arduino.h>

#include <string>

// Persistent configuration in NVS (Preferences namespace "vibmon").
struct Settings {
  std::string wifiSsid;
  std::string wifiPassword;
  uint32_t linkBaud = 0;
  bool flowControl = true;
  std::string ntp1, ntp2, ntp3;
  // Access point for the phone app where there is no Wi-Fi to join.
  bool apEnabled = true;
  std::string apPassword;  // 8..63 characters (WPA2)


  void load();
  bool saveWifi(const std::string& ssid, const std::string& password);
  bool saveLink(uint32_t baud, bool flow);
  bool saveAp(bool enabled, const std::string& password);
};

extern Settings g_settings;
