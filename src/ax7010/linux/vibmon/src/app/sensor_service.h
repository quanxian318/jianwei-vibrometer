#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "app/config.h"
#include "app/state.h"
#include "drv/adxl345.h"
#include "drv/carrier_ctrl.h"
#include "drv/lsm6dsv.h"
#include "drv/sht40.h"
#include "drv/vl53l1x.h"
#include "hw/i2c_bus.h"
#include "hw/spi_dev.h"

namespace vm {

// Maps PL timestamp ticks onto CLOCK_MONOTONIC. The main loop feeds paired
// readings; the offset is the median of recent pairs (the PL counter runs
// from FCLK, the A9 from its own PLL, so it is refreshed continuously).
class TickClock {
 public:
  void setHz(uint32_t hz) { hz_ = hz; }
  void addPair(uint64_t ticks, int64_t monoNs);
  int64_t toMonoNs(uint64_t ticks) const;
  bool ready() const;

 private:
  mutable std::mutex mutex_;
  uint32_t hz_ = 100000000;
  std::deque<int64_t> offsets_;
};

struct ImuSample {
  int64_t monoNs = 0;
  float ax = 0, ay = 0, az = 0;  // g
  float gx = 0, gy = 0, gz = 0;  // dps
  float tempC = 0;
};

struct AdxlSample {
  int64_t monoNs = 0;
  float x = 0, y = 0, z = 0;  // g
};

// IMU and ADXL345 sampling on DRDY (PL timestamps), ToF and SHT40 polling.
// Owns I2C1 and the sensor SPI bus; runs in its own thread.
class SensorService {
 public:
  SensorService(const AppConfig& cfg, StateStore& state, TickClock& clock) : cfg_(cfg), state_(state), clock_(clock) {}
  ~SensorService() { stop(); }

  void start(I2cBus* sensorBus, SpiDevice* imuSpi, SpiDevice* adxlSpi);
  void stop();
  void onDrdy(const CarrierEvent& e);  // any thread
  void requestTofRestart() { tofRestart_ = true; }

  void imuWindow(int64_t t0, int64_t t1, std::vector<ImuSample>& out);
  void adxlWindow(int64_t t0, int64_t t1, std::vector<AdxlSample>& out);
  double imuOdr() const { return imuOdr_; }
  double adxlRate() const { return adxlRate_; }

 private:
  void run();
  void initImu();
  void initAdxl();
  void initTof();
  void readImu(int64_t monoNs);
  void readAdxl(int64_t monoNs);
  void pollTof(int64_t nowMs);
  void pollSht(int64_t nowMs);

  const AppConfig& cfg_;
  StateStore& state_;
  TickClock& clock_;
  I2cBus* i2c_ = nullptr;
  std::unique_ptr<Lsm6dsv> imu_;
  std::unique_ptr<Adxl345> adxl_;
  std::unique_ptr<Vl53l1x> tof_;
  std::unique_ptr<Sht40> sht_;
  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<bool> tofRestart_{false};

  std::mutex qMutex_;
  std::condition_variable qCv_;
  std::deque<CarrierEvent> queue_;

  std::mutex dataMutex_;
  std::deque<ImuSample> imuBuf_;
  std::deque<AdxlSample> adxlBuf_;
  double imuOdr_ = 0, adxlRate_ = 0;
  int64_t lastImuNs_ = 0, lastAdxlNs_ = 0;
  int64_t nextShtMs_ = 0, nextTofMs_ = 0;
  bool tofRunning_ = false;
};

}  // namespace vm
