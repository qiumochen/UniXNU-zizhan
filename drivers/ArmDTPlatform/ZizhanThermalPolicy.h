/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_THERMAL_POLICY_H
#define ZIZHAN_THERMAL_POLICY_H
#include <stdint.h>

/* THERM-01 v1 policy core. Pure state machine: no MMIO, no kernel API, no
 * allocation and no clock. The caller supplies one TSENS sample; the core
 * returns the CPU/GPU cap levels to apply. Levels are 0 = unrestricted,
 * higher = more restrictive; cpu_max_level/gpu_max_level are the lowest
 * levels the board exposes. CPU levels are logical: the later wiring maps
 * them to the three EPSS/PDMEM domains (max_state 15/19/20), not one index.
 *
 * Sensor mapping is the live-DT one from the M2 plan (2026-11-17):
 *   tsens0 index 5..13 = cpu-1-0..8 (CPU)
 *   tsens1 index 1..4  = cpu-0-0..3 (CPU)
 *   tsens0 index 14,15 = gpuss-0/1 (GPU)
 * A sample carries per-bank validity and a per-sensor valid mask; a zero
 * bank-valid bit invalidates every sensor in that bank.
 *
 * Semantics: a sample older than two periods (or with a time that goes
 * backwards) is stale and holds the current caps; any invalid/missing bank
 * blocks release; a valid over-temperature reading clamps even when the
 * other bank is invalid. Release needs `release_samples` consecutive fresh
 * samples below the release threshold, and each released level starts a new
 * streak. Missing readings are never turned into 0 C. */

enum {
 ZizhanThermalBanks=2u, ZizhanThermalSensors=16u,
 ZizhanThermalCpuSensors=13u, ZizhanThermalGpuSensors=2u,
 ZizhanThermalCpuLevels=20u, ZizhanThermalGpuLevels=12u,
 ZizhanThermalReleaseSamples=3u
};

enum { ZizhanThermalFresh=1, ZizhanThermalStale=0, ZizhanThermalInvalid=-1 };

typedef struct {
 uint64_t time_us;
 uint8_t bank_valid[ZizhanThermalBanks];
 uint16_t valid_mask[ZizhanThermalBanks];
 int32_t temp_mc[ZizhanThermalBanks][ZizhanThermalSensors];
} ZizhanThermalSample;

typedef struct {
 uint64_t period_us;
 uint8_t cpu_max_level, gpu_max_level, release_samples;
 int32_t cpu_clamp_mc, cpu_release_mc;
 int32_t gpu_clamp_mc, gpu_release_mc;
} ZizhanThermalConfig;

typedef struct {
 uint8_t cpu_level, gpu_level;
 uint8_t cpu_fresh, gpu_fresh;
 uint64_t last_time_us;
 uint8_t have_last;
} ZizhanThermalState;

static inline ZizhanThermalConfig zizhan_thermal_default_config(void){
 ZizhanThermalConfig c;
 c.period_us=1000000u;
 c.cpu_max_level=(uint8_t)ZizhanThermalCpuLevels;
 c.gpu_max_level=(uint8_t)ZizhanThermalGpuLevels;
 c.release_samples=(uint8_t)ZizhanThermalReleaseSamples;
 c.cpu_clamp_mc=110000; c.cpu_release_mc=105000;
 c.gpu_clamp_mc=95000; c.gpu_release_mc=90000;
 return c;
}

static inline void zizhan_thermal_init(ZizhanThermalState *s){
 if(!s)return;
 s->cpu_level=0; s->gpu_level=0; s->cpu_fresh=0; s->gpu_fresh=0;
 s->last_time_us=0; s->have_last=0;
}

/* n-th CPU sensor -> (bank, index); 0 when n is out of range. */
static inline int zizhan_thermal_cpu_sensor(unsigned n,unsigned *bank,unsigned *index){
 if(!bank||!index)return 0;
 if(n<9u){*bank=0;*index=5u+n;return 1;}
 if(n<13u){*bank=1;*index=1u+(n-9u);return 1;}
 return 0;
}

/* n-th GPU sensor (0/1) -> tsens0 index 14/15. */
static inline int zizhan_thermal_gpu_sensor(unsigned n,unsigned *bank,unsigned *index){
 if(!bank||!index||n>=ZizhanThermalGpuSensors)return 0;
 *bank=0;*index=14u+n;return 1;
}

static inline int zizhan_thermal_config_ok(const ZizhanThermalConfig *c){
 if(!c||c->period_us==0||c->period_us>UINT64_MAX/2u)return 0;
 if(c->cpu_clamp_mc<=c->cpu_release_mc)return 0;
 if(c->gpu_clamp_mc<=c->gpu_release_mc)return 0;
 return 1;
}

/* Sample is stale when its time is before the last accepted one (out of
 * order or 64-bit wrap) or more than two periods after it. Stale holds the
 * caps and clears both streaks; a newer in-order sample updates the clock. */
static inline int zizhan_thermal_time_ok(const ZizhanThermalConfig *c,ZizhanThermalState *s,
                                         uint64_t now){
 if(!s->have_last){s->last_time_us=now;s->have_last=1;return 1;}
 if(now<s->last_time_us){s->cpu_fresh=0;s->gpu_fresh=0;return 0;}
 if(now-s->last_time_us>c->period_us*2u){
  s->last_time_us=now;s->cpu_fresh=0;s->gpu_fresh=0;return 0;
 }
 s->last_time_us=now;return 1;
}

/* Max valid temperature of one group; all_valid is cleared for any missing
 * or invalid sensor, any is set when at least one reading is usable. */
static inline void zizhan_thermal_scan(const ZizhanThermalSample *x,unsigned n,
                                       int cpu,int32_t *max_mc,int *all_valid,int *any){
 *max_mc=INT32_MIN;*all_valid=1;*any=0;
 for(unsigned i=0;i<n;++i){
  unsigned bank,index;
  if(cpu? !zizhan_thermal_cpu_sensor(i,&bank,&index)
        : !zizhan_thermal_gpu_sensor(i,&bank,&index)){*all_valid=0;continue;}
  if(!x->bank_valid[bank]||!(x->valid_mask[bank]&(uint16_t)(1u<<index))){
   *all_valid=0;continue;
  }
  if(x->temp_mc[bank][index]>*max_mc)*max_mc=x->temp_mc[bank][index];
  *any=1;
 }
}

/* Apply one sample. Returns ZizhanThermalFresh/Stale/Invalid and updates the
 * caps in *s. Invalid config leaves the state untouched. */
static inline int zizhan_thermal_step(const ZizhanThermalConfig *c,ZizhanThermalState *s,
                                      const ZizhanThermalSample *x){
 if(!s||!x||!zizhan_thermal_config_ok(c))return ZizhanThermalInvalid;
 const int banks_ok=x->bank_valid[0]&&x->bank_valid[1];
 if(!zizhan_thermal_time_ok(c,s,x->time_us))return ZizhanThermalStale;
 const unsigned need=c->release_samples?c->release_samples:1u;
 int32_t cpu_max,gpu_max;int cpu_all,cpu_any,gpu_all,gpu_any;
 zizhan_thermal_scan(x,(unsigned)ZizhanThermalCpuSensors,1,&cpu_max,&cpu_all,&cpu_any);
 zizhan_thermal_scan(x,(unsigned)ZizhanThermalGpuSensors,0,&gpu_max,&gpu_all,&gpu_any);
 if(cpu_any&&cpu_max>=c->cpu_clamp_mc){
  s->cpu_level=c->cpu_max_level;s->cpu_fresh=0;
 }else if(s->cpu_level>0&&banks_ok&&cpu_all&&cpu_max<c->cpu_release_mc){
  if(++s->cpu_fresh>=need){if(s->cpu_level)--s->cpu_level;s->cpu_fresh=0;}
 }else s->cpu_fresh=0;
 if(gpu_any&&gpu_max>=c->gpu_clamp_mc){
  s->gpu_level=c->gpu_max_level;s->gpu_fresh=0;
 }else if(s->gpu_level>0&&banks_ok&&gpu_all&&gpu_max<c->gpu_release_mc){
  if(++s->gpu_fresh>=need){if(s->gpu_level)--s->gpu_level;s->gpu_fresh=0;}
 }else s->gpu_fresh=0;
 return ZizhanThermalFresh;
}

#endif
