/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* zizhan (docs/progress/2026-11-01-battery.md): the shared APSS RPMh owner and the SMMU lease lock for a kernel with the ADSP and
 * battery (CONFIG_XNU_PONG_POWER) but without the Wi-Fi stack, whose PongWifiPlatform.cpp defines them otherwise; build-pong-power-
 * native.py compiles this file only when XNU_PONG_WIFI is not 1. The vote is PongWifiPlatform.cpp's rawVote unchanged: APSS DRV2
 * (0x17a20000, SM8475 live DT on Pong and zizhan), SLEEP0..2, ACTIVE3..4, WAKE5..6; the GPU leases active TCS3, these clients share
 * active TCS4 under one sleeping lock, polled (no RSC interrupt handler). */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/clock.h>

namespace {
struct Window {
 IOMemoryMap *map=nullptr; volatile uint8_t *v=nullptr;
 bool open(uint64_t pa,size_t bytes) {
  auto m=IODeviceMemory::withRange(pa,bytes); if(!m)return false;
  map=m->map(kIOMapAnywhere|kIOMapInhibitCache);m->release();
  v=map?reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()):nullptr;return v;
 }
 uint32_t r(size_t o) const {uint32_t x=*reinterpret_cast<volatile uint32_t *>(v+o);__asm__ volatile("dmb oshld":::"memory");return x;}
 void w(size_t o,uint32_t x) {*reinterpret_cast<volatile uint32_t *>(v+o)=x;__asm__ volatile("dsb oshst":::"memory");}
};
Window rsc;
IOLock *powerLock,*smmuLeaseLock;
uint64_t now() {uint64_t ns=0;absolutetime_to_nanoseconds(mach_absolute_time(),&ns);return ns/1000000;}
bool fail(const char *s) {IOLog("PongRpmh: %s\n",s);return false;}
bool wait(Window &w,size_t o,uint32_t mask,uint32_t value,unsigned ms) {uint64_t end=now()+ms;do {if((w.r(o)&mask)==value)return true;IOSleep(1);}while(now()<end);return false;}
bool rawVote(uint32_t addr,uint32_t value) {
 constexpr unsigned b=0xd00+4*0x2a0,other=0xd00+3*0x2a0;
 unsigned caps=rsc.r(0xc),count=(caps>>12)&63,ncpt=(caps>>27)&31;
 if(((rsc.r(0)>>16)&255)>=3||count<8||ncpt<1||!rsc.r(b+0x18))return fail("active TCS4 unavailable");
 if(!rsc.r(other+0x18)){unsigned mask=rsc.r(other+0x1c);for(unsigned i=0;i<ncpt&&i<16;i++)if((mask&(1u<<i))&&rsc.r(other+0x34+20*i)==addr)return fail("conflicting RPMh transaction");}
 rsc.w(0xd00,rsc.r(0xd00)&~(1u<<4));
 rsc.w(b+0x14,rsc.r(b+0x14)&~((1u<<24)|(1u<<16)));rsc.w(b+0x1c,0);rsc.w(0xd08,1u<<4);
 if(!wait(rsc,0xd04,1u<<4,0,10))return false;
 rsc.w(b+0x30,0x10108);rsc.w(b+0x34,addr);rsc.w(b+0x38,value);
 rsc.w(b+0x10,1);rsc.w(b+0x1c,1);rsc.w(b+0x14,1u<<16);rsc.w(b+0x14,(1u<<16)|(1u<<24));
 bool progressed=false,done=false;uint64_t end=now()+100;
 do {unsigned st=rsc.r(b+0x3c),idle=rsc.r(b+0x18);if((st&0x10100)!=0x10100||!idle)progressed=true;if((st&0x10100)==0x10100&&idle&&(progressed||(rsc.r(0xd04)&(1u<<4)))){done=true;break;}IODelay(10);}while(now()<end);
 if(!done)return fail("RPMh response timeout; transaction retained");
 rsc.w(b+0x1c,0);rsc.w(0xd08,1u<<4);rsc.w(b+0x14,rsc.r(b+0x14)&~(1u<<24));return true;
}
}

extern "C" void pong_rpmh_init(void) {
 if(!powerLock)powerLock=IOLockAlloc();
 if(!smmuLeaseLock)smmuLeaseLock=IOLockAlloc();
}
extern "C" bool pong_rpmh_vote(uint32_t address,uint32_t value) {
 if(!powerLock)return false;
 IOLockLock(powerLock);
 bool ok=(rsc.v||rsc.open(0x17a20000,0x10000))&&rawVote(address,value);
 IOLockUnlock(powerLock);return ok;
}
extern "C" bool pong_smmu_lease_lock(void){if(!smmuLeaseLock)return false;IOLockLock(smmuLeaseLock);return true;}
extern "C" void pong_smmu_lease_unlock(void){if(smmuLeaseLock)IOLockUnlock(smmuLeaseLock);}
