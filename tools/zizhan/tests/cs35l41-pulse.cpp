#include <cassert>
#include <cstdio>
#include <map>
#include "ZizhanAmpPulse.h"
struct Device{
 std::map<uint32_t,uint32_t> regs={{0x2014,0},{0x2018,0x3300},{0x2bc1000,1},{0x4800,0x10000},{0x13004,0},{0x28007d0,2},{0x4c48,0x18},{0x4c4c,0x19},{0x6000,0x267c},{0x4c00,0},{0x6c04,0xb3},{0x10010,0x1400000},{0x10090,0x41406000}};
 unsigned test=0,commands=0,fail=0,level=0;bool uncertain=false;uint64_t time=0;
 static int read(void *p,uint32_t reg,uint32_t *v){auto &d=*static_cast<Device *>(p);assert(!d.uncertain);++d.commands;if(d.fail==d.commands){d.uncertain=true;return 0;}assert(d.regs.count(reg));*v=d.regs[reg];return 1;}
 static int write(void *p,uint32_t reg,uint32_t v){auto &d=*static_cast<Device *>(p);assert(!d.uncertain);++d.commands;if(d.fail==d.commands){d.uncertain=true;return 0;}d.regs[reg]=v;return 1;}
 static uint64_t now(void *p){return ++static_cast<Device *>(p)->time;}
 static int bad(void *p){return static_cast<Device *>(p)->uncertain;}
 static int data(void *p){auto &d=*static_cast<Device *>(p);if(d.test==11)assert(d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);else if(d.level)assert(d.regs[0x2018]==0x3321&&d.regs[0x6000]==0x4&&d.regs[0x6c04]==0x253);else assert(d.regs[0x2018]==0x3321&&d.regs[0x6000]==0x3f44);d.time+=10000;if(d.test==9)d.time+=250000;if(d.test==10)d.regs[0x10090]|=0x100;return d.test==8?0:1;}
 ZizhanPulseOps ops(){return {this,read,write,now,bad,data,nullptr,0,test==11?1u:0u,level};}
};
int main(){
 unsigned commands=0;
 for(unsigned test=0;test<12;++test){Device d;d.test=test;auto o=d.ops();ZizhanPulseReceipt r={};
  if(test==1){assert(zizhan_pulse_run(&o,&r)==ZizhanPulseGuard&&!d.commands);continue;}
  assert(!zizhan_pulse_patch(&o,&r,1)&&r.patch_on);d.regs[0x2014]=1;
  if(test==2){d.regs[0x13004]=1;}if(test==3)d.regs[0x28007d0]=1;if(test==4)d.regs[0x4c48]=0;
  if(test==5){d.regs[0x10010]|=0x100;}if(test==6)d.regs[0x10090]|=0x80000000;
  if(test==7)d.regs[0x6000]=0;
  int rc=zizhan_pulse_run(&o,&r);
  if(test==11){unsigned writes=r.attempted;assert(!rc&&r.complete&&!r.enabled&&r.disabled&&r.data_ack&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c&&writes==5);
   d.regs[0x2014]=0;assert(!zizhan_pulse_patch(&o,&r,0)&&r.patch_off);continue;}
  if(!test){assert(!rc&&r.complete&&r.enabled&&r.disabled&&r.data_ack&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);d.regs[0x2014]=0;
   assert(!zizhan_pulse_patch(&o,&r,0)&&r.patch_off&&r.attempted==16&&r.acknowledged==16);commands=d.commands;
  }else{assert(rc&&!r.complete);if(test>=8)assert(r.enabled&&r.disabled&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);else assert(!r.enabled);}
 }
 for(unsigned fail=1;fail<=commands;++fail){Device d;d.fail=fail;auto o=d.ops();ZizhanPulseReceipt r={};int rc=zizhan_pulse_patch(&o,&r,1);
  if(!rc){d.regs[0x2014]=1;rc=zizhan_pulse_run(&o,&r);}
  if(!rc){d.regs[0x2014]=0;rc=zizhan_pulse_patch(&o,&r,0);}
  assert(rc&&d.uncertain&&d.commands==fail);
 }
 /* Playback window split: arm/hold/disarm, dry, fault while held, guards, every transaction failing. */
 unsigned window=0;
 for(unsigned test=0;test<6;++test){Device d;d.test=test==1?11u:0u;auto o=d.ops();ZizhanPulseReceipt r={};
  assert(!zizhan_pulse_patch(&o,&r,1));d.regs[0x2014]=1;
  if(test==4){assert(zizhan_pulse_disarm(&o,&r,1)==ZizhanPulseGuard&&zizhan_pulse_hold_check(&o,&r)==ZizhanPulseGuard);}
  if(test==5)d.regs[0x13004]=1;
  int rc=zizhan_pulse_arm(&o,&r);
  if(test==5){assert(rc==ZizhanPulseGuard&&!r.armed&&!r.enabled&&d.regs[0x2018]==0x3300&&zizhan_pulse_disarm(&o,&r,1)==ZizhanPulseGuard);continue;}
  assert(!rc&&r.armed);
  if(test==1){assert(!r.enabled&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);}
  else{assert(r.enabled&&d.regs[0x2018]==0x3321&&d.regs[0x6000]==0x3f44);}
  assert(zizhan_pulse_arm(&o,&r)==ZizhanPulseGuard);
  for(unsigned i=0;i<20;++i){if(test==2&&i==7)d.regs[0x10010]|=0x8000;rc=zizhan_pulse_hold_check(&o,&r);if(rc)break;}
  if(test==2){assert(rc==ZizhanPulseGuard&&r.polls==8);}else{assert(!rc&&r.polls==20);}
  rc=zizhan_pulse_disarm(&o,&r,test!=3);
  assert(!rc&&r.disabled&&d.regs[0x2018]==0x3300&&d.regs[0x6000]==0x267c);
  assert(r.complete==(test!=3?1u:0u)&&r.data_ack==(test!=3?1u:0u));
  assert(zizhan_pulse_disarm(&o,&r,1)==ZizhanPulseGuard&&zizhan_pulse_hold_check(&o,&r)==ZizhanPulseGuard);
  d.regs[0x2014]=0;
  if(test==3){assert(zizhan_pulse_patch(&o,&r,0)==ZizhanPulseGuard&&!r.patch_off);continue;}  /* no pdn after a failed window, as in zizhan_pulse_run */
  assert(!zizhan_pulse_patch(&o,&r,0)&&r.patch_off);
  if(test==1)assert(r.attempted==10);else{assert(r.attempted==18&&r.acknowledged==18&&d.regs[0x4c00]==0&&!r.pcm_source);if(!test)window=d.commands;}
 }
 /* zizhan_amp_lvl=1: Android level (volume field 0, AMP_GAIN 0x253), restored on disarm; 20-write budget; unexpected gain refuses. */
 unsigned level_window=0;
 for(unsigned test=0;test<2;++test){Device d;d.level=1;if(test==1)d.regs[0x6c04]=0x1ff;auto o=d.ops();ZizhanPulseReceipt r={};
  assert(!zizhan_pulse_patch(&o,&r,1));d.regs[0x2014]=1;
  int rc=zizhan_pulse_arm(&o,&r);
  if(test==1){assert(rc==ZizhanPulseGuard&&!r.enabled&&d.regs[0x6000]==0x267c&&d.regs[0x6c04]==0x1ff);continue;}
  assert(!rc&&r.enabled&&r.gain_set&&r.original_gain==0xb3&&d.regs[0x6000]==0x4&&d.regs[0x6c04]==0x253&&d.regs[0x2018]==0x3321);
  for(unsigned i=0;i<20&&!rc;++i)rc=zizhan_pulse_hold_check(&o,&r);
  assert(!rc);rc=zizhan_pulse_disarm(&o,&r,1);
  assert(!rc&&r.complete&&!r.gain_set&&d.regs[0x6c04]==0xb3&&d.regs[0x6000]==0x267c&&d.regs[0x2018]==0x3300&&d.regs[0x4c00]==0);
  d.regs[0x2014]=0;assert(!zizhan_pulse_patch(&o,&r,0));
  assert(r.attempted==20&&r.acknowledged==20);level_window=d.commands;
 }
 for(unsigned fail=1;fail<=level_window;++fail){Device d;d.level=1;d.fail=fail;auto o=d.ops();ZizhanPulseReceipt r={};int rc=zizhan_pulse_patch(&o,&r,1);
  if(!rc){d.regs[0x2014]=1;rc=zizhan_pulse_arm(&o,&r);}
  for(unsigned i=0;!rc&&i<20;++i)rc=zizhan_pulse_hold_check(&o,&r);
  if(r.armed){const int off=zizhan_pulse_disarm(&o,&r,!rc);if(!rc)rc=off;}
  if(!rc){d.regs[0x2014]=0;rc=zizhan_pulse_patch(&o,&r,0);}
  assert(rc&&d.uncertain&&d.commands==fail);
 }
 for(unsigned fail=1;fail<=window;++fail){Device d;d.fail=fail;auto o=d.ops();ZizhanPulseReceipt r={};int rc=zizhan_pulse_patch(&o,&r,1);
  if(!rc){d.regs[0x2014]=1;rc=zizhan_pulse_arm(&o,&r);}
  for(unsigned i=0;!rc&&i<20;++i)rc=zizhan_pulse_hold_check(&o,&r);
  if(r.armed){const int off=zizhan_pulse_disarm(&o,&r,!rc);if(!rc)rc=off;}
  if(!rc){d.regs[0x2014]=0;rc=zizhan_pulse_patch(&o,&r,0);}
  assert(rc&&d.uncertain&&d.commands==fail);
 }
 puts("PASS nativepulse+androidlevel+window(arm/hold/disarm,dry,held fault,guards,all-transaction failure) guards/faults/latefault/datafailure/timeout/clearAMP-beforeBST/nativepup-pdn/16writebudget/alltransactionfailure retention;no hardware");
}
