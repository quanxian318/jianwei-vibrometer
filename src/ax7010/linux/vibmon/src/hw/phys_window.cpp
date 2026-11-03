#include "hw/phys_window.h"

#include <dirent.h>
#include <fcntl.h>
#include <setjmp.h>
#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>

#include "util/log.h"

namespace vm {

PhysWindow::~PhysWindow() { close(); }

bool PhysWindow::open(uint64_t phys, size_t size) {
  close();
  const uint64_t page = static_cast<uint64_t>(sysconf(_SC_PAGESIZE));
  const uint64_t start = phys & ~(page - 1);
  const size_t lead = static_cast<size_t>(phys - start);
  const int fd = ::open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
  if (fd < 0) {
    LOGE("mem", "open /dev/mem: %s", strerror(errno));
    return false;
  }
  void* p = mmap64(nullptr, lead + size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, static_cast<off64_t>(start));
  ::close(fd);
  if (p == MAP_FAILED) {
    LOGE("mem", "mmap 0x%llx (%zu bytes): %s", static_cast<unsigned long long>(phys), size, strerror(errno));
    return false;
  }
  map_ = p;
  mapSize_ = lead + size;
  base_ = static_cast<uint8_t*>(p) + lead;
  size_ = size;
  return true;
}

void PhysWindow::close() {
  if (map_) munmap(map_, mapSize_);
  map_ = nullptr;
  base_ = nullptr;
  mapSize_ = size_ = 0;
}

uint32_t PhysWindow::read32(uint32_t offset) { return *reinterpret_cast<volatile uint32_t*>(base_ + offset); }

void PhysWindow::write32(uint32_t offset, uint32_t value) {
  *reinterpret_cast<volatile uint32_t*>(base_ + offset) = value;
}

namespace {
thread_local sigjmp_buf* probeJump = nullptr;

void onBusError(int sig) {
  if (probeJump) siglongjmp(*probeJump, 1);
  signal(sig, SIG_DFL);
  raise(sig);
}
}  // namespace

bool PhysWindow::tryRead32(uint32_t offset, uint32_t& value) {
  struct sigaction sa {}, old {};
  sa.sa_handler = onBusError;
  sigemptyset(&sa.sa_mask);
  sigaction(SIGBUS, &sa, &old);
  sigjmp_buf jump;
  bool ok = false;
  if (sigsetjmp(jump, 1) == 0) {
    probeJump = &jump;
    value = read32(offset);
    ok = true;
  }
  probeJump = nullptr;
  sigaction(SIGBUS, &old, nullptr);
  return ok;
}

bool reservedForDevices(uint64_t base, uint64_t size) {
  const uint64_t end = base + size;
  bool overlapsRam = false;
  if (FILE* f = fopen("/proc/iomem", "r")) {
    char line[256];
    unsigned long long first, last;
    while (fgets(line, sizeof(line), f)) {
      if (strstr(line, "System RAM") && sscanf(line, "%llx-%llx", &first, &last) == 2 && base <= last && end > first) {
        overlapsRam = true;
      }
    }
    fclose(f);
  }
  if (!overlapsRam) return true;
  // reserved-memory children with one address and one size cell each.
  const std::string dir = "/sys/firmware/devicetree/base/reserved-memory";
  DIR* d = opendir(dir.c_str());
  if (!d) return false;
  bool inside = false;
  while (dirent* e = readdir(d)) {
    FILE* f = fopen((dir + "/" + e->d_name + "/reg").c_str(), "rb");
    if (!f) continue;
    unsigned char b[8];
    if (fread(b, 1, sizeof(b), f) == sizeof(b)) {
      const uint64_t first = (uint64_t(b[0]) << 24) | (b[1] << 16) | (b[2] << 8) | b[3];
      const uint64_t len = (uint64_t(b[4]) << 24) | (b[5] << 16) | (b[6] << 8) | b[7];
      if (base >= first && end <= first + len) inside = true;
    }
    fclose(f);
  }
  closedir(d);
  return inside;
}

}  // namespace vm
