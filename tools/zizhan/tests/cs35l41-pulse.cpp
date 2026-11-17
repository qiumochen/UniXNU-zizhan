#include <cassert>
#include <cstdio>
#include <map>
#include "ZizhanAmpPulse.h"
struct Device{
 std::map<uint32_t,uint32_t> regs={{0x2014,0},{0x2018,0x3300},{0x2bc1000,1},{0x4800,0x10000},{0x13004,0},{0x28007d0,2},{0x4c48,0x18},{0x4c4c,0x19},{0x6000,0x267c},{0x10010,0x1400000},{0x10090,0x41406000}};
 unsigned test=0,commands=0,fail=0;bool uncertain=false;uint64_t time=0;
 static int read(void *p,uint32_t reg,uint32_t *v){auto &d=*static_cast<Device *>(p);assert(!d.uncertain);++d.commands;if(d.fail==d.commands){d.uncertain=true;return 0;}assert(d.regs.count(reg));*v=d.regs[reg];return 1;}
 static int write(void *p,uint32_t reg,uint32_t v){auto &d=*static_cast<Device *>(p);assert(!d.uncertain);++d.commands;if(d.fail==d.commands){d.uncertain=true;return 0;}d.regs[reg]=v;return 1;}
 static uint64_t now(void *p){return ++static_cast<Device *>(p)->time;}
 static int bad(void *p){return static_cast<Device *>(p)->uncertain;}
 static int data(void *p){auto &d=*static_cast<Device *>(p);assert(d.regs[0x2018]==0x3321&&d.regs[0x6000]==0x3f44);d.time+=10000;if(d.test==9)d.time+=250000;if(d.test==10)d.regs[0x10090]|=0x100;return d.test==8?0:1;}
 ZizhanPulseOps ops(){return {this,read,write,now,bad,data};}
};
int main(){
 unsigned commands=0;
 for(unsigned test=0;test<11;++test){Device d;d.test=test;auto o=d.ops();ZizhanPulseReceipt r={};
  if(test==1){assert(zizhan_pulse_run(&o,&r)==ZizhanPulseGuard&&!d.commands);continue;}
  assert(!zizhan_pulse_patch(&o,&r,1)&&r.patch_on);d.regs[0x2014]=1;
  if(test==2)d.regs[0x13004]=1;if(test==3)d.regs[0x28007d0]=1;if(test==4)d.regs[0x4c48]=0;
  if(test==5)d.regs[0x10010]|=0x100;if(test==6)d.regs[0x10090]|=0x80000000;
  if(test==7)d.regs[0x6000]=0;
  int rc=zizhan_pulse_run(&o,&r);
  if(!test){assert(!rc&&r.complete&&r.enabled&&r.disabled&&r.data_ack&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);d.regs[0x2014]=0;
   assert(!zizhan_pulse_patch(&o,&r,0)&&r.patch_off&&r.attempted==16&&r.acknowledged==16);commands=d.commands;
  }else{assert(rc&&!r.complete);if(test>=8)assert(r.enabled&&r.disabled&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);else assert(!r.enabled);}
 }
 for(unsigned fail=1;fail<=commands;++fail){Device d;d.fail=fail;auto o=d.ops();ZizhanPulseReceipt r={};int rc=zizhan_pulse_patch(&o,&r,1);
  if(!rc){d.regs[0x2014]=1;rc=zizhan_pulse_run(&o,&r);}
  if(!rc){d.regs[0x2014]=0;rc=zizhan_pulse_patch(&o,&r,0);}
  assert(rc&&d.uncertain&&d.commands==fail);
 }
 puts("PASS nativepulse guards/faults/latefault/datafailure/timeout/clearAMP-beforeBST/nativepup-pdn/16writebudget/alltransactionfailure retention;no hardware");
}
