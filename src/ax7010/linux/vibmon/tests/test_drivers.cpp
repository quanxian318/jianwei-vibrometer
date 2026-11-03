// Chip drivers against register-level fakes: write order, encodings, scaling.
#include <map>
#include <cmath>
#include <string>
#include <vector>

#include "app/board_service.h"
#include "app/panel_service.h"
#include "app/alarm_service.h"
#include "drv/adxl345.h"
#include "drv/carrier_ctrl.h"
#include "drv/ds3231.h"
#include "drv/escpos.h"
#include "drv/ina226.h"
#include "drv/lsm6dsv.h"
#include "drv/sht40.h"
#include "drv/tca9555.h"
#include "test_util.h"
#include "util/clock.h"

using namespace vm;

namespace {

// Byte-register device model: first written byte selects the register,
// following bytes auto-increment; reads continue from the selected register.
struct FakeI2c : I2cBus {
  bool fail = false;
  std::map<uint8_t, std::vector<uint8_t>> regs;  // addr -> 256 bytes
  std::vector<std::pair<uint8_t, std::vector<uint8_t>>> writes;
  std::map<uint8_t, uint8_t> ptr;
  std::map<uint8_t, std::vector<uint8_t>> rawRead;  // devices answering a fixed byte stream (SHT40)
  bool transfer(uint8_t addr, const uint8_t* w, size_t wl, uint8_t* r, size_t rl) override {
    if (fail || (!regs.count(addr) && !rawRead.count(addr))) return false;
    auto& m = regs[addr];
    if (m.empty()) m.assign(256, 0);
    if (wl) {
      writes.emplace_back(addr, std::vector<uint8_t>(w, w + wl));
      ptr[addr] = w[0];
      for (size_t i = 1; i < wl; ++i) m[static_cast<uint8_t>(w[0] + i - 1)] = w[i];
    }
    if (rl) {
      if (rawRead.count(addr)) {
        for (size_t i = 0; i < rl && i < rawRead[addr].size(); ++i) r[i] = rawRead[addr][i];
      } else {
        for (size_t i = 0; i < rl; ++i) r[i] = m[static_cast<uint8_t>(ptr[addr] + i)];
      }
    }
    return true;
  }
};

// INA226-style device: 16-bit big-endian registers addressed by a pointer byte.
struct FakeI2c16 : I2cBus {
  std::map<uint8_t, uint16_t> regs;
  std::vector<std::vector<uint8_t>> writes;
  uint8_t ptr = 0;
  bool transfer(uint8_t, const uint8_t* w, size_t wl, uint8_t* r, size_t rl) override {
    if (wl) {
      writes.emplace_back(w, w + wl);
      ptr = w[0];
      if (wl == 3) regs[ptr] = static_cast<uint16_t>((w[1] << 8) | w[2]);
    }
    if (rl == 2) {
      r[0] = static_cast<uint8_t>(regs[ptr] >> 8);
      r[1] = static_cast<uint8_t>(regs[ptr]);
    }
    return true;
  }
};

void testTca() {
  FakeI2c bus;
  bus.regs[0x20].assign(256, 0xFF);
  Tca9555 t(bus);
  CHECK(t.init(0x0F, 0xD0));
  // Order: outputs, polarity, configuration.
  CHECK(bus.writes.size() >= 3);
  CHECK(bus.writes[0].second == std::vector<uint8_t>({0x02, 0x00, 0x00}));
  CHECK(bus.writes[1].second == std::vector<uint8_t>({0x04, 0x00, 0x00}));
  CHECK(bus.writes[2].second == std::vector<uint8_t>({0x06, 0x0F, 0xD0}));
  CHECK(t.setBit(tca::kPrnPwrPort, tca::kPrnPwrBit, true));
  CHECK(t.setBit(tca::kAlmPort, tca::kFanBit, true));
  CHECK(t.setBit(tca::kPrnComPort, tca::kPrnComBit, true));
  CHECK(t.setBit(tca::kPrnPwrPort, tca::kPrnPwrBit, false));
  CHECK(t.shadow(0) == 0x80);
  CHECK(t.shadow(1) == 0x20);
  CHECK(bus.regs[0x20][0x02] == 0x80 && bus.regs[0x20][0x03] == 0x20);
}

void testPrinterInhibit() {
  FakeI2c bus;
  bus.regs[0x20].assign(256, 0xFF);
  AppConfig cfg;
  StateStore state;
  BoardService board(bus, cfg, state);
  CHECK(board.init());
  // An unplugged/stuck management bus must not leave an old "healthy" flag.
  const auto tcaRegisters = bus.regs[0x20];
  bus.regs.erase(0x20);
  board.pollInputs();
  CHECK(!state.snapshot().tcaOk && !state.snapshot().lightAllowed);
  bus.regs[0x20] = tcaRegisters;
  board.pollInputs();
  CHECK(state.snapshot().tcaOk && state.snapshot().lightAllowed);
  CHECK(board.setPrinterPower(true));
  CHECK(board.setPrinterComm(true));
  board.inhibitPrinter(true);
  CHECK((bus.regs[0x20][2] & 0xC0) == 0);
  CHECK(!board.setPrinterPower(true));
  CHECK(!board.setPrinterComm(true));
  CHECK(board.setPrinterComm(false));
  CHECK(board.setPrinterPower(false));
  board.inhibitPrinter(false);
  CHECK(board.setPrinterPower(true));
  AlarmService alarm(board, cfg.alarm);
  CHECK(alarm.level() == AlarmLevel::kUnknown);
  alarm.tick(0);
  CHECK((bus.regs[0x20][3] & 0x0F) == 0);
  alarm.setLevel(AlarmLevel::kAlarm, 10);
  alarm.tick(10);
  CHECK((bus.regs[0x20][3] & 0x0F) == 0x09);
  alarm.mute();
  alarm.tick(20);
  CHECK((bus.regs[0x20][3] & 0x0F) == 0x01);  // muting keeps the red lamp
  alarm.mute(false);
  alarm.tick(1010);
  CHECK((bus.regs[0x20][3] & 0x0F) == 0x09);  // unmuted: the buzzer sounds again
}

void testAlarmConsistency() {
  FakeI2c bus;bus.regs[0x20].assign(256,0);
  AppConfig cfg;StateStore state;BoardService board(bus,cfg,state);
  board.init();cfg.alarm.binaryThreshold=true;cfg.alarm.warnPp=.01;cfg.alarm.alarmPp=.1;
  AlarmService alarm(board,cfg.alarm);
  ResultSummary r;r.have=r.valid=true;r.unit="mm";r.method="multipoint";r.peakToPeak=.2;
  auto apply = [&](int64_t t) { alarm.setLevel(AlarmService::classifyResult(r,cfg.alarm),t);alarm.tick(t); };
  apply(0);CHECK(alarm.level()==AlarmLevel::kAlarm);CHECK((bus.regs[0x20][3]&7)==1);
  cfg.alarm.alarmPp=.3;apply(100);CHECK(alarm.level()==AlarmLevel::kNormal);CHECK((bus.regs[0x20][3]&7)==4);
  cfg.alarm.alarmPp=.1;apply(200);CHECK(alarm.level()==AlarmLevel::kAlarm);
  r.waveformValid=false;apply(300);CHECK(alarm.level()==AlarmLevel::kUnknown);CHECK((bus.regs[0x20][3]&15)==0);
  r.waveformValid=true;r.valid=false;apply(400);CHECK(alarm.level()==AlarmLevel::kUnknown);
  r.valid=true;r.unit="px";apply(500);CHECK(alarm.level()==AlarmLevel::kUnknown);
  r.unit="mm";r.peakToPeak=NAN;apply(600);CHECK(alarm.level()==AlarmLevel::kUnknown);
  r.peakToPeak=.1;apply(700);CHECK(alarm.level()==AlarmLevel::kNormal); // strictly exceeds
  r.peakToPeak=.2;apply(800);CHECK(alarm.outputsOk());
  alarm.mute();alarm.tick(801);CHECK((bus.regs[0x20][3]&15)==1); // only buzzer muted
  board.setFan(true);board.allLoadsOff(); // another service clears hardware after lastOut cached
  alarm.tick(1801);CHECK((bus.regs[0x20][3]&15)==1);CHECK(alarm.outputsOk());
  bus.fail=true;alarm.setLevel(AlarmLevel::kNormal,1900);alarm.tick(1900);CHECK(!alarm.outputsOk());
  bus.fail=false;alarm.tick(1901);CHECK(alarm.outputsOk());CHECK((bus.regs[0x20][3]&15)==4);
  bus.regs[0x20][7]|=1;alarm.tick(3001);CHECK(!alarm.outputsOk()); // direction readback catches reset
  bus.regs[0x20][7]&=~1;alarm.tick(3002);CHECK(alarm.outputsOk());
}

void testIna() {
  FakeI2c16 bus;
  bus.regs[0xFE] = 0x5449;
  bus.regs[0xFF] = 0x2260;
  Ina226 ina(bus);
  CHECK(ina.calibration() == 4096);
  CHECK(ina.init(4.5));
  CHECK(bus.regs[0x00] == 0x4527);  // AVG 16, 1.1 ms, continuous
  CHECK(bus.regs[0x05] == 4096);
  CHECK(bus.regs[0x07] == 9000);    // 4.5 A * 5 mOhm / 2.5 uV
  CHECK(bus.regs[0x06] == 0x8001);  // SOL alert, latched
  bus.regs[0x01] = 8400;   // 21 mV shunt
  bus.regs[0x02] = 9600;   // 12.0 V
  bus.regs[0x04] = 16800;  // 4.2 A
  bus.regs[0x03] = 8064;   // 50.4 W / (25 * 0.25 mA)
  PowerSample s;
  CHECK(ina.read(s));
  CHECK_NEAR(s.busV, 12.0, 1e-9);
  CHECK_NEAR(s.currentA, 4.2, 1e-9);
  CHECK_NEAR(s.shuntMv, 21.0, 1e-9);
  CHECK_NEAR(s.powerW, 50.4, 1e-9);
  bus.regs[0x04] = static_cast<uint16_t>(-400);  // reverse current is signed
  CHECK(ina.read(s));
  CHECK_NEAR(s.currentA, -0.1, 1e-9);
  bus.regs[0x06] = 0x8011;  // AFF set
  bool oc = false;
  CHECK(ina.readAlert(oc) && oc);
}

void testDs3231() {
  FakeI2c bus;
  bus.regs[0x68].assign(256, 0);
  Ds3231 rtc(bus);
  const int64_t t = 1792454400 + 3600 * 13 + 60 * 7 + 9;  // 2026-10-20 13:07:09 UTC
  CHECK(rtc.writeTime(t));
  auto& m = bus.regs[0x68];
  CHECK(m[0] == 0x09 && m[1] == 0x07 && m[2] == 0x13 && m[4] == 0x20 && m[5] == 0x10 && m[6] == 0x26);
  int64_t back = 0;
  bool valid = false;
  CHECK(rtc.readTime(back, valid));
  CHECK(valid && back == t);
  m[0x0F] = 0x80;  // oscillator stopped
  CHECK(rtc.readTime(back, valid) && !valid);
  m[0x0F] = 0x00;
  m[2] = 0x40 | 0x20 | 0x12;  // 12-hour mode, 12 PM
  CHECK(rtc.readTime(back, valid));
  CHECK(civilFromUnix(back).hour == 12);
  m[0x11] = 0x19;
  m[0x12] = 0x40;
  double temp = 0;
  CHECK(rtc.readTemperature(temp));
  CHECK_NEAR(temp, 25.25, 1e-9);
}

void testSht40() {
  const uint8_t ex[2] = {0xBE, 0xEF};
  CHECK(Sht40::crc8(ex, 2) == 0x92);  // datasheet example
  FakeI2c bus;
  uint8_t t[2] = {0x66, 0x66}, rh[2] = {0x80, 0x00};
  bus.rawRead[0x44] = {t[0], t[1], Sht40::crc8(t, 2), rh[0], rh[1], Sht40::crc8(rh, 2)};
  Sht40 s(bus);
  double tc = 0, h = 0;
  CHECK(s.measure(tc, h));
  CHECK_NEAR(tc, -45 + 175.0 * 0x6666 / 65535.0, 1e-9);
  CHECK_NEAR(h, -6 + 125.0 * 0x8000 / 65535.0, 1e-9);
  bus.rawRead[0x44][5] ^= 1;  // corrupt CRC
  CHECK(!s.measure(tc, h));
}

struct FakeSpi : SpiDevice {
  std::map<uint8_t, uint8_t> regs;
  std::vector<std::vector<uint8_t>> log;
  bool lsmStyle = true;  // LSM6DSV: bit7 read, auto-increment; ADXL345: bit7 read, bit6 multi
  bool transfer(const uint8_t* tx, uint8_t* rx, size_t len) override {
    log.emplace_back(tx, tx + len);
    const bool read = tx[0] & 0x80;
    const uint8_t reg = lsmStyle ? (tx[0] & 0x7F) : (tx[0] & 0x3F);
    rx[0] = 0;
    for (size_t i = 1; i < len; ++i) {
      const uint8_t r = static_cast<uint8_t>(reg + i - 1);
      if (read) {
        rx[i] = regs[r];
      } else {
        regs[r] = tx[i];
        if (lsmStyle && r == 0x12) regs[r] &= static_cast<uint8_t>(~0x01);  // SW_RESET self-clears
      }
    }
    return true;
  }
};

void testLsm6dsv() {
  FakeSpi spi;
  spi.regs[0x0F] = 0x70;
  Lsm6dsv imu(spi);
  Lsm6dsv::Config c;
  c.odrHz = 240;
  c.accelRangeG = 4;
  c.gyroRangeDps = 500;
  CHECK(imu.init(c));
  CHECK(spi.regs[0x03] == 0x01);  // I2C/I3C disabled, push-pull, active high
  CHECK(spi.regs[0x12] == 0x44);  // BDU | IF_INC
  CHECK(spi.regs[0x13] == 0x02);  // DRDY pulsed
  CHECK(spi.regs[0x15] == 0x02);  // +-500 dps
  CHECK(spi.regs[0x17] == 0x01);  // +-4 g
  CHECK(spi.regs[0x0D] == 0x01 && spi.regs[0x0E] == 0x02);
  CHECK(spi.regs[0x10] == 0x07 && spi.regs[0x11] == 0x07);
  CHECK(imu.odrHz() == 240);
  // 1 g on Z at +-4 g: 1 / 0.122 mg = 8197 LSB.
  spi.regs[0x2C] = 8197 & 0xFF;
  spi.regs[0x2D] = 8197 >> 8;
  spi.regs[0x20] = 0x00;
  spi.regs[0x21] = 0x01;  // +1 C
  ImuRaw r;
  CHECK(imu.readRaw(r));
  CHECK_NEAR(imu.accelG(r.az), 1.0, 0.001);
  CHECK_NEAR(Lsm6dsv::tempC(r.temp), 26.0, 1e-9);
  CHECK(spi.log.back()[0] == 0xA0);  // burst from OUT_TEMP_L
  int actual = 0;
  CHECK(Lsm6dsv::odrCode(200, actual) == 0x7 && actual == 240);
}

void testAdxl345() {
  FakeSpi spi;
  spi.lsmStyle = false;
  spi.regs[0x00] = 0xE5;
  Adxl345 a(spi);
  CHECK(a.init(0x0B, 2));
  CHECK(spi.regs[0x31] == 0x08);  // full resolution, +-2 g, 4-wire, INT active high
  CHECK(spi.regs[0x2C] == 0x0B && spi.regs[0x2D] == 0x08 && spi.regs[0x2E] == 0x80 && spi.regs[0x2F] == 0x00);
  spi.regs[0x36] = 0x00;
  spi.regs[0x37] = 0x01;  // z = 256 LSB = 1 g
  int16_t x, y, z;
  CHECK(a.readXyz(x, y, z));
  CHECK(z == 256);
  CHECK(spi.log.back()[0] == (0x80 | 0x40 | 0x32));
  CHECK_NEAR(Adxl345::rateHz(0x0B), 200, 1e-9);
}

struct FakeRegs : RegIo {
  std::map<uint32_t, uint32_t> r;
  std::vector<uint32_t> fifoInfo;
  size_t head = 0;
  uint32_t read32(uint32_t off) override {
    if (off == creg::kEvtStatus) return static_cast<uint32_t>(fifoInfo.size() - head);
    if (off == creg::kEvtInfo) return head < fifoInfo.size() ? fifoInfo[head] : 0;
    if (off == creg::kEvtTsLo) return static_cast<uint32_t>(1000 + head);
    if (off == creg::kEvtTsHi) return 2;
    return r[off];
  }
  void write32(uint32_t off, uint32_t v) override {
    if (off == creg::kEvtPop) {
      ++head;
      return;
    }
    r[off] = v;
  }
};

void testCarrierCtrl() {
  FakeRegs regs;
  regs.r[creg::kId] = creg::kIdValue;
  regs.r[creg::kClkHz] = 100000000;
  regs.r[creg::kPwmPeriod] = 5000;
  regs.r[creg::kPwmCtrl] = 0x30;
  CarrierCtrl c(regs);
  CHECK(c.probe());
  c.setPwmFrequency(20000);
  CHECK(regs.r[creg::kPwmPeriod] == 5000);
  c.setPwmPercent(0, 25);
  c.setPwmPercent(1, 100);
  CHECK(regs.r[creg::kPwmDuty0] == 1250 && regs.r[creg::kPwmDuty1] == 5000);
  c.setPwmEnable(true, false);
  CHECK(regs.r[creg::kPwmCtrl] == 0x31);  // kills stay enabled
  const uint32_t rgb[5] = {0xFF0000, 0x00FF00, 0, 0, 0x123456};
  c.setLeds(rgb);
  CHECK(regs.r[creg::kWsColor0] == 0xFF0000 && regs.r[creg::kWsColor0 + 16] == 0x123456);
  regs.fifoInfo = {(1u << 16) | 0x100 | kSrcKey, (2u << 16) | kSrcKey, (3u << 16) | 0x100 | kSrcEncStep};
  std::vector<CarrierEvent> ev;
  CHECK(c.popEvents(ev, 16) == 3);
  CHECK(ev[0].source == kSrcKey && ev[0].level && ev[0].seq == 1);
  CHECK(ev[1].source == kSrcKey && !ev[1].level);
  CHECK(ev[2].source == kSrcEncStep && ev[2].level);
  CHECK(ev[0].ticks == ((2ULL << 32) | 1000));
  AppConfig cfg;
  PanelService panel(c, cfg.panel);
  uint32_t leds[5] = {0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF,0xFFFFFFFF};
  panel.tick(0);
  leds[4] = regs.r[creg::kWsColor0 + 16];
  CHECK(leds[4] == 0);  // no random colour or normal judgment before the first valid mm result
  int zoomSteps = 0, resets = 0, longs = 0;
  PanelService::Callbacks callbacks;
  callbacks.encoderSteps = [&](int n) { zoomSteps += n; };
  callbacks.encoderPress = [&]() { ++resets; };
  callbacks.encoderLong = [&]() { ++longs; };
  panel.setCallbacks(callbacks);
  CarrierEvent e;
  e.source = kSrcEncStep; e.level = true;
  panel.onEvent(e); CHECK(zoomSteps == 0);
  panel.onEvent(e); CHECK(zoomSteps == 1);
  e.level = false; panel.onEvent(e); panel.onEvent(e); CHECK(zoomSteps == 0);
  e.source = kSrcEncSw; e.level = true; panel.onEvent(e);
  e.level = false; panel.onEvent(e); CHECK(resets == 1);
  e.level = true; panel.onEvent(e);
  e.source = kSrcEncSwLong; panel.onEvent(e);
  e.source = kSrcEncSw; e.level = false; panel.onEvent(e);
  CHECK(resets == 1);  // a long press must not also act as a short press
  CHECK(longs == 1);


}

void testEscpos() {
  CHECK(escpos::feedDots(0).empty() && escpos::feedDots(-1).empty());
  const std::string tail = escpos::feedDots(320);
  CHECK(tail.size() == 6);
  CHECK(tail[0] == 0x1B && tail[1] == 0x4A && static_cast<uint8_t>(tail[2]) == 255);
  CHECK(tail[3] == 0x1B && tail[4] == 0x4A && static_cast<uint8_t>(tail[5]) == 65);
  CHECK(escpos::feedDots(255).size() == 3 && escpos::feedDots(256).size() == 6);
  CHECK(escpos::feedDots(5000) == escpos::feedDots(4096));
  std::vector<uint8_t> bits(48 * 30, 0xAA);
  auto strips = escpos::rasterImage(bits, 48, 30, 24);
  CHECK(strips.size() == 2);
  CHECK(strips[0].size() == 8 + 48 * 24 && strips[1].size() == 8 + 48 * 6);
  CHECK(static_cast<uint8_t>(strips[0][0]) == 0x1D && strips[0][1] == 0x76 && strips[0][2] == 0x30);
  CHECK(static_cast<uint8_t>(strips[0][4]) == 48 && strips[0][5] == 0 && static_cast<uint8_t>(strips[0][6]) == 24);
  CHECK(escpos::parseDleEot4(0x60).paperOut && !escpos::parseDleEot4(0x12).paperOut);
  CHECK(escpos::parseGsR1(0x0C).paperOut && escpos::parseGsR1(0x03).nearEnd);
}

void testClock() {
  CivilTime c = civilFromUnix(1792454400);  // 2026-10-20 00:00:00 UTC, Tuesday
  CHECK(c.year == 2026 && c.month == 10 && c.day == 20 && c.hour == 0 && c.weekday == 2);
  CHECK(unixFromCivil(c) == 1792454400);
  CHECK(formatUtcIso(1792454400123LL) == "2026-10-20T00:00:00.123Z");
  CHECK(formatLocal(1792454400000LL, 480, "%Y-%m-%d %H:%M") == "2026-10-20 08:00");
  CivilTime far = civilFromUnix(4102444800LL);  // 2100-01-01, beyond 32-bit time_t
  CHECK(far.year == 2100 && far.month == 1 && far.day == 1);
}

}  // namespace

int main() {
  testTca();
  testPrinterInhibit();
  testAlarmConsistency();
  testIna();
  testDs3231();
  testSht40();
  testLsm6dsv();
  testAdxl345();
  testCarrierCtrl();
  testEscpos();
  testClock();
  return testSummary("drivers");
}
