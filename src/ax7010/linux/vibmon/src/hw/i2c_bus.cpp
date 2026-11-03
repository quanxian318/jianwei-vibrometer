#include "hw/i2c_bus.h"

#include <fcntl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "util/log.h"

namespace vm {

bool I2cBus::writeReg8(uint8_t addr, uint8_t reg, uint8_t value) {
  const uint8_t b[2] = {reg, value};
  return transfer(addr, b, 2, nullptr, 0);
}

bool I2cBus::readReg8(uint8_t addr, uint8_t reg, uint8_t& value) { return transfer(addr, &reg, 1, &value, 1); }

bool I2cBus::writeReg16BE(uint8_t addr, uint8_t reg, uint16_t value) {
  const uint8_t b[3] = {reg, static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value & 0xFF)};
  return transfer(addr, b, 3, nullptr, 0);
}

bool I2cBus::readReg16BE(uint8_t addr, uint8_t reg, uint16_t& value) {
  uint8_t b[2];
  if (!transfer(addr, &reg, 1, b, 2)) return false;
  value = static_cast<uint16_t>((b[0] << 8) | b[1]);
  return true;
}

bool I2cBus::readRegs(uint8_t addr, uint8_t reg, uint8_t* buf, size_t n) { return transfer(addr, &reg, 1, buf, n); }

bool I2cBus::probe(uint8_t addr) {
  uint8_t b;
  return transfer(addr, nullptr, 0, &b, 1);
}

LinuxI2cBus::~LinuxI2cBus() { close(); }

bool LinuxI2cBus::open(const std::string& device) {
  close();
  fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    LOGE("i2c", "open %s failed: %s", device.c_str(), strerror(errno));
    return false;
  }
  device_ = device;
  ioctl(fd_, I2C_TIMEOUT, 10);  // units of 10 ms
  ioctl(fd_, I2C_RETRIES, 1);
  return true;
}

void LinuxI2cBus::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool LinuxI2cBus::transfer(uint8_t addr, const uint8_t* w, size_t wlen, uint8_t* r, size_t rlen) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (fd_ < 0) return false;
  i2c_msg msgs[2];
  int n = 0;
  if (wlen > 0) {
    msgs[n].addr = addr;
    msgs[n].flags = 0;
    msgs[n].len = static_cast<uint16_t>(wlen);
    msgs[n].buf = const_cast<uint8_t*>(w);
    ++n;
  }
  if (rlen > 0) {
    msgs[n].addr = addr;
    msgs[n].flags = I2C_M_RD;
    msgs[n].len = static_cast<uint16_t>(rlen);
    msgs[n].buf = r;
    ++n;
  }
  if (n == 0) return false;
  i2c_rdwr_ioctl_data data{};
  data.msgs = msgs;
  data.nmsgs = static_cast<uint32_t>(n);
  for (int attempt = 0; attempt < 2; ++attempt) {
    if (ioctl(fd_, I2C_RDWR, &data) == n) return true;
    if (errno != EAGAIN && errno != ETIMEDOUT) break;
  }
  errors_++;
  LOGD("i2c", "%s addr 0x%02x transfer failed: %s", device_.c_str(), addr, strerror(errno));
  return false;
}

}  // namespace vm
