#pragma once
#include <Arduino.h>

#include <string>

#include <vibproto.h>

// PN532 V3 module on J602 in SPI mode (mode 0, LSB first, 1 MHz).
// Polls ISO14443A tags with the RF field switched on only during a poll, so
// a tag that stays on the reader is reported once and its removal is seen.
// The SPI bus is held only for individual transfers; waiting for the PN532 to
// answer never blocks the SD card or the main loop.
class NfcService {
 public:
  void begin();
  void poll();
  void setEnabled(bool on);  // runtime diagnostic; restart restores enabled
  bool enabled() const { return enabled_; }
  bool ready() const { return ready_; }
  void describe(vp::JsonValue& obj) const;

 private:
  enum class State : uint8_t { kInit, kIdle, kRfSettle, kWaitList };

  bool init();
  bool writeCommand(uint8_t cmd, const uint8_t* params, size_t n);
  bool waitReady(uint32_t timeoutMs);
  bool readyNow();
  bool readAck();
  bool readResponse(uint8_t cmd, uint8_t* data, size_t& len);
  bool command(uint8_t cmd, const uint8_t* params, size_t n, uint8_t* resp, size_t& respLen, uint32_t timeoutMs);
  bool setRf(bool on);
  void abortCommand();
  void handleListResponse(const uint8_t* data, size_t len);
  void finishCycle(bool ok);
  void tagSeen(const std::string& uid, size_t uidLen, uint16_t atqa, uint8_t sak);
  void tagMissed();

  void spiBegin();
  void spiEnd();

  bool enabled_ = true;
  State state_ = State::kInit;
  bool ready_ = false;
  uint32_t nextActionMs_ = 0;
  uint32_t stateSinceMs_ = 0;
  uint32_t lastStatusPollMs_ = 0;
  uint32_t initFailures_ = 0;
  uint32_t cycleErrors_ = 0;
  uint32_t totalErrors_ = 0;
  char firmware_[16] = {0};

  bool tagPresent_ = false;
  std::string tagUid_;
  size_t tagUidLen_ = 0;
  uint32_t tagSinceMs_ = 0;
  uint32_t misses_ = 0;
};

extern NfcService g_nfc;
