#pragma once
#include <Arduino.h>

#include <vibproto.h>

// MicroSD on J603: mechanical card detect on A1 (low = inserted), SPI shared
// with PN532. Mounting happens only in the loop task.
class SdService {
 public:
  void begin();
  void poll();
  bool present() const { return present_; }
  bool mounted() const { return mounted_; }
  // PREPARE_POWER_OFF: unmount and stay unmounted until allowMount(true).
  void allowMount(bool allow);
  bool exists(const char* path) const;
  // GET_SD_INFO data (present, mounted, fs, total/free bytes, card type, assets).
  void describe(vp::JsonValue& obj) const;
  // Short status for GET_STATUS.
  void describeShort(vp::JsonValue& obj) const;

 private:
  void mount();
  void unmount(const char* why);
  void postState(const char* error);

  volatile bool present_ = false;
  volatile bool mounted_ = false;
  bool allowMount_ = true;
  bool rawLast_ = false;
  uint32_t rawSince_ = 0;
  uint32_t nextMountTry_ = 0;
  uint32_t mountFailures_ = 0;
};

extern SdService g_sd;
