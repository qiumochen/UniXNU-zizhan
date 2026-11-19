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
 /* Optional (attended diagnostics): keep AMP enabled hold_us after the data ack so the
  * DSP pipeline can play the finite tone out; NULL pause or 0 keeps the original timing. */
 void (*pause)(void *,unsigned);
 unsigned hold_us;
 /* Silent diagnostics: never write AMP/BST enable or volume; only the data transfer and fault reads. */
 unsigned dry;
 /* Playback window only (zizhan_amp_lvl=1, attended): Android's level, digital volume 0 dB (0x6000 field 0)
  * and AMP_GAIN_PCM 18 (0x6c04 = 0x253, 18.5 dB), both restored on disarm. */
 unsigned android_level;
} ZizhanPulseOps;
typedef struct {
 unsigned attempted,acknowledged,patch_on,patch_off,enabled,disabled,data_ack,complete,reason,reads;
 unsigned armed,polls,pcm_source; /* playback window only (zizhan_pulse_arm/hold_check/disarm) */
 uint32_t original_volume,original_power,fault_sticky[2],fault_raw[2];uint64_t enabled_us;
 uint32_t original_gain;unsigned gain_set;
} ZizhanPulseReceipt;
enum {ZizhanPulseIo=1,ZizhanPulseGuard,ZizhanPulseTimeout,ZizhanPulseReadback,ZizhanPulseData};
static inline int zizhan_pulse_read(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,uint32_t reg,uint32_t *v){
 ++r->reads;return o->read(o->context,reg,v)?0:ZizhanPulseIo;
}
static inline int zizhan_pulse_write(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,uint32_t reg,uint32_t v){
 if(r->attempted>=20){return ZizhanPulseGuard;}++r->attempted;
 if(!o->write(o->context,reg,v)){return ZizhanPulseIo;}++r->acknowledged;return 0;
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
 if(o->dry){const uint64_t start=o->now(o->context);
  if(o->transfer(o->context)!=1)rc=ZizhanPulseData;
  else{r->data_ack=1;rc=o->now(o->context)-start>=250000?ZizhanPulseTimeout:zizhan_pulse_faults(o,r,1);}
  r->disabled=1;r->enabled_us=o->now(o->context)-start;r->reason=rc;if(!rc)r->complete=1;return rc;}
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
 if(o->pause&&o->hold_us&&o->hold_us<=300000u){
  for(unsigned waited=0;waited<o->hold_us&&!o->uncertain(o->context);waited+=10000u){o->pause(o->context,10000u);}
 }
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
/* Playback window (docs/plan/2026-11-18-m1-playback-amp.md): the reads, checks and writes of
 * zizhan_pulse_run, split so both amps can be enabled, held while data streams, polled for faults
 * and disabled. Same volume (-12 dB index), same enable sequence and readbacks, same cleanup order,
 * same 18-write budget (16 + DACPCM1 source on/off). Dry: identical reads and fault checks, no volume or AMP write. */
static inline int zizhan_pulse_arm(const ZizhanPulseOps *o,ZizhanPulseReceipt *r){
 if(!o||!r||!o->read||!o->write||!o->now||!o->uncertain||!r->patch_on||r->armed||r->enabled||r->complete||o->uncertain(o->context))return ZizhanPulseGuard;
 uint32_t value=0;const uint32_t regs[]={0x2014,0x2018,0x2bc1000,0x4800,0x13004,0x28007d0,0x4c48,0x4c4c,0x6000};
 const uint32_t expected[]={1,0x3300,1,0x10000,0,2,0x18,0x19,0};int rc=0;
 for(unsigned i=0;i<9;++i){rc=zizhan_pulse_read(o,r,regs[i],&value);if(rc){return rc;}
  if(i==8){r->original_volume=value;}else if(value!=expected[i]){return ZizhanPulseGuard;}}
 if((r->original_volume&0x3fffu)!=0x267cu){return ZizhanPulseGuard;}
 r->original_power=0x3300;rc=zizhan_pulse_faults(o,r,0);if(rc){return rc;}
 /* Android level: the original gain is checked read-only first, dry included. */
 if(o->android_level){
  rc=zizhan_pulse_read(o,r,0x6c04,&r->original_gain);if(rc){return rc;}
  if((r->original_gain&~0x3e0u)!=0x13u){return ZizhanPulseGuard;}
 }
 r->armed=1;r->enabled_us=o->now(o->context);
 if(o->dry){return 0;}
 const uint32_t volume=(r->original_volume&~0x3ff8u)|(o->android_level?0u:0x3f40u);
 rc=zizhan_pulse_write(o,r,0x6000,volume);if(rc){return rc;}
 rc=zizhan_pulse_read(o,r,0x6000,&value);if(rc){return rc;}if(value!=volume){return ZizhanPulseReadback;}
 if(o->android_level){
  rc=zizhan_pulse_write(o,r,0x6c04,0x253);if(rc){return rc;}r->gain_set=1;
  rc=zizhan_pulse_read(o,r,0x6c04,&value);if(rc){return rc;}if(value!=0x253){return ZizhanPulseReadback;}
 }
 /* DACPCM1 source: reset value 0 feeds the DAC silence. 0x08 = ASPRX1, as the vendor "PCM Source:ASP". */
 rc=zizhan_pulse_write(o,r,0x4c00,0x08);if(rc){return rc;}
 rc=zizhan_pulse_read(o,r,0x4c00,&value);if(rc){return rc;}if(value!=0x08){return ZizhanPulseReadback;}
 r->pcm_source=1;
 rc=zizhan_pulse_write(o,r,0x2018,0x3320);if(rc){return rc;}
 rc=zizhan_pulse_read(o,r,0x2018,&value);if(rc){return rc;}if(value!=0x3320){return ZizhanPulseReadback;}
 rc=zizhan_pulse_write(o,r,0x2018,0x3321);if(rc){return rc;}
 rc=zizhan_pulse_read(o,r,0x2018,&value);if(rc){return rc;}if(value!=0x3321){return ZizhanPulseReadback;}
 r->enabled=1;return 0;
}
/* Called every poll while data streams: any sticky or raw protection fault stops the window. */
static inline int zizhan_pulse_hold_check(const ZizhanPulseOps *o,ZizhanPulseReceipt *r){
 if(!o||!r||!r->armed||r->disabled){return ZizhanPulseGuard;}
 if(o->uncertain(o->context)){return ZizhanPulseIo;}
 ++r->polls;return zizhan_pulse_faults(o,r,1);
}
/* Always called once per armed amp, also after a failed arm or a fault. Uncertain bus: no I2C at all,
 * the caller's shared reset contains the output (same rule as zizhan_pulse_run). */
static inline int zizhan_pulse_disarm(const ZizhanPulseOps *o,ZizhanPulseReceipt *r,int data_ok){
 if(!o||!r||!r->armed||r->disabled){return ZizhanPulseGuard;}
 int rc=0;uint32_t value=0;
 if(o->uncertain(o->context)){rc=ZizhanPulseIo;}
 else if(o->dry){r->disabled=1;}
 else{
  if(r->pcm_source){rc=zizhan_pulse_write(o,r,0x4c00,0);if(!rc){r->pcm_source=0;}}
  if(!rc&&r->gain_set){rc=zizhan_pulse_write(o,r,0x6c04,r->original_gain);if(!rc){r->gain_set=0;}}
  if(!rc){rc=zizhan_pulse_write(o,r,0x2018,0x3320);}
  if(!rc){rc=zizhan_pulse_write(o,r,0x2018,0x3300);}
  if(!rc){rc=zizhan_pulse_read(o,r,0x2018,&value);}
  if(!rc&&value!=0x3300){rc=ZizhanPulseReadback;}
  if(!rc){r->disabled=1;rc=zizhan_pulse_write(o,r,0x6000,r->original_volume);}
  if(!rc){rc=zizhan_pulse_read(o,r,0x6000,&value);}
  if(!rc&&value!=r->original_volume){rc=ZizhanPulseReadback;}
 }
 r->enabled_us=o->now(o->context)-r->enabled_us;r->data_ack=data_ok?1u:0u;r->reason=rc;
 if(!rc&&r->disabled&&data_ok){r->complete=1;}
 return rc;
}
#endif
