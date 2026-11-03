#pragma once

#include <atomic>
#include <cstdint>
#include <mutex>

#include "app/config.h"
#include "app/state.h"
#include "drv/ds3231.h"
#include "drv/ina226.h"
#include "drv/tca9555.h"
#include "hw/i2c_bus.h"

namespace vm {

// Board management on I2C0: TCA9555 loads/inputs, INA226 power, DS3231.
// Thread-safe; the printer thread drives its own two TCA bits through here.
class BoardService {
 public:
  BoardService(I2cBus& bus, const AppConfig& cfg, StateStore& state);

  bool init();
  bool tcaReady() const { return tca_.initialized(); }

  bool setPrinterPower(bool on) { return setBit(tca::kPrnPwrPort, tca::kPrnPwrBit, on); }
  bool setPrinterComm(bool on) { return setBit(tca::kPrnComPort, tca::kPrnComBit, on); }
  bool setTofDisable(bool on) { return setBit(tca::kTofDisablePort, tca::kTofDisableBit, on); }
  bool setNanoReset(bool on) { return setBit(tca::kEspResetPort, tca::kEspResetBit, on); }
  bool setTower(bool red, bool yellow, bool green);
  bool setAlarmOutputs(bool red, bool yellow, bool green, bool buzzer);
  bool setBuzzer(bool on) { return setBit(tca::kAlmPort, tca::kBuzzBit, on); }
  bool setFan(bool on) { return setBit(tca::kAlmPort, tca::kFanBit, on); }
  bool fanOn() const { return (tca_.shadow(tca::kAlmPort) >> tca::kFanBit) & 1; }
  // Printer, tower light, buzzer and fan off (ToF and Nano reset untouched).
  bool allLoadsOff();
  // Serialize the inhibit latch with printer writes: an aborted worker cannot re-enable it.
  void inhibitPrinter(bool on);

  // EXP_INT_N: reads both input ports (releases /INT), LIGHT_OK and INA ALERT.
  void pollInputs();
  // 5 Hz power sampling against the warn/deny thresholds.
  void samplePower();
  bool heavyLoadAllowed() const { return !denyHeavy_.load(); }

  bool readRtc(int64_t& unixSec, bool& valid);
  bool writeRtc(int64_t unixSec);

 private:
  bool setBit(int port, int bit, bool on);

  I2cBus& bus_;
  const AppConfig& cfg_;
  StateStore& state_;
  Tca9555 tca_;
  Ina226 ina_;
  Ds3231 rtc_;
  bool inaOk_ = false;
  bool rtcOk_ = false;
  std::atomic<bool> denyHeavy_{false};
  int powerErrors_ = 0;
  std::mutex printerMutex_;
  bool printerInhibited_ = false;
};

}  // namespace vm
