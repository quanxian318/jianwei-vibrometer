#include "nfc_service.h"

#include <SPI.h>

#include "app_config.h"
#include "board_pins.h"
#include "event_store.h"
#include "log.h"

NfcService g_nfc;

namespace {
const char* kTag = "nfc";

// PN532 SPI operation bytes (UM0701 6.2.5).
const uint8_t kSpiDataWrite = 0x01;
const uint8_t kSpiStatusRead = 0x02;
const uint8_t kSpiDataRead = 0x03;

const uint8_t kHostToPn = 0xD4;
const uint8_t kPnToHost = 0xD5;

const uint8_t kCmdGetFirmwareVersion = 0x02;
const uint8_t kCmdSamConfiguration = 0x14;
const uint8_t kCmdRfConfiguration = 0x32;
const uint8_t kCmdInListPassiveTarget = 0x4A;
const uint8_t kCmdInRelease = 0x52;

const uint8_t kAck[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};

const uint32_t kRfSettleMs = 10;
const uint32_t kListTimeoutMs = 200;
const uint32_t kMaxCycleErrors = 5;

const char* tagFamily(uint8_t sak) {
  switch (sak & 0x7F) {
    case 0x00: return "MIFARE Ultralight/NTAG";
    case 0x08: return "MIFARE Classic 1K";
    case 0x09: return "MIFARE Mini";
    case 0x18: return "MIFARE Classic 4K";
    case 0x20: return "ISO14443-4";
    case 0x28: return "MIFARE Classic 1K + ISO14443-4";
    case 0x38: return "MIFARE Classic 4K + ISO14443-4";
    default: return "ISO14443A";
  }
}

void toHex(const uint8_t* p, size_t n, std::string& out) {
  static const char kHex[] = "0123456789ABCDEF";
  out.clear();
  for (size_t i = 0; i < n; ++i) {
    out += kHex[p[i] >> 4];
    out += kHex[p[i] & 0x0F];
  }
}
}  // namespace

void NfcService::spiBegin() {
  SPI.beginTransaction(SPISettings(cfg::kPn532SpiHz, LSBFIRST, SPI_MODE0));
  digitalWrite(board_pins::PN_CS, LOW);
  delayMicroseconds(100);
}

void NfcService::spiEnd() {
  digitalWrite(board_pins::PN_CS, HIGH);
  SPI.endTransaction();
}

void NfcService::begin() {
  pinMode(board_pins::PN_IRQ, INPUT);  // 10k pull-up to 3V3_D on the carrier
  state_ = State::kInit;
  nextActionMs_ = millis() + 200;  // let the module's own regulator settle
}

bool NfcService::writeCommand(uint8_t cmd, const uint8_t* params, size_t n) {
  if (n > 32) return false;
  uint8_t frame[48];
  size_t i = 0;
  const uint8_t len = static_cast<uint8_t>(n + 2);
  frame[i++] = 0x00;
  frame[i++] = 0x00;
  frame[i++] = 0xFF;
  frame[i++] = len;
  frame[i++] = static_cast<uint8_t>(0x100 - len);
  frame[i++] = kHostToPn;
  frame[i++] = cmd;
  uint8_t sum = static_cast<uint8_t>(kHostToPn + cmd);
  for (size_t k = 0; k < n; ++k) {
    frame[i++] = params[k];
    sum = static_cast<uint8_t>(sum + params[k]);
  }
  frame[i++] = static_cast<uint8_t>(0x100 - sum);
  frame[i++] = 0x00;
  spiBegin();
  SPI.transfer(kSpiDataWrite);
  for (size_t k = 0; k < i; ++k) SPI.transfer(frame[k]);
  spiEnd();
  return true;
}

bool NfcService::readyNow() {
  if (digitalRead(board_pins::PN_IRQ) == LOW) return true;
  spiBegin();
  SPI.transfer(kSpiStatusRead);
  uint8_t status = SPI.transfer(0x00);
  spiEnd();
  return (status & 0x01) != 0;
}

bool NfcService::waitReady(uint32_t timeoutMs) {
  const uint32_t start = millis();
  for (;;) {
    if (readyNow()) return true;
    if (millis() - start >= timeoutMs) return false;
    delay(1);
  }
}

bool NfcService::readAck() {
  uint8_t buf[6];
  spiBegin();
  SPI.transfer(kSpiDataRead);
  for (int i = 0; i < 6; ++i) buf[i] = SPI.transfer(0x00);
  spiEnd();
  return memcmp(buf, kAck, sizeof(kAck)) == 0;
}

bool NfcService::readResponse(uint8_t cmd, uint8_t* data, size_t& len) {
  uint8_t buf[64];
  spiBegin();
  SPI.transfer(kSpiDataRead);
  for (size_t i = 0; i < sizeof(buf); ++i) buf[i] = SPI.transfer(0x00);
  spiEnd();
  // Locate the 00 FF start code; leading preamble bytes are optional.
  size_t s = 0;
  while (s + 1 < sizeof(buf) && !(buf[s] == 0x00 && buf[s + 1] == 0xFF)) ++s;
  if (s + 6 >= sizeof(buf)) return false;
  const uint8_t flen = buf[s + 2];
  if (static_cast<uint8_t>(flen + buf[s + 3]) != 0) return false;   // LCS
  if (flen < 2 || s + 5 + flen >= sizeof(buf)) return false;
  const uint8_t* body = buf + s + 4;  // TFI, CMD, data...
  uint8_t sum = 0;
  for (size_t i = 0; i < flen; ++i) sum = static_cast<uint8_t>(sum + body[i]);
  if (static_cast<uint8_t>(sum + body[flen]) != 0) return false;   // DCS
  if (body[0] != kPnToHost || body[1] != static_cast<uint8_t>(cmd + 1)) return false;
  const size_t n = flen - 2;
  if (n > len) return false;
  memcpy(data, body + 2, n);
  len = n;
  return true;
}

bool NfcService::command(uint8_t cmd, const uint8_t* params, size_t n, uint8_t* resp, size_t& respLen,
                         uint32_t timeoutMs) {
  writeCommand(cmd, params, n);
  if (!waitReady(10) || !readAck()) return false;
  if (!waitReady(timeoutMs)) {
    abortCommand();
    return false;
  }
  return readResponse(cmd, resp, respLen);
}

void NfcService::abortCommand() {
  // An ACK frame from the host aborts the command in progress (UM0701 6.2.1.4).
  spiBegin();
  SPI.transfer(kSpiDataWrite);
  for (size_t i = 0; i < sizeof(kAck); ++i) SPI.transfer(kAck[i]);
  spiEnd();
}

bool NfcService::setRf(bool on) {
  const uint8_t p[2] = {0x01, static_cast<uint8_t>(on ? 0x01 : 0x00)};
  uint8_t resp[8];
  size_t n = sizeof(resp);
  return command(kCmdRfConfiguration, p, sizeof(p), resp, n, 50);
}

bool NfcService::init() {
  // Wake from power-up: NSS low for >= 2 ms.
  SPI.beginTransaction(SPISettings(cfg::kPn532SpiHz, LSBFIRST, SPI_MODE0));
  digitalWrite(board_pins::PN_CS, LOW);
  delay(3);
  digitalWrite(board_pins::PN_CS, HIGH);
  SPI.endTransaction();
  delay(10);

  uint8_t resp[16];
  size_t n = 0;
  bool ok = false;
  for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
    n = sizeof(resp);
    ok = command(kCmdGetFirmwareVersion, nullptr, 0, resp, n, 50) && n >= 4;
  }
  if (!ok) return false;
  snprintf(firmware_, sizeof(firmware_), "%02X:%u.%u", resp[0], resp[1], resp[2]);

  const uint8_t sam[3] = {0x01, 0x14, 0x01};  // normal mode, 1 s virtual card timeout, IRQ enabled
  n = sizeof(resp);
  if (!command(kCmdSamConfiguration, sam, sizeof(sam), resp, n, 50)) return false;
  const uint8_t retries[4] = {0x05, 0xFF, 0x01, 0x02};  // MxRtyATR, MxRtyPSL, MxRtyPassiveActivation
  n = sizeof(resp);
  if (!command(kCmdRfConfiguration, retries, sizeof(retries), resp, n, 50)) return false;
  return setRf(false);
}

void NfcService::setEnabled(bool on) {
  if (enabled_ == on) return;
  if (!on) {
    // Finish the PN532 command before stopping its polling. SPI transactions
    // retain the core's shared-bus mutex, so the SD audio task stays serialized.
    if (ready_) {
      if (state_ == State::kWaitList) abortCommand();
      setRf(false);
    }
    digitalWrite(board_pins::PN_CS, HIGH);
    if (tagPresent_) {
      misses_ = cfg::kNfcRemoveMisses;
      tagMissed();
    }
  }
  enabled_ = on;
  ready_ = false;
  state_ = State::kInit;
  initFailures_ = cycleErrors_ = 0;
  nextActionMs_ = millis() + 200;
  LOGI(kTag, "polling %s", on ? "enabled" : "disabled (CS high)");
}

void NfcService::poll() {
  if (!enabled_) return;
  const uint32_t now = millis();
  switch (state_) {
    case State::kInit:
      if (static_cast<int32_t>(now - nextActionMs_) < 0) return;
      if (init()) {
        ready_ = true;
        initFailures_ = 0;
        cycleErrors_ = 0;
        LOGI(kTag, "PN532 ready, firmware %s", firmware_);
        state_ = State::kIdle;
        nextActionMs_ = now;
      } else {
        ready_ = false;
        initFailures_++;
        LOGW(kTag, "PN532 init failed (%lu)", static_cast<unsigned long>(initFailures_));
        if (initFailures_ == 1) {
          vp::JsonValue d = vp::JsonValue::makeObject();
          d.set("module", "nfc");
          d.set("code", "INIT_FAILED");
          g_events.post("FAULT", d);
        }
        nextActionMs_ = now + cfg::kNfcRetryInitMs;
      }
      return;
    case State::kIdle:
      if (static_cast<int32_t>(now - nextActionMs_) < 0) return;
      if (!setRf(true)) {
        finishCycle(false);
        return;
      }
      state_ = State::kRfSettle;
      stateSinceMs_ = now;
      return;
    case State::kRfSettle: {
      if (now - stateSinceMs_ < kRfSettleMs) return;
      const uint8_t p[2] = {0x01, 0x00};  // one target, 106 kbps type A
      writeCommand(kCmdInListPassiveTarget, p, sizeof(p));
      if (!waitReady(10) || !readAck()) {
        finishCycle(false);
        return;
      }
      state_ = State::kWaitList;
      stateSinceMs_ = now;
      lastStatusPollMs_ = now;
      return;
    }
    case State::kWaitList: {
      bool ready = digitalRead(board_pins::PN_IRQ) == LOW;
      if (!ready && now - lastStatusPollMs_ >= 5) {
        lastStatusPollMs_ = now;
        ready = readyNow();
      }
      if (ready) {
        uint8_t data[48];
        size_t n = sizeof(data);
        bool ok = readResponse(kCmdInListPassiveTarget, data, n);
        if (ok) handleListResponse(data, n);
        finishCycle(ok);
        return;
      }
      if (now - stateSinceMs_ > kListTimeoutMs) {
        abortCommand();
        finishCycle(false);
      }
      return;
    }
  }
}

void NfcService::handleListResponse(const uint8_t* data, size_t len) {
  // NbTg, Tg, SENS_RES(2), SEL_RES, NFCIDLength, NFCID1...
  if (len < 1 || data[0] == 0) {
    tagMissed();
    return;
  }
  if (len < 6) {
    tagMissed();
    return;
  }
  const uint16_t atqa = static_cast<uint16_t>((data[2] << 8) | data[3]);
  const uint8_t sak = data[4];
  const size_t uidLen = data[5];
  if (uidLen == 0 || uidLen > 10 || 6 + uidLen > len) {
    tagMissed();
    return;
  }
  std::string uid;
  toHex(data + 6, uidLen, uid);
  tagSeen(uid, uidLen, atqa, sak);
  const uint8_t rel[1] = {0x00};
  uint8_t resp[4];
  size_t n = sizeof(resp);
  command(kCmdInRelease, rel, sizeof(rel), resp, n, 30);
}

void NfcService::finishCycle(bool ok) {
  setRf(false);
  if (ok) {
    cycleErrors_ = 0;
  } else {
    cycleErrors_++;
    totalErrors_++;
    if (cycleErrors_ >= kMaxCycleErrors) {
      LOGW(kTag, "PN532 not responding, re-initialising");
      ready_ = false;
      vp::JsonValue d = vp::JsonValue::makeObject();
      d.set("module", "nfc");
      d.set("code", "NO_RESPONSE");
      g_events.post("FAULT", d);
      state_ = State::kInit;
      initFailures_ = 1;  // FAULT already reported for this outage
      nextActionMs_ = millis() + cfg::kNfcRetryInitMs;
      return;
    }
  }
  state_ = State::kIdle;
  nextActionMs_ = millis() + cfg::kNfcPollIntervalMs;
}

void NfcService::tagSeen(const std::string& uid, size_t uidLen, uint16_t atqa, uint8_t sak) {
  misses_ = 0;
  if (tagPresent_ && uid == tagUid_) return;  // same tag still on the reader
  if (tagPresent_) {
    // Another tag replaced the previous one: report the removal first.
    vp::JsonValue d = vp::JsonValue::makeObject();
    d.set("uid", tagUid_);
    d.set("uid_len", static_cast<unsigned>(tagUidLen_));
    d.set("present_ms", static_cast<unsigned long>(millis() - tagSinceMs_));
    g_events.post("NFC_REMOVED", d);
  }
  tagPresent_ = true;
  tagUid_ = uid;
  tagUidLen_ = uidLen;
  tagSinceMs_ = millis();
  misses_ = 0;
  char hex[8];
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("uid", uid);
  d.set("uid_len", static_cast<unsigned>(uidLen));
  d.set("type", "ISO14443A");
  snprintf(hex, sizeof(hex), "%04X", atqa);
  d.set("atqa", hex);
  snprintf(hex, sizeof(hex), "%02X", sak);
  d.set("sak", hex);
  d.set("family", tagFamily(sak));
  g_events.post("NFC_TAG", d);
  LOGI(kTag, "tag %s (%s)", uid.c_str(), tagFamily(sak));
}

void NfcService::tagMissed() {
  if (!tagPresent_) return;
  if (++misses_ < cfg::kNfcRemoveMisses) return;
  vp::JsonValue d = vp::JsonValue::makeObject();
  d.set("uid", tagUid_);
  d.set("uid_len", static_cast<unsigned>(tagUidLen_));
  d.set("present_ms", static_cast<unsigned long>(millis() - tagSinceMs_));
  g_events.post("NFC_REMOVED", d);
  LOGI(kTag, "tag %s removed", tagUid_.c_str());
  tagPresent_ = false;
  tagUid_.clear();
  tagUidLen_ = 0;
  misses_ = 0;
}

void NfcService::describe(vp::JsonValue& obj) const {
  obj.set("enabled", enabled_);
  obj.set("ready", ready_);
  obj.set("firmware", firmware_[0] ? vp::JsonValue(firmware_) : vp::JsonValue());
  obj.set("tag_present", tagPresent_);
  obj.set("uid", tagPresent_ ? vp::JsonValue(tagUid_) : vp::JsonValue());
  obj.set("errors", static_cast<unsigned long>(totalErrors_));
}
