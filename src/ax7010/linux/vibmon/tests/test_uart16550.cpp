#include "hw/uart16550.h"
#include <cassert>
#include <deque>
#include <map>
#include <vector>
#include <cstdio>
using namespace vm;
struct FakeUart : RegIo {
  std::map<uint32_t,uint32_t> regs;
  std::deque<uint8_t> rx;
  std::vector<uint8_t> tx;
  bool cts=true,empty=true,scratch=true;
  uint32_t read32(uint32_t off) override {
    if (off==0x1014) return (empty ? 0x60 : 0) | (!rx.empty() ? 1 : 0);
    if (off==0x1018) return cts ? 0x10 : 0;
    if (off==0x101c && !scratch) return 0;
    if (off==0x1000 && !(regs[0x100c]&128) && !rx.empty()) { auto v=rx.front();rx.pop_front();return v; }
    return regs[off];
  }
  void write32(uint32_t off,uint32_t v) override {
    if (off==0x1000 && !(regs[0x100c]&128)) { tx.push_back(v);return; }
    regs[off]=v;
  }
};
int main() {
  FakeUart f;Uart16550 u(f);
  assert(!u.init(100000000,0,true));
  assert(u.init(100000000,115200,true));
  assert(f.regs[0x1000]==54 && f.regs[0x1004]==0 && f.regs[0x100c]==3);
  uint8_t data[32]={};
  f.cts=false;assert(u.writeSome(data,32)==0 && f.tx.empty());
  f.cts=true;f.empty=false;assert(u.writeSome(data,32)==0 && u.outputQueued()==1);
  f.empty=true;assert(u.writeSome(data,32)==16 && f.tx.size()==16 && u.outputQueued()==0);
  f.rx={0x41,0x42};assert(u.readSome(data,32)==2 && data[0]==0x41 && data[1]==0x42);
  assert(!u.readable());
  FakeUart bad;bad.scratch=false;Uart16550 b(bad);assert(!b.init(100000000,115200,true));
  FakeUart nf;nf.cts=false;Uart16550 noflow(nf);assert(noflow.init(100000000,115200,false));
  assert(noflow.writeSome(data,1)==1);
  puts("uart16550: divisor, absent device, CTS gating, FIFO bounds, drain and receive passed");
}
