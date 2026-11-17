/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_AMP_PULSE_H
#define ZIZHAN_AMP_PULSE_H
#include <stdint.h>
/* Attended one-shot candidate only. Caller owns fresh native protection,
 * real IV route, default-off entry, clocks, data and finite waveform. No
 * streaming,calibration,OTP,automatic retry or gain search. */
typedef struct {
 void *context;
 int (*read)(void *,uint32_t,uint32_t *);
 int (*write)(void *,uint32_t,uint32_t);
 uint64_t (*now)(void *);
 int (*uncertain)(void *);
 int (*transfer)(void *);
} ZizhanPulseOps;
typedef struct {
 unsigned attempted,acknowledged,patch_on,patch_off,enabled,disabled,data_ack,complete,reason,reads;
 uint32_t original_volume,original_power,fault_sticky[2],fault_raw[2];uint64_t enabled_us;
} ZizhanPulseReceipt;
enum {ZizhanPulseIo=1,ZizhanPulseGuard,ZizhanPulseTimeout,ZizhanPulseReadback,ZizhanPulseData};
static inline int zizhan_pulse_read(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,uint32_t reg,uint32_t *v){
 ++r->reads;return o->read(o->context,reg,v)?0:ZizhanPulseIo;
}
static inline int zizhan_pulse_write(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,uint32_t reg,uint32_t v){
 if(r->attempted>=16)return ZizhanPulseGuard;++r->attempted;
 if(!o->write(o->context,reg,v))return ZizhanPulseIo;++r->acknowledged;return 0;
}
/* Exact original native pup/pdn patch. Parent calls on before GLOBAL_EN,
 * off after GLOBAL_EN0. A key-close ACK is not a hardware-lock claim. */
static inline int zizhan_pulse_patch(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,unsigned on){
 if(!o||!o->read||!o->write||!o->now||!o->uncertain||on>1||o->uncertain(o->context))return ZizhanPulseGuard;
 if((on&&(r->attempted||r->patch_on))||(!on&&(!r->patch_on||r->patch_off||!r->disabled||!r->complete)))return ZizhanPulseGuard;
 uint32_t value=0;int rc=zizhan_pulse_read(o,r,0x2014,&value);if(rc)return rc;if(value!=0)return ZizhanPulseGuard;
 rc=zizhan_pulse_read(o,r,0x2018,&value);if(rc)return rc;if(value!=0x3300)return ZizhanPulseGuard;
 const uint32_t regs[]={0x40,0x40,0x2084,0x40,0x40},vals[]={0x55,0xaa,on?0x2f1aa0u:0x2f1aa3u,0xcc,0x33};
 const uint64_t begin=o->now(o->context);
 for(unsigned i=0;i<5;++i){if(o->now(o->context)-begin>=100000)return ZizhanPulseTimeout;
  rc=zizhan_pulse_write(o,r,regs[i],vals[i]);if(rc)return rc;
  if(i==2){rc=zizhan_pulse_read(o,r,0x2084,&value);if(rc)return rc;if(value!=vals[i])return ZizhanPulseReadback;}}
 if(on)r->patch_on=1;else r->patch_off=1;return 0;
}
static inline int zizhan_pulse_faults(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,unsigned at){
 int rc=zizhan_pulse_read(o,r,0x10010,&r->fault_sticky[at]);if(rc)return rc;
 rc=zizhan_pulse_read(o,r,0x10090,&r->fault_raw[at]);if(rc)return rc;
 // Original native amp-short,boost-short/OVP/UVP,overtemp/warn bits.
 if((r->fault_sticky[at]|r->fault_raw[at])&0x800281c0u)return ZizhanPulseGuard;
 return 0;
}
static inline int zizhan_pulse_run(const ZizhanPulseOps *o,ZizhanPulseReceipt *r){
 if(!o||!o->read||!o->write||!o->now||!o->uncertain||!o->transfer||!r->patch_on||r->enabled||r->complete||o->uncertain(o->context))return ZizhanPulseGuard;
 uint32_t value=0;const uint32_t regs[]={0x2014,0x2018,0x2bc1000,0x4800,0x13004,0x28007d0,0x4c48,0x4c4c,0x6000};
 const uint32_t expected[]={1,0x3300,1,0x10000,0,2,0x18,0x19,0};int rc=0;
 for(unsigned i=0;i<9;++i){rc=zizhan_pulse_read(o,r,regs[i],&value);if(rc)return rc;
  if(i==8)r->original_volume=value;else if(value!=expected[i])return ZizhanPulseGuard;}
 if((r->original_volume&0x3fffu)!=0x267cu)return ZizhanPulseGuard;
 r->original_power=0x3300;rc=zizhan_pulse_faults(o,r,0);if(rc)return rc;
 // Native index793 = -12dB (817 is0dB); fixed finite -36dBFS data.
 const uint32_t volume=(r->original_volume&~0x3ff8u)|0x3f40u;
 rc=zizhan_pulse_write(o,r,0x6000,volume);if(rc)return rc;
 rc=zizhan_pulse_read(o,r,0x6000,&value);if(rc)return rc;if(value!=volume)return ZizhanPulseReadback;
 const uint64_t begin=o->now(o->context);unsigned maybe_on=1;
 rc=zizhan_pulse_write(o,r,0x2018,0x3320);if(rc)goto stop;
 rc=zizhan_pulse_read(o,r,0x2018,&value);if(rc)goto stop;if(value!=0x3320){rc=ZizhanPulseReadback;goto stop;}
 rc=zizhan_pulse_write(o,r,0x2018,0x3321);if(rc)goto stop;
 rc=zizhan_pulse_read(o,r,0x2018,&value);if(rc)goto stop;if(value!=0x3321){rc=ZizhanPulseReadback;goto stop;}r->enabled=1;
 if(o->now(o->context)-begin>=100000){rc=ZizhanPulseTimeout;goto stop;}
 if(o->transfer(o->context)!=1){rc=ZizhanPulseData;goto stop;}r->data_ack=1;
 if(o->now(o->context)-begin>=250000){rc=ZizhanPulseTimeout;goto stop;}
 rc=zizhan_pulse_faults(o,r,1);
stop:
 // On uncertainty,the caller's real shared reset immediately contains the
 // output; no further I2C attempt. Known bus failures must clear AMP first.
 if(maybe_on&&!o->uncertain(o->context)){
  int cleanup=zizhan_pulse_write(o,r,0x2018,0x3320);
  if(!cleanup)cleanup=zizhan_pulse_write(o,r,0x2018,0x3300);
  if(!cleanup)cleanup=zizhan_pulse_read(o,r,0x2018,&value);
  if(!cleanup&&value!=0x3300)cleanup=ZizhanPulseReadback;
  if(!cleanup){r->disabled=1;cleanup=zizhan_pulse_write(o,r,0x6000,r->original_volume);}
  if(!cleanup)cleanup=zizhan_pulse_read(o,r,0x6000,&value);
  if(!cleanup&&value!=r->original_volume)cleanup=ZizhanPulseReadback;
  if(!rc)rc=cleanup;
 }
 r->enabled_us=o->now(o->context)-begin;r->reason=rc;if(!rc&&r->disabled&&r->data_ack)r->complete=1;return rc;
}
#endif
