#include "hw/uart16550.h"
#include <algorithm>

namespace vm {
namespace {
constexpr uint32_t kData=0x1000, kIer=0x1004, kFcr=0x1008, kLcr=0x100c,
                   kMcr=0x1010, kLsr=0x1014, kMsr=0x1018, kScr=0x101c;
}
bool Uart16550::init(uint32_t clockHz, uint32_t baud, bool ctsFlow) {
  if (!baud || !clockHz) return false;
  const uint64_t divisor=(static_cast<uint64_t>(clockHz)+8ull*baud)/(16ull*baud);
  if (!divisor || divisor>65535) return false;
  const uint64_t target=16ull*baud*divisor;
  const uint64_t error=target>clockHz ? target-clockHz : clockHz-target;
  if (error*100>target*3) return false;
  io_.write32(kLcr,3);  // DLAB off, 8N1
  io_.write32(kIer,0);  // polling: no unhandled UART interrupts
  const uint32_t saved=io_.read32(kScr);
  io_.write32(kScr,0x5a);
  if ((io_.read32(kScr)&255)!=0x5a) { io_.write32(kScr,saved); return false; }
  io_.write32(kScr,0xa5);
  const bool ok=(io_.read32(kScr)&255)==0xa5;
  io_.write32(kScr,saved);
  if (!ok) return false;
  io_.write32(kLcr,0x83);
  io_.write32(kData,static_cast<uint32_t>(divisor)&255);
  io_.write32(kIer,static_cast<uint32_t>(divisor)>>8);
  io_.write32(kLcr,3);
  io_.write32(kMcr,3);  // assert DTR/RTS, leave internal loopback off
  io_.write32(kFcr,7);  // enable and clear FIFOs
  ctsFlow_=ctsFlow;
  return true;
}
bool Uart16550::ctsAsserted() const { return (io_.read32(kMsr)&0x10)!=0; }
bool Uart16550::readable() const { return (io_.read32(kLsr)&1)!=0; }
ssize_t Uart16550::readSome(uint8_t* data,size_t count) {
  size_t n=0;
  while (n<count && readable()) data[n++]=static_cast<uint8_t>(io_.read32(kData));
  return static_cast<ssize_t>(n);
}
ssize_t Uart16550::writeSome(const uint8_t* data,size_t count) {
  if (!count || (ctsFlow_ && !ctsAsserted()) || !(io_.read32(kLsr)&0x20)) return 0;
  // Limit a burst to the hardware FIFO; recheck CTS before each byte.
  size_t n=0;
  while (n<std::min<size_t>(16,count) && (!ctsFlow_ || ctsAsserted())) io_.write32(kData,data[n++]);
  return static_cast<ssize_t>(n);
}
int Uart16550::outputQueued() const { return (io_.read32(kLsr)&0x40) ? 0 : 1; }
void Uart16550::flushInput() { io_.write32(kFcr,3); }
void Uart16550::flushOutput() { io_.write32(kFcr,5); }
}  // namespace vm
