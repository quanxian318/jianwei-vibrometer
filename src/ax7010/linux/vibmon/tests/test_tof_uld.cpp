#include <array>
#include <cassert>
#include <chrono>
#include <cstdio>
#include "drv/vl53l1x.h"

class Bus : public vm::I2cBus {
 public:
  std::array<uint8_t,65536> reg{};
  bool fail=false, ready=true;
  bool transfer(uint8_t addr,const uint8_t* w,size_t wn,uint8_t* r,size_t rn) override {
    assert(addr==0x29 && wn>=2);
    if(fail)return false;
    const unsigned index=(unsigned(w[0])<<8)|w[1];
    if(rn){
      for(size_t i=0;i<rn;++i)r[i]=reg[index+i];
      if(index==0x31 && rn==1)r[0]=ready?1:0;
    } else for(size_t i=2;i<wn;++i)reg[index+i-2]=w[i];
    return true;
  }
};

int main(){
  Bus b;b.reg[0x10f]=0xea;b.reg[0x110]=0xcc;b.reg[0xe5]=1;
  vm::Vl53l1x tof(b);uint16_t id=0;
  assert(tof.driverAvailable() && tof.readModelId(id) && id==0xeacc);
  assert(tof.start(50,100,true));
  b.reg[0x89]=9;b.reg[0x96]=1;b.reg[0x97]=44;
  vm::TofReading r;bool ready=false;
  assert(tof.poll(r,ready) && ready && r.valid && r.distanceMm==300);
  b.ready=false;assert(tof.poll(r,ready) && !ready);
  b.ready=true;b.reg[0x89]=4;
  assert(tof.poll(r,ready) && ready && !r.valid);
  b.fail=true;assert(!tof.readModelId(id));b.fail=false;
  assert(tof.stop());
  // A sensor which boots but never completes initial calibration must return.
  b.ready=false;
  const auto start=std::chrono::steady_clock::now();
  assert(!tof.start(50,100,true));
  assert(std::chrono::steady_clock::now()-start<std::chrono::seconds(3));
  std::puts("test_tof_uld: PASS (identity, valid/invalid/not-ready, I2C error, bounded init)");
}
