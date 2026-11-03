// carrier_test - bring-up tool for the carrier board (stop vibmond first).
// Talks to the hardware directly; covers stages A/B/C of 分阶段联调与交付.md.
//
//   carrier_test probe                 device nodes, carrier_ctrl id/clock, inputs
//   carrier_test i2c-scan mgmt|sens    7-bit addresses that answer
//   carrier_test tca                   TCA9555 init (loads off) and inputs
//   carrier_test load <name> <0|1>     prn_pwr prn_com tof_off nano_reset red yellow green buzz fan
//   carrier_test power [n]             INA226 readings
//   carrier_test rtc [set]             read DS3231 / write system UTC into it
//   carrier_test leds RRGGBB x5        HL101..HL105
//   carrier_test pwm <pct0> <pct1>     light PWM (0 0 = off)
//   carrier_test events [s]            timestamped PL events (keys, encoder, DRDY, ...)
//   carrier_test imu | adxl [n]        identity + samples
//   carrier_test sht40 | tof
//   carrier_test nano [baud]           HELLO + GET_STATUS + DRAIN over the Nano UART
//   carrier_test printer [baud]        power sequence + one test strip
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "app/config.h"
#include "drv/adxl345.h"
#include "drv/carrier_ctrl.h"
#include "drv/ds3231.h"
#include "drv/escpos.h"
#include "drv/ina226.h"
#include "drv/lsm6dsv.h"
#include "drv/sht40.h"
#include "drv/tca9555.h"
#include "drv/vl53l1x.h"
#include "hw/i2c_bus.h"
#include "hw/serial_port.h"
#include "hw/spi_dev.h"
#include "hw/sysfs_find.h"
#include "hw/uio.h"
#include "link/nano_link.h"
#include "util/clock.h"

using namespace vm;
using vp::JsonValue;

namespace {

AppConfig g_cfg;

std::string pick(const std::string& explicitDev, const std::string& found) { return explicitDev.empty() ? found : explicitDev; }

bool openCarrier(UioDevice& uio, CarrierCtrl*& c) {
  const std::string dev = pick(g_cfg.carrierDev, findUioByBase(g_cfg.carrierBase));
  if (dev.empty() || !uio.open(dev, creg::kMapSize)) {
    printf("carrier_ctrl UIO not found\n");
    return false;
  }
  c = new CarrierCtrl(uio);
  uint32_t ver = 0;
  if (!c->probe(&ver)) {
    printf("%s: ID mismatch\n", dev.c_str());
    return false;
  }
  return true;
}

bool openI2c(LinuxI2cBus& bus, bool mgmt) {
  const std::string dev = mgmt ? pick(g_cfg.i2cMgmtDev, findI2cDevByBase(g_cfg.i2cMgmtBase))
                               : pick(g_cfg.i2cSensDev, findI2cDevByBase(g_cfg.i2cSensBase));
  if (dev.empty() || !bus.open(dev)) {
    printf("%s I2C not found\n", mgmt ? "management" : "sensor");
    return false;
  }
  return true;
}

int cmdProbe() {
  printf("carrier_uio    %s\n", pick(g_cfg.carrierDev, findUioByBase(g_cfg.carrierBase)).c_str());
  printf("i2c management %s\n", pick(g_cfg.i2cMgmtDev, findI2cDevByBase(g_cfg.i2cMgmtBase)).c_str());
  printf("i2c sensors    %s\n", pick(g_cfg.i2cSensDev, findI2cDevByBase(g_cfg.i2cSensBase)).c_str());
  printf("uart nano      %s\n", pick(g_cfg.uartNanoDev, findTtyByBase(g_cfg.uartNanoBase)).c_str());
  printf("uart printer   %s\n", pick(g_cfg.uartPrnDev, findTtyByBase(g_cfg.uartPrnBase)).c_str());
  printf("spi imu (cs0)  %s\n", pick(g_cfg.spiImuDev, findSpidevByBase(g_cfg.spiSensBase, 0)).c_str());
  printf("spi adxl (cs1) %s\n", pick(g_cfg.spiAdxlDev, findSpidevByBase(g_cfg.spiSensBase, 1)).c_str());
  UioDevice uio;
  CarrierCtrl* c = nullptr;
  if (!openCarrier(uio, c)) return 1;
  const uint64_t t0 = c->timestamp();
  sleepMs(100);
  const uint64_t t1 = c->timestamp();
  const uint32_t in = c->inputs();
  printf("carrier_ctrl ok, clock %u Hz, timestamp advanced %.1f ms in 100 ms\n", c->clockHz(),
         (t1 - t0) * 1000.0 / c->clockHz());
  printf("inputs 0x%04x: pfail_n=%u exp_int_n=%u esp_irq=%u esp_cts=%u prn_cts=%u k1=%u enc_sw=%u a=%u b=%u\n", in,
         (in >> 6) & 1, (in >> 7) & 1, (in >> 8) & 1, (in >> 10) & 1, (in >> 9) & 1, (in >> 11) & 1, (in >> 12) & 1,
         (in >> 13) & 1, (in >> 14) & 1);
  return 0;
}

int cmdScan(const char* which) {
  LinuxI2cBus bus;
  if (!openI2c(bus, strcmp(which, "sens") != 0)) return 1;
  for (int a = 0x03; a <= 0x77; ++a) {
    if (bus.probe(static_cast<uint8_t>(a))) printf("0x%02x answers\n", a);
  }
  return 0;
}

int cmdTca(bool report) {
  LinuxI2cBus bus;
  if (!openI2c(bus, true)) return 1;
  Tca9555 t(bus);
  if (!t.init(tca::kConfig0, tca::kConfig1)) {
    printf("TCA9555 init failed\n");
    return 1;
  }
  uint8_t p0, p1;
  t.readInputs(p0, p1);
  if (report) printf("TCA9555 ok, loads off; P0=0x%02x (LIGHT_OK=%d) P1=0x%02x (INA_ALERT=%d)\n", p0, p0 & 1, p1, !((p1 >> 6) & 1));
  return 0;
}

int cmdLoad(const char* name, int on) {
  LinuxI2cBus bus;
  if (!openI2c(bus, true)) return 1;
  Tca9555 t(bus);
  // Re-initialising clears every output; the tool sets exactly one load.
  if (!t.init(tca::kConfig0, tca::kConfig1)) return 1;
  struct {
    const char* n;
    int port, bit;
  } map[] = {{"prn_pwr", 0, 6}, {"prn_com", 0, 7}, {"tof_off", 0, 4}, {"nano_reset", 0, 5}, {"red", 1, 0},
             {"yellow", 1, 1}, {"green", 1, 2}, {"buzz", 1, 3}, {"fan", 1, 5}};
  for (const auto& m : map) {
    if (!strcmp(m.n, name)) {
      const bool ok = t.setBit(m.port, m.bit, on != 0);
      printf("P%d.%d = %d %s (other loads off)\n", m.port, m.bit, on, ok ? "ok" : "FAILED");
      return ok ? 0 : 1;
    }
  }
  printf("unknown load %s\n", name);
  return 2;
}

int cmdPower(int n) {
  LinuxI2cBus bus;
  if (!openI2c(bus, true)) return 1;
  Ina226 ina(bus);
  if (!ina.init(g_cfg.power.alertA)) {
    printf("INA226 init failed\n");
    return 1;
  }
  for (int i = 0; i < n; ++i) {
    sleepMs(100);
    PowerSample s;
    if (ina.read(s)) printf("%.3f V  %.4f A  %.2f W  (shunt %.3f mV)\n", s.busV, s.currentA, s.powerW, s.shuntMv);
  }
  return 0;
}

int cmdRtc(bool set) {
  LinuxI2cBus bus;
  if (!openI2c(bus, true)) return 1;
  Ds3231 r(bus);
  if (set) {
    if (!r.writeTime(realtimeMs() / 1000)) return 1;
    printf("RTC set to %s\n", formatUtcIso(realtimeMs()).c_str());
  }
  r.configure1Hz();
  int64_t sec;
  bool valid;
  double temp = 0;
  if (!r.readTime(sec, valid)) {
    printf("DS3231 not responding\n");
    return 1;
  }
  r.readTemperature(temp);
  printf("RTC %s UTC (%s), die %.2f C\n", formatUtcIso(sec * 1000).c_str(), valid ? "valid" : "OSF set / invalid", temp);
  return 0;
}

int cmdLeds(int argc, char** argv) {
  UioDevice uio;
  CarrierCtrl* c = nullptr;
  if (!openCarrier(uio, c)) return 1;
  uint32_t rgb[5] = {0, 0, 0, 0, 0};
  for (int i = 0; i < 5 && i < argc; ++i) rgb[i] = static_cast<uint32_t>(strtoul(argv[i], nullptr, 16));
  c->setLeds(rgb);
  c->sendLeds();
  printf("sent HL101..HL105 = %06x %06x %06x %06x %06x\n", rgb[0], rgb[1], rgb[2], rgb[3], rgb[4]);
  return 0;
}

int cmdPwm(double a, double b) {
  UioDevice uio;
  CarrierCtrl* c = nullptr;
  if (!openCarrier(uio, c)) return 1;
  c->setPwmFrequency(g_cfg.light.pwmHz);
  c->setPwmPercent(0, a);
  c->setPwmPercent(1, b);
  c->clearPwmKills();
  c->setPwmEnable(a > 0, b > 0);
  printf("PWM %.0f%% / %.0f%%, status 0x%02x (bit4 pfail kill, bit5 watchdog kill)\n", a, b, c->pwmStatus());
  return 0;
}

int cmdEvents(int seconds) {
  UioDevice uio;
  CarrierCtrl* c = nullptr;
  if (!openCarrier(uio, c)) return 1;
  c->configureEvents(0x1F07FF, 0x0307FF);
  c->clearEvents();
  static const char* names[] = {"IMU_INT1", "IMU_INT2", "ADXL_DRDY", "TOF_INT", "RTC_1HZ", "TACH", "PFAIL_N",
                                "EXP_INT_N", "ESP_IRQ", "PRN_CTS", "ESP_CTS"};
  const int64_t end = monoMs() + seconds * 1000LL;
  const double hz = c->clockHz();
  uint64_t first = 0;
  while (monoMs() < end) {
    std::vector<CarrierEvent> ev;
    c->popEvents(ev, 256);
    for (const auto& e : ev) {
      if (!first) first = e.ticks;
      const char* n = e.source < 11 ? names[e.source]
                      : e.source == 16 ? "K101" : e.source == 17 ? "ENC_SW" : e.source == 18 ? "K101_LONG"
                      : e.source == 19 ? "ENC_SW_LONG" : e.source == 20 ? "ENC_STEP" : "?";
      printf("%10.6f s  seq %5u  %-11s %s\n", (e.ticks - first) / hz, e.seq, n,
             e.source == 20 ? (e.level ? "up" : "down") : (e.level ? "rise" : "fall"));
    }
    sleepMs(20);
  }
  const PanelState p = c->panel();
  printf("encoder count %d, illegal transitions %u, dropped events %u\n", p.encCount, p.encErrors, c->droppedEvents());
  c->configureEvents(0, 0);
  return 0;
}

int cmdImu(int n) {
  LinuxSpiDevice spi;
  const std::string dev = pick(g_cfg.spiImuDev, findSpidevByBase(g_cfg.spiSensBase, 0));
  if (dev.empty() || !spi.open(dev, 0, g_cfg.imu.spiHz)) return 1;
  Lsm6dsv imu(spi);
  uint8_t id = 0;
  imu.whoAmI(id);
  printf("WHO_AM_I 0x%02x (expect 0x70)\n", id);
  Lsm6dsv::Config cfg;
  cfg.odrHz = g_cfg.imu.odrHz;
  if (!imu.init(cfg)) return 1;
  for (int i = 0; i < n; ++i) {
    sleepMs(50);
    ImuRaw r;
    if (imu.readRaw(r))
      printf("a %+.3f %+.3f %+.3f g   w %+7.2f %+7.2f %+7.2f dps   %.1f C\n", imu.accelG(r.ax), imu.accelG(r.ay),
             imu.accelG(r.az), imu.gyroDps(r.gx), imu.gyroDps(r.gy), imu.gyroDps(r.gz), Lsm6dsv::tempC(r.temp));
  }
  imu.powerDown();
  return 0;
}

int cmdAdxl(int n) {
  LinuxSpiDevice spi;
  const std::string dev = pick(g_cfg.spiAdxlDev, findSpidevByBase(g_cfg.spiSensBase, 1));
  if (dev.empty() || !spi.open(dev, 3, g_cfg.adxl.spiHz)) return 1;
  Adxl345 a(spi);
  uint8_t id = 0;
  a.devId(id);
  printf("DEVID 0x%02x (expect 0xE5)\n", id);
  if (!a.init(static_cast<uint8_t>(g_cfg.adxl.rateCode), g_cfg.adxl.rangeG)) return 1;
  for (int i = 0; i < n; ++i) {
    sleepMs(50);
    int16_t x, y, z;
    if (a.readXyz(x, y, z)) printf("%+.3f %+.3f %+.3f g\n", x * Adxl345::lsbToG(), y * Adxl345::lsbToG(), z * Adxl345::lsbToG());
  }
  a.standby();
  return 0;
}

int cmdSht() {
  LinuxI2cBus bus;
  if (!openI2c(bus, false)) return 1;
  Sht40 s(bus);
  uint32_t serial = 0;
  double t, rh;
  if (s.readSerial(serial)) printf("SHT40 serial %08x\n", serial);
  if (!s.measure(t, rh)) {
    printf("SHT40 measurement failed (address 0x44, CRC)\n");
    return 1;
  }
  printf("%.2f C  %.1f %%RH\n", t, rh);
  return 0;
}

int cmdTof() {
  LinuxI2cBus bus;
  if (!openI2c(bus, false)) return 1;
  Vl53l1x t(bus);
  uint16_t id = 0;
  if (!t.readModelId(id)) {
    printf("no answer at 0x29\n");
    return 1;
  }
  printf("model id 0x%04x (%s)\n", id, id == 0xEACC ? "VL53L1X" : "not VL53L1X");
  if (!Vl53l1x::driverAvailable()) {
    printf("ST ULD not built in: ranging unavailable\n");
    return 0;
  }
  if (!t.start(g_cfg.tof.timingBudgetMs, g_cfg.tof.periodMs, g_cfg.tof.longRange)) return 1;
  for (int i = 0; i < 20;) {
    TofReading r;
    bool ready = false;
    if (t.poll(r, ready) && ready) {
      printf("%5d mm  status %d %s  signal %d kcps\n", r.distanceMm, r.rangeStatus, r.valid ? "" : "(invalid)", r.signalKcps);
      ++i;
    }
    sleepMs(20);
  }
  t.stop();
  return 0;
}

class SerialCh : public ByteChannel {
 public:
  explicit SerialCh(SerialPort& p) : p_(p) {}
  ssize_t readSome(uint8_t* b, size_t n) override { return p_.readSome(b, n); }
  ssize_t writeSome(const uint8_t* b, size_t n) override { return p_.writeSome(b, n); }

 private:
  SerialPort& p_;
};

int cmdNano(uint32_t baud) {
  SerialPort port;
  const std::string dev = pick(g_cfg.uartNanoDev, findTtyByBase(g_cfg.uartNanoBase));
  if (dev.empty() || !port.open(dev, baud, g_cfg.nano.flowControl)) return 1;
  SerialCh ch(port);
  NanoLink::Config nc;
  nc.offlineResetMs = 0;
  NanoLink link(ch, nc);
  link.setEventHandler([](const NanoEvent& e) {
    printf("event %u %s %s\n", e.id, e.type.c_str(), vp::jsonSerialize(e.data).c_str());
  });
  const int64_t end = monoMs() + 5000;
  bool asked = false;
  while (monoMs() < end) {
    link.onReadable(monoMs());
    link.tick(monoMs());
    if (link.online() && !asked) {
      asked = true;
      printf("online: nano boot %s, hello %s\n", link.bootId().c_str(), vp::jsonSerialize(link.helloData()).c_str());
      link.request(vp::kOpGetStatus, JsonValue::makeObject(), [](const NanoReply& r) {
        printf("GET_STATUS %s %s\n", r.code.c_str(), vp::jsonSerialize(r.data).c_str());
      });
    }
    port.waitReadable(10);
  }
  const auto& s = link.stats();
  printf("requests %u retries %u timeouts %u events %u crc errors %u\n", s.requests, s.retries, s.timeouts, s.events,
         link.decoderStats().crcErrors);
  return link.online() ? 0 : 1;
}

int cmdPrinter(uint32_t baud) {
  LinuxI2cBus bus;
  if (!openI2c(bus, true)) return 1;
  Tca9555 t(bus);
  if (!t.init(tca::kConfig0, tca::kConfig1)) return 1;
  const std::string dev = pick(g_cfg.uartPrnDev, findTtyByBase(g_cfg.uartPrnBase));
  if (dev.empty()) return 1;
  printf("PRN_COM_EN=0, PRN_PWR_EN=1, waiting %d ms\n", g_cfg.printer.powerOnWaitMs);
  t.setBit(tca::kPrnComPort, tca::kPrnComBit, false);
  t.setBit(tca::kPrnPwrPort, tca::kPrnPwrBit, true);
  sleepMs(g_cfg.printer.powerOnWaitMs);
  SerialPort port;
  int rc = 1;
  if (port.open(dev, baud, true)) {
    printf("CTS %s\n", port.ctsAsserted() ? "ready (low)" : "busy (high)");
    port.flushInput();
    t.setBit(tca::kPrnComPort, tca::kPrnComBit, true);
    sleepMs(20);
    std::vector<uint8_t> bits(48 * 64, 0);
    for (int y = 0; y < 64; ++y) {
      for (int x = 0; x < 48; ++x) bits[y * 48 + x] = ((x / 4 + y / 16) & 1) ? 0xFF : 0x00;  // checker strip
    }
    std::string data = escpos::init();
    for (const auto& s : escpos::rasterImage(bits, 48, 64, 24)) data += s;
    data += escpos::feedLines(4);
    rc = port.writeAll(reinterpret_cast<const uint8_t*>(data.data()), data.size(), g_cfg.printer.busyTimeoutMs) ? 0 : 1;
    printf("sent %zu bytes %s\n", data.size(), rc == 0 ? "ok" : "FAILED (CTS stuck busy?)");
    sleepMs(3000);
  }
  t.setBit(tca::kPrnComPort, tca::kPrnComBit, false);
  port.close();
  t.setBit(tca::kPrnPwrPort, tca::kPrnPwrBit, false);
  printf("communication off, power off\n");
  return rc;
}

}  // namespace

int main(int argc, char** argv) {
  std::string err;
  loadConfig("/etc/vibmon/vibmon.json", g_cfg, err);
  if (argc < 2) {
    fprintf(stderr, "usage: %s probe|i2c-scan|tca|load|power|rtc|leds|pwm|events|imu|adxl|sht40|tof|nano|printer\n", argv[0]);
    return 2;
  }
  const std::string c = argv[1];
  if (c == "probe") return cmdProbe();
  if (c == "i2c-scan") return cmdScan(argc > 2 ? argv[2] : "mgmt");
  if (c == "tca") return cmdTca(true);
  if (c == "load" && argc > 3) return cmdLoad(argv[2], atoi(argv[3]));
  if (c == "power") return cmdPower(argc > 2 ? atoi(argv[2]) : 10);
  if (c == "rtc") return cmdRtc(argc > 2 && !strcmp(argv[2], "set"));
  if (c == "leds") return cmdLeds(argc - 2, argv + 2);
  if (c == "pwm" && argc > 3) return cmdPwm(atof(argv[2]), atof(argv[3]));
  if (c == "events") return cmdEvents(argc > 2 ? atoi(argv[2]) : 30);
  if (c == "imu") return cmdImu(argc > 2 ? atoi(argv[2]) : 10);
  if (c == "adxl") return cmdAdxl(argc > 2 ? atoi(argv[2]) : 10);
  if (c == "sht40") return cmdSht();
  if (c == "tof") return cmdTof();
  if (c == "nano") return cmdNano(argc > 2 ? static_cast<uint32_t>(atoi(argv[2])) : g_cfg.nano.baud);
  if (c == "printer") return cmdPrinter(argc > 2 ? static_cast<uint32_t>(atoi(argv[2])) : g_cfg.printer.baud);
  fprintf(stderr, "unknown or incomplete command %s\n", c.c_str());
  return 2;
}
