#include "hw/sysfs_find.h"

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace vm {

namespace {
std::vector<std::string> listDir(const char* dir, const char* prefix) {
  std::vector<std::string> out;
  DIR* d = opendir(dir);
  if (!d) return out;
  while (dirent* e = readdir(d)) {
    if (strncmp(e->d_name, prefix, strlen(prefix)) == 0) out.push_back(e->d_name);
  }
  closedir(d);
  return out;
}

bool exists(const std::string& p) { return access(p.c_str(), F_OK) == 0; }
}  // namespace

bool readOfNodeBase(const std::string& ofNodeDir, uint64_t& base) {
  std::ifstream f(ofNodeDir + "/reg", std::ios::binary);
  if (!f) return false;
  unsigned char b[4];
  if (!f.read(reinterpret_cast<char*>(b), 4)) return false;
  base = (static_cast<uint64_t>(b[0]) << 24) | (static_cast<uint64_t>(b[1]) << 16) |
         (static_cast<uint64_t>(b[2]) << 8) | b[3];
  return true;
}

std::string findI2cDevByBase(uint64_t base) {
  for (const std::string& name : listDir("/sys/bus/i2c/devices", "i2c-")) {
    uint64_t b = 0;
    if (readOfNodeBase("/sys/bus/i2c/devices/" + name + "/of_node", b) && b == base) {
      std::string dev = "/dev/" + name;
      if (exists(dev)) return dev;
    }
  }
  return std::string();
}

std::string findTtyByBase(uint64_t base) {
  for (const std::string& name : listDir("/sys/class/tty", "tty")) {
    uint64_t b = 0;
    if (readOfNodeBase("/sys/class/tty/" + name + "/device/of_node", b) && b == base) {
      std::string dev = "/dev/" + name;
      if (exists(dev)) return dev;
    }
  }
  return std::string();
}

std::string findSpidevByBase(uint64_t base, int cs) {
  for (const std::string& name : listDir("/sys/class/spi_master", "spi")) {
    uint64_t b = 0;
    if (readOfNodeBase("/sys/class/spi_master/" + name + "/device/of_node", b) && b == base) {
      std::string dev = "/dev/spidev" + name.substr(3) + "." + std::to_string(cs);
      if (exists(dev)) return dev;
    }
  }
  return std::string();
}

std::string findUioByBase(uint64_t base) {
  for (const std::string& name : listDir("/sys/class/uio", "uio")) {
    std::ifstream f("/sys/class/uio/" + name + "/maps/map0/addr");
    std::string s;
    if (f >> s && strtoull(s.c_str(), nullptr, 0) == base) {
      std::string dev = "/dev/" + name;
      if (exists(dev)) return dev;
    }
  }
  return std::string();
}

}  // namespace vm
