#include "hw/uio.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "util/log.h"

namespace vm {

UioDevice::~UioDevice() { close(); }

bool UioDevice::open(const std::string& device, size_t mapSize) {
  close();
  fd_ = ::open(device.c_str(), O_RDWR | O_CLOEXEC | O_NONBLOCK);
  if (fd_ < 0) {
    LOGE("uio", "open %s failed: %s", device.c_str(), strerror(errno));
    return false;
  }
  void* p = mmap(nullptr, mapSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);  // map0 at offset 0
  if (p == MAP_FAILED) {
    LOGE("uio", "mmap %s failed: %s", device.c_str(), strerror(errno));
    close();
    return false;
  }
  regs_ = static_cast<volatile uint32_t*>(p);
  size_ = mapSize;
  return true;
}

void UioDevice::close() {
  if (regs_) munmap(const_cast<uint32_t*>(regs_), size_);
  regs_ = nullptr;
  if (fd_ >= 0) ::close(fd_);
  fd_ = -1;
}

uint32_t UioDevice::read32(uint32_t offset) { return regs_[offset / 4]; }

void UioDevice::write32(uint32_t offset, uint32_t value) { regs_[offset / 4] = value; }

bool UioDevice::enableIrq() {
  uint32_t one = 1;
  return fd_ >= 0 && ::write(fd_, &one, sizeof(one)) == static_cast<ssize_t>(sizeof(one));
}

bool UioDevice::ackIrq() {
  uint32_t count = 0;
  return fd_ >= 0 && ::read(fd_, &count, sizeof(count)) == static_cast<ssize_t>(sizeof(count));
}

}  // namespace vm
