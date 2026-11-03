#include "drv/pl_camera.h"

#include <cstdio>

#include "hw/phys_window.h"
#include "util/clock.h"

namespace vm {

namespace {

constexpr uint8_t kOv5640 = 0x3C;

struct SensorReg {
  uint16_t reg;
  uint8_t value;
};
const SensorReg kSensorSetup[] = {
#include "drv/ov5640_table.inc"
};

// AXI GPIO (cmos_rst): bit c releases sensor c.
constexpr uint32_t kGpioData = 0x00, kGpioTri = 0x04;
// AXI VDMA S2MM registers (PG020).
constexpr uint32_t kParkPtr = 0x28, kS2mmCr = 0x30, kS2mmSr = 0x34, kS2mmFrmStore = 0x48, kS2mmVsize = 0xA0,
                   kS2mmHsize = 0xA4, kS2mmStride = 0xA8, kS2mmAddr0 = 0xAC;
constexpr uint32_t kCrRun = 1u << 0, kCrCircular = 1u << 1, kCrReset = 1u << 2, kCrFrameIrq = 1u << 12,
                   kCrIrqEveryFrame = 1u << 16;
constexpr uint32_t kSrErrors = 0x0FF0, kSrClear = 0x7FF0;

}  // namespace

PlCameraRig::PlCameraRig(const PlCameraConfig& cfg, std::unique_ptr<RegIo> gpio, std::unique_ptr<RegIo> vdma0,
                         std::unique_ptr<RegIo> vdma1, std::unique_ptr<I2cBus> i2c0, std::unique_ptr<I2cBus> i2c1)
    : cfg_(cfg), gpio_(std::move(gpio)) {
  vdma_[0] = std::move(vdma0);
  vdma_[1] = std::move(vdma1);
  i2c_[0] = std::move(i2c0);
  i2c_[1] = std::move(i2c1);
}

std::unique_ptr<PlCameraRig> PlCameraRig::open(const PlCameraConfig& cfg, std::string& err) {
  std::unique_ptr<PhysWindow> gpio(new PhysWindow), v0(new PhysWindow), v1(new PhysWindow);
  if (!gpio->open(cfg.gpioBase, 0x1000) || !v0->open(cfg.vdmaBase[0], 0x1000) || !v1->open(cfg.vdmaBase[1], 0x1000)) {
    err = "cannot map the camera registers through /dev/mem";
    return nullptr;
  }
  std::unique_ptr<LinuxI2cBus> i0(new LinuxI2cBus), i1(new LinuxI2cBus);
  if (!i0->open(cfg.i2cDevice[0]) || !i1->open(cfg.i2cDevice[1])) {
    err = "cannot open the camera I2C buses";
    return nullptr;
  }
  return std::unique_ptr<PlCameraRig>(
      new PlCameraRig(cfg, std::move(gpio), std::move(v0), std::move(v1), std::move(i0), std::move(i1)));
}

void PlCameraRig::resetSensors() {
  gpio_->write32(kGpioTri, 0);  // both bits outputs
  for (uint32_t level : {3u, 0u, 3u}) {
    gpio_->write32(kGpioData, level);
    sleepMs(500);
  }
}

bool PlCameraRig::writeSensor(int cam, uint16_t reg, uint8_t value) {
  const uint8_t b[3] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF), value};
  return i2c_[cam]->transfer(kOv5640, b, 3, nullptr, 0);
}

bool PlCameraRig::readSensor(int cam, uint16_t reg, uint8_t& value) {
  const uint8_t b[2] = {static_cast<uint8_t>(reg >> 8), static_cast<uint8_t>(reg & 0xFF)};
  return i2c_[cam]->transfer(kOv5640, b, 2, &value, 1);
}

bool PlCameraRig::initSensor(int cam, std::string& err) {
  char buf[96];
  uint8_t hi = 0, lo = 0;
  if (!readSensor(cam, 0x300A, hi) || !readSensor(cam, 0x300B, lo)) {
    err = "no OV5640 answers on " + cfg_.i2cDevice[cam];
    return false;
  }
  if (hi != 0x56 || lo != 0x40) {
    snprintf(buf, sizeof(buf), "chip id 0x%02X%02X on %s is not an OV5640", hi, lo, cfg_.i2cDevice[cam].c_str());
    err = buf;
    return false;
  }
  for (const SensorReg& r : kSensorSetup) {
    if (!writeSensor(cam, r.reg, r.value)) {
      snprintf(buf, sizeof(buf), "write 0x%04X failed on %s", r.reg, cfg_.i2cDevice[cam].c_str());
      err = buf;
      return false;
    }
    if (r.reg == 0x3008 && (r.value & 0x80)) sleepMs(5);  // software reset
  }
  return true;
}

bool PlCameraRig::initPair(std::string& err) {
  resetSensors();
  for (int cam = 0; cam < 2; ++cam) {
    if (!initSensor(cam, err)) return false;
  }
  if (cfg_.fps != 45) return true;
  // The team's 45 fps timing: PLL x120, HTS 1440, AEC bands and tone curve.
  static const SensorReg kFast[] = {
      {0x3008, 0x42}, {0x3036, 120}, {0x380C, 0x05}, {0x380D, 0xA0}, {0x3A08, 0x00}, {0x3A09, 166},
      {0x3A0A, 0x00}, {0x3A0B, 138}, {0x3A0E, 0x04}, {0x3A0D, 0x05}, {0x5580, 0x02}, {0x5588, 0x41},
      {0x5583, 0x48}, {0x5584, 0x48}, {0x5481, 0x28}, {0x5482, 0x48}, {0x5483, 0x68}, {0x5484, 0x82},
      {0x5485, 0x92}, {0x5486, 0x9C}, {0x5487, 0xA6}, {0x5488, 0xAF}, {0x5489, 0xB8}, {0x548A, 0xC1},
      {0x548B, 0xCF}, {0x548C, 0xD9}, {0x548D, 0xE6}, {0x548E, 0xED}, {0x548F, 0xF4}, {0x3008, 0x02},
  };
  for (int cam = 0; cam < 2; ++cam) {
    for (const SensorReg& r : kFast) {
      if (!writeSensor(cam, r.reg, r.value)) {
        err = "fast timing not accepted by " + cfg_.i2cDevice[cam];
        return false;
      }
    }
  }
  sleepMs(2500);  // automatic exposure settles
  // Manual exposure of 667 lines (in 1/16 line units) on both sensors and one
  // gain that keeps the mean exposure x gain brightness.
  uint64_t sum = 0;
  for (int cam = 0; cam < 2; ++cam) {
    uint8_t e0 = 0, e1 = 0, e2 = 0, g0 = 0, g1 = 0;
    readSensor(cam, 0x3500, e0);
    readSensor(cam, 0x3501, e1);
    readSensor(cam, 0x3502, e2);
    readSensor(cam, 0x350A, g0);
    readSensor(cam, 0x350B, g1);
    sum += static_cast<uint64_t>(((e0 & 15u) << 16) | (e1 << 8) | e2) * (((g0 & 3u) << 8) | g1);
    writeSensor(cam, 0x3503, 0x03);  // AEC and AGC manual
    writeSensor(cam, 0x3500, 0x00);
    writeSensor(cam, 0x3501, 0x29);
    writeSensor(cam, 0x3502, 0xB0);
  }
  uint32_t gain = static_cast<uint32_t>(sum / (2u * 10672u));
  gain = gain < 16 ? 16 : (gain > 512 ? 512 : gain);
  for (int cam = 0; cam < 2; ++cam) {  // group hold: both gain bytes take effect together
    writeSensor(cam, 0x3208, 0x00);
    writeSensor(cam, 0x350A, static_cast<uint8_t>((gain >> 8) & 3));
    writeSensor(cam, 0x350B, static_cast<uint8_t>(gain & 0xFF));
    writeSensor(cam, 0x3208, 0x10);
    writeSensor(cam, 0x3208, 0xA0);
  }
  return true;
}

bool PlCameraRig::startCapture(int cam, std::string& err) {
  RegIo& v = *vdma_[cam];
  v.write32(kS2mmCr, kCrReset);
  const int64_t t0 = monoMs();
  while (v.read32(kS2mmCr) & kCrReset) {
    if (monoMs() - t0 > 2000) {
      err = "VDMA reset does not complete";
      return false;
    }
    sleepMs(1);
  }
  v.write32(kS2mmFrmStore, kStores);
  v.write32(kS2mmSr, kSrClear);
  for (int k = 0; k < kStores; ++k) v.write32(kS2mmAddr0 + 4 * k, storeAddress(cam, k));
  v.write32(kS2mmHsize, kWidth * 3);
  v.write32(kS2mmStride, kWidth * 3);
  v.write32(kS2mmCr, kCrRun | kCrCircular | kCrFrameIrq | kCrIrqEveryFrame);
  v.write32(kS2mmVsize, kHeight);  // starts the channel
  return true;
}

void PlCameraRig::stopCapture(int cam) { vdma_[cam]->write32(kS2mmCr, 0); }

int PlCameraRig::writingStore(int cam) { return static_cast<int>((vdma_[cam]->read32(kParkPtr) >> 24) & 31u); }

uint32_t PlCameraRig::errors(int cam) { return vdma_[cam]->read32(kS2mmSr) & kSrErrors; }

void PlCameraRig::clearErrors(int cam) { vdma_[cam]->write32(kS2mmSr, kSrClear); }

uint32_t PlCameraRig::storeAddress(int cam, int store) const {
  return cfg_.bufferBase + static_cast<uint32_t>(kStores * cam + store) * cfg_.frameStride;
}

}  // namespace vm
