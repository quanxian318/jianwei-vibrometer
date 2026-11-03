#include <Arduino.h>
#include <SPI.h>
#include <cstdio>
#include "nfc_service.h"
#include "app_config.h"
#include "board_pins.h"
#include "event_store.h"
#include "log.h"
TestSPI SPI;
TestSerial Serial, Serial0;
uint32_t testMillis = 0;
int testPins[64];
EventStore g_events;
uint32_t EventStore::post(const char*, const vp::JsonValue&) { return 1; }
void logPrintf(LogLevel, const char*, const char*, ...) {}
int main() {
  for (auto& pin : testPins) pin = HIGH;
  NfcService missing;
  missing.begin(); missing.setEnabled(false);
  testMillis = 10000; missing.poll();
  assert(SPI.transactions == 0 && !missing.enabled() && !missing.ready());
  assert(testPins[board_pins::PN_CS] == HIGH);
  missing.setEnabled(true); missing.poll();
  assert(SPI.transactions == 0);  // fresh power-up settling interval
  testMillis += 200; missing.poll();
  assert(SPI.transactions > 0 && !missing.ready());
  missing.setEnabled(false);
  auto count = SPI.transactions;
  for (int i = 0; i < 20; ++i) { testMillis += 5000; missing.poll(); }
  assert(SPI.transactions == count);  // failed init must stop retrying
  SPI.responding = true;
  NfcService present; present.begin(); testMillis += 200; present.poll();
  assert(present.ready());
  vp::JsonValue status = vp::JsonValue::makeObject(); present.describe(status);
  bool enabled = false; assert(status.getBool("enabled", enabled) && enabled);
  present.poll();  // enable RF, then leave a command pending
  testMillis += 10; present.poll();
  present.setEnabled(false);
  assert(!present.ready() && !present.enabled() && !SPI.locked);
  assert(testPins[board_pins::PN_CS] == HIGH);
  assert(testPins[board_pins::SD_CS] == HIGH);  // never drive the other CS
  count = SPI.transactions;
  testMillis += 10000; present.poll(); assert(SPI.transactions == count);
  present.setEnabled(true); testMillis += 200; present.poll();
  assert(present.ready() && present.enabled());
  assert(findVoiceAsset("baseline_done"));
  assert(!findVoiceAsset("unknown_asset"));
  std::puts("nfc control: disable before init, stop retry, pending command, resume, CS and baseline asset passed");
}
