#include "app/sensor_service.h"

#include <algorithm>

#include "util/clock.h"
#include "util/log.h"

namespace vm {

namespace {
const char* kTag = "sensors";
const size_t kImuKeep = 240 * 120;   // 2 minutes at 240 Hz
const size_t kAdxlKeep = 200 * 120;
}  // namespace

void TickClock::addPair(uint64_t ticks, int64_t monoNs) {
  std::lock_guard<std::mutex> lock(mutex_);
  const int64_t tNs = static_cast<int64_t>(static_cast<long double>(ticks) * 1e9L / hz_);
  offsets_.push_back(monoNs - tNs);
  if (offsets_.size() > 9) offsets_.pop_front();
}

bool TickClock::ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return !offsets_.empty();
}

int64_t TickClock::toMonoNs(uint64_t ticks) const {
  std::lock_guard<std::mutex> lock(mutex_);
  if (offsets_.empty()) return monoNs();
  std::vector<int64_t> v(offsets_.begin(), offsets_.end());
  std::nth_element(v.begin(), v.begin() + v.size() / 2, v.end());
  return static_cast<int64_t>(static_cast<long double>(ticks) * 1e9L / hz_) + v[v.size() / 2];
}

void SensorService::start(I2cBus* sensorBus, SpiDevice* imuSpi, SpiDevice* adxlSpi) {
  i2c_ = sensorBus;
  if (imuSpi && cfg_.imu.enabled) imu_.reset(new Lsm6dsv(*imuSpi));
  if (adxlSpi && cfg_.adxl.enabled) adxl_.reset(new Adxl345(*adxlSpi));
  if (i2c_ && cfg_.tof.enabled) tof_.reset(new Vl53l1x(*i2c_));
  if (i2c_ && cfg_.sht.enabled) sht_.reset(new Sht40(*i2c_));
  running_ = true;
  thread_ = std::thread(&SensorService::run, this);
}

void SensorService::stop() {
  if (!running_) return;
  running_ = false;
  qCv_.notify_all();
  if (thread_.joinable()) thread_.join();
  if (imu_) imu_->powerDown();
  if (adxl_) adxl_->standby();
  if (tof_) tof_->stop();
}

void SensorService::onDrdy(const CarrierEvent& e) {
  {
    std::lock_guard<std::mutex> lock(qMutex_);
    if (queue_.size() < 2048) queue_.push_back(e);
  }
  qCv_.notify_one();
}

void SensorService::initImu() {
  if (!imu_) return;
  uint8_t id = 0;
  imu_->whoAmI(id);
  Lsm6dsv::Config c;
  c.odrHz = cfg_.imu.odrHz;
  c.accelRangeG = cfg_.imu.accelG;
  c.gyroRangeDps = cfg_.imu.gyroDps;
  const bool ok = imu_->init(c);
  imuOdr_ = ok ? imu_->odrHz() : 0;
  if (ok) {
    LOGI(kTag, "LSM6DSV ready, ODR %d Hz", imu_->odrHz());
  } else {
    LOGW(kTag, "LSM6DSV not found or not configured (WHO_AM_I 0x%02X, expected 0x70)", id);
  }
  state_.update([&](SystemState& s) { s.imuOk = ok; });
}

void SensorService::initAdxl() {
  if (!adxl_) return;
  uint8_t id = 0;
  adxl_->devId(id);
  const bool ok = adxl_->init(static_cast<uint8_t>(cfg_.adxl.rateCode), cfg_.adxl.rangeG);
  adxlRate_ = ok ? Adxl345::rateHz(static_cast<uint8_t>(cfg_.adxl.rateCode)) : 0;
  if (ok) {
    LOGI(kTag, "ADXL345 ready, %.0f Hz", adxlRate_);
  } else {
    LOGW(kTag, "ADXL345 not found (DEVID 0x%02X, expected 0xE5)", id);
  }
  state_.update([&](SystemState& s) { s.adxlOk = ok; });
}

void SensorService::initTof() {
  if (!tof_) return;
  uint16_t model = 0;
  std::string note;
  bool ok = false;
  if (!tof_->readModelId(model)) {
    note = "no_response";
  } else if (model != 0xEACC) {
    note = "not_vl53l1x";
    LOGW(kTag, "ToF model id 0x%04X is not a VL53L1X (VL53L0X needs a different driver)", model);
  } else if (!Vl53l1x::driverAvailable()) {
    note = "uld_driver_missing";
    LOGW(kTag, "VL53L1X found; ST ULD sources not built in, ranging disabled");
  } else if (!tof_->start(cfg_.tof.timingBudgetMs, cfg_.tof.periodMs, cfg_.tof.longRange)) {
    note = "init_failed";
  } else {
    ok = true;
    note = "ranging";
  }
  tofRunning_ = ok;
  state_.update([&](SystemState& s) {
    s.tofOk = ok;
    s.tofNote = note;
    s.distanceValid = false;
  });
}

void SensorService::readImu(int64_t monoNs) {
  ImuRaw r;
  if (!imu_->readRaw(r)) return;
  ImuSample s;
  s.monoNs = monoNs;
  s.ax = static_cast<float>(imu_->accelG(r.ax));
  s.ay = static_cast<float>(imu_->accelG(r.ay));
  s.az = static_cast<float>(imu_->accelG(r.az));
  s.gx = static_cast<float>(imu_->gyroDps(r.gx));
  s.gy = static_cast<float>(imu_->gyroDps(r.gy));
  s.gz = static_cast<float>(imu_->gyroDps(r.gz));
  s.tempC = static_cast<float>(Lsm6dsv::tempC(r.temp));
  std::lock_guard<std::mutex> lock(dataMutex_);
  imuBuf_.push_back(s);
  if (imuBuf_.size() > kImuKeep) imuBuf_.pop_front();
  lastImuNs_ = monoNs;
}

void SensorService::readAdxl(int64_t monoNs) {
  int16_t x, y, z;
  if (!adxl_->readXyz(x, y, z)) return;
  AdxlSample s;
  s.monoNs = monoNs;
  s.x = static_cast<float>(x * Adxl345::lsbToG());
  s.y = static_cast<float>(y * Adxl345::lsbToG());
  s.z = static_cast<float>(z * Adxl345::lsbToG());
  std::lock_guard<std::mutex> lock(dataMutex_);
  adxlBuf_.push_back(s);
  if (adxlBuf_.size() > kAdxlKeep) adxlBuf_.pop_front();
  lastAdxlNs_ = monoNs;
}

void SensorService::pollTof(int64_t nowMs) {
  if (!tof_) return;
  if (tofRestart_.exchange(false)) {
    tof_->stop();
    initTof();
  }
  if (!tofRunning_ || nowMs < nextTofMs_) return;
  nextTofMs_ = nowMs + std::max(20, cfg_.tof.periodMs / 2);
  TofReading r;
  bool ready = false;
  if (!tof_->poll(r, ready) || !ready) return;
  // Invalid range status is never shown as a distance (no "0 mm" readings).
  state_.update([&](SystemState& s) {
    s.distanceValid = r.valid;
    if (r.valid) s.distanceMm = r.distanceMm;
  });
}

void SensorService::pollSht(int64_t nowMs) {
  if (!sht_ || nowMs < nextShtMs_) return;
  nextShtMs_ = nowMs + cfg_.sht.periodMs;
  double t = 0, rh = 0;
  const bool ok = sht_->measure(t, rh);
  state_.update([&](SystemState& s) {
    s.shtOk = ok;
    s.envValid = ok;
    if (ok) {
      s.tempC = t;
      s.rhPercent = rh;
    }
  });
}

void SensorService::run() {
  // 3V3_SENS comes up ~30 ms after AX_3V3; give the modules time to boot.
  sleepMs(200);
  initImu();
  initAdxl();
  initTof();
  while (running_) {
    std::deque<CarrierEvent> events;
    {
      std::unique_lock<std::mutex> lock(qMutex_);
      qCv_.wait_for(lock, std::chrono::milliseconds(10), [this] { return !queue_.empty() || !running_; });
      events.swap(queue_);
    }
    for (const auto& e : events) {
      const int64_t t = clock_.toMonoNs(e.ticks);
      if (e.source == kSrcImuInt1 && e.level && imu_ && imuOdr_ > 0) readImu(t);
      if (e.source == kSrcAdxlDrdy && e.level && adxl_ && adxlRate_ > 0) readAdxl(t);
    }
    const int64_t now = monoNs();
    // ADXL345 DATA_READY is latched: a missed edge would stall it, so read
    // anyway when no edge arrived for three periods.
    if (adxl_ && adxlRate_ > 0 && now - lastAdxlNs_ > static_cast<int64_t>(3e9 / adxlRate_)) readAdxl(now);
    const int64_t nowMs = now / 1000000;
    pollTof(nowMs);
    pollSht(nowMs);
  }
}

void SensorService::imuWindow(int64_t t0, int64_t t1, std::vector<ImuSample>& out) {
  std::lock_guard<std::mutex> lock(dataMutex_);
  for (const auto& s : imuBuf_) {
    if (s.monoNs >= t0 && s.monoNs <= t1) out.push_back(s);
  }
}

void SensorService::adxlWindow(int64_t t0, int64_t t1, std::vector<AdxlSample>& out) {
  std::lock_guard<std::mutex> lock(dataMutex_);
  for (const auto& s : adxlBuf_) {
    if (s.monoNs >= t0 && s.monoNs <= t1) out.push_back(s);
  }
}

}  // namespace vm
