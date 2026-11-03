#include "hw/spi_dev.h"

#include <fcntl.h>
#include <linux/spi/spidev.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "util/log.h"

namespace vm {

LinuxSpiDevice::~LinuxSpiDevice() { close(); }

bool LinuxSpiDevice::open(const std::string& device, uint8_t mode, uint32_t speedHz) {
  close();
  fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC);
  if (fd_ < 0) {
    LOGE("spi", "open %s failed: %s", device.c_str(), strerror(errno));
    return false;
  }
  uint8_t bits = 8;
  if (ioctl(fd_, SPI_IOC_WR_MODE, &mode) < 0 || ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
      ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ, &speedHz) < 0) {
    LOGE("spi", "configure %s failed: %s", device.c_str(), strerror(errno));
    close();
    return false;
  }
  speedHz_ = speedHz;
  device_ = device;
  return true;
}

void LinuxSpiDevice::close() {
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

bool LinuxSpiDevice::transfer(const uint8_t* tx, uint8_t* rx, size_t len) {
  if (fd_ < 0) return false;
  spi_ioc_transfer t;
  memset(&t, 0, sizeof(t));
  t.tx_buf = reinterpret_cast<uintptr_t>(tx);
  t.rx_buf = reinterpret_cast<uintptr_t>(rx);
  t.len = static_cast<uint32_t>(len);
  t.speed_hz = speedHz_;
  t.bits_per_word = 8;
  if (ioctl(fd_, SPI_IOC_MESSAGE(1), &t) < 0) {
    LOGD("spi", "%s transfer failed: %s", device_.c_str(), strerror(errno));
    return false;
  }
  return true;
}

}  // namespace vm
