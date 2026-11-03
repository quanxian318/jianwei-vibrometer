#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "hw/i2c_bus.h"
#include "hw/uio.h"

namespace vm {

// AN5642 binocular module (two OV5640 on DVP) on the team's PL design
// (11_队友Vivado工程 AX7010_Sdoira_PL_V1): sensor reset on the cmos_rst AXI
// GPIO, SCCB on PS I2C0/I2C1 (EMIO), and per camera an AXI VDMA S2MM channel
// that stores 1280x720 RGB888 (byte 0 = R) circularly into three frame stores.
struct PlCameraConfig {
  std::string i2cDevice[2] = {"/dev/i2c-0", "/dev/i2c-1"};
  uint32_t gpioBase = 0x41200000;
  uint32_t vdmaBase[2] = {0x43010000, 0x43020000};
  // Frame store k of camera c lies at bufferBase + (3 * c + k) * frameStride
  // in DDR the kernel does not use (a reserved-memory region).
  uint32_t bufferBase = 0x1E000000;
  uint32_t frameStride = 3u << 20;
  // 30: the base table, automatic exposure (frame rate drops in dim light).
  // 45: the team's faster timing; exposure is then locked after 2.5 s of
  // automatic settling, with one gain for both sensors (fixed frame time,
  // matched brightness for stereo).
  int fps = 45;
  // Camera index (0/1) mounted on the left: measurement and stereo reference.
  int leftCamera = 1;
};

// Control side of both cameras; frame data is read by the caller from
// storeAddress() (see PlCameraSource).
class PlCameraRig {
 public:
  static constexpr int kWidth = 1280, kHeight = 720, kStores = 3;
  static constexpr uint32_t kFrameBytes = kWidth * kHeight * 3;

  PlCameraRig(const PlCameraConfig& cfg, std::unique_ptr<RegIo> gpio, std::unique_ptr<RegIo> vdma0,
              std::unique_ptr<RegIo> vdma1, std::unique_ptr<I2cBus> i2c0, std::unique_ptr<I2cBus> i2c1);
  // Maps the registers through /dev/mem and opens the I2C devices of cfg.
  static std::unique_ptr<PlCameraRig> open(const PlCameraConfig& cfg, std::string& err);

  void resetSensors();  // both sensors, about 1.5 s
  bool initSensor(int cam, std::string& err);  // chip id check, then the register table
  // Reset, set up both sensors and, at 45 fps, lock their exposure (about 4 s).
  bool initPair(std::string& err);
  bool startCapture(int cam, std::string& err);
  void stopCapture(int cam);
  // Store the VDMA is writing now; the previous one, (store + 2) % 3, is complete.
  int writingStore(int cam);
  // DMA error bits (SOF/EOL timing, slave/decode errors) since the last clear.
  uint32_t errors(int cam);
  void clearErrors(int cam);
  uint32_t storeAddress(int cam, int store) const;
  const PlCameraConfig& config() const { return cfg_; }

 private:
  bool writeSensor(int cam, uint16_t reg, uint8_t value);
  bool readSensor(int cam, uint16_t reg, uint8_t& value);

  PlCameraConfig cfg_;
  std::unique_ptr<RegIo> gpio_, vdma_[2];
  std::unique_ptr<I2cBus> i2c_[2];
};

}  // namespace vm
