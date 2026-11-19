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
 * backwards) is reported stale, but it **never prevents clamping**: any
 * valid over-temperature reading in it still sets the cap immediately. A
 * stale/backwards sample only blocks release and clears the recovery
 * streaks. A backwards timestamp re-bases the clock to the new time, so
 * later samples can become fresh again under the new baseline. Release
 * needs `release_samples` consecutive fresh samples below the release
 * threshold; a valid over-temperature reading clamps even when the other
 * bank is invalid. Missing readings are never turned into 0 C. */

enum {
 ZizhanThermalBanks=2u, ZizhanThermalSensors=16u,
 ZizhanThermalCpuSensors=13u, ZizhanThermalGpuSensors=2u,
 ZizhanThermalCpuLevels=20u, ZizhanThermalGpuLevels=12u,
 ZizhanThermalReleaseSamples=3u
};

enum { ZizhanThermalFresh=1, ZizhanThermalStale=0, ZizhanThermalInvalid=-1 };
enum { ZizhanThermalClockFresh=0, ZizhanThermalClockStale=1,
       ZizhanThermalClockBackwards=2 };

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
 /* Stepped clamp: each hot sample adds clamp_step levels (0 = jump straight to the lowest level, the v1 behaviour);
  * a reading at clamp + critical_margin_mc (10 C) or above always jumps to the lowest level. */
 uint8_t clamp_step;
 int32_t critical_margin_mc;
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
 c.clamp_step=0; c.critical_margin_mc=10000;
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

/* Boot-arg tuning, kept free of kernel APIs so host tests exercise the same
 * acceptance rules as the service: mode 0 off, 1 observe, 2 enforce;
 * test_cpu_c/test_gpu_c 0 = default clamp, otherwise 40..100 C and strictly
 * below the default clamp; load_s 0 = off, 1..120 s only with mode >= 1.
 * A rejected tuning leaves *out untouched so a partially applied threshold
 * can never reach the policy. */
enum {
 ZizhanThermalTuneOk=0, ZizhanThermalTuneBadMode=-1,
 ZizhanThermalTuneBadCpu=-2, ZizhanThermalTuneBadGpu=-3,
 ZizhanThermalTuneBadLoad=-4, ZizhanThermalTuneBadOutput=-5
};

typedef struct {
 uint8_t mode;
 uint32_t test_cpu_c, test_gpu_c, load_seconds;
 int32_t cpu_clamp_mc, cpu_release_mc, gpu_clamp_mc, gpu_release_mc;
} ZizhanThermalTuning;

/* Map a logical CPU cap level to a rank in an ascending usable-frequency
 * list: level 0 = no cap, level 20 = the lowest row. The caller walks its
 * LUT from the highest frequency down and takes the returned rank. */
static inline unsigned zizhan_thermal_cpu_rank(unsigned level,unsigned usable){
 if(!level||usable<2)return 0;
 if(level>ZizhanThermalCpuLevels)level=(unsigned)ZizhanThermalCpuLevels;
 return (level*(usable-1)+10u)/20u;
}

/* Select the cap frequency from the domain's LUT. The LUT is ascending;
 * zero rows are boost/unusable and are skipped. Level 20 returns the lowest
 * usable row, level 0 returns 0 (no cap). This is the exact scan the CPU
 * worker uses, kept here so the host tests cover its direction. */
static inline unsigned zizhan_thermal_cpu_cap_khz(unsigned level,const unsigned *khz,
                                                  unsigned levels){
 if(!level||!khz||levels<2)return 0;
 unsigned usable=0;
 for(unsigned i=0;i<levels;++i)if(khz[i])++usable;
 if(usable<2)return 0;
 const unsigned rank=zizhan_thermal_cpu_rank(level,usable);
 unsigned seen=0;
 for(unsigned i=levels;i-- >0;)if(khz[i]){
  if(seen==rank)return khz[i];
  ++seen;
 }
 return 0;
}

static inline int zizhan_thermal_tune(uint32_t mode,uint32_t test_cpu_c,
                                      uint32_t test_gpu_c,uint32_t load_s,
                                      ZizhanThermalTuning *out){
 ZizhanThermalTuning t;
 const ZizhanThermalConfig d=zizhan_thermal_default_config();
 if(!out)return ZizhanThermalTuneBadOutput;
 if(mode>2u)return ZizhanThermalTuneBadMode;
 if(test_cpu_c&&(test_cpu_c<40u||test_cpu_c>100u||
                 (int32_t)test_cpu_c*1000>=d.cpu_clamp_mc))
  return ZizhanThermalTuneBadCpu;
 if(test_gpu_c&&(test_gpu_c<40u||test_gpu_c>100u||
                 (int32_t)test_gpu_c*1000>=d.gpu_clamp_mc))
  return ZizhanThermalTuneBadGpu;
 if(load_s>120u||(load_s&&mode==0u))return ZizhanThermalTuneBadLoad;
 t.mode=(uint8_t)mode;
 t.test_cpu_c=test_cpu_c;t.test_gpu_c=test_gpu_c;t.load_seconds=load_s;
 t.cpu_clamp_mc=test_cpu_c?(int32_t)test_cpu_c*1000:d.cpu_clamp_mc;
 t.cpu_release_mc=test_cpu_c?t.cpu_clamp_mc-5000:d.cpu_release_mc;
 t.gpu_clamp_mc=test_gpu_c?(int32_t)test_gpu_c*1000:d.gpu_clamp_mc;
 t.gpu_release_mc=test_gpu_c?t.gpu_clamp_mc-5000:d.gpu_release_mc;
 *out=t;
 return ZizhanThermalTuneOk;
}

/* Bounded load bookkeeping. The service stops the busy threads on the
 * deadline or on a real reading at/above the hot threshold; both decisions
 * are pure here so the host tests can reproduce them. */
enum { ZizhanThermalLoadRun=0, ZizhanThermalLoadDeadline=1,
       ZizhanThermalLoadHot=2 };
#define ZIZHAN_THERMAL_LOAD_HOT_MC 75000
/* 11-18 device rounds: the first frame arrives while boot work holds the CPU
 * at 85-90 C, so a load started there hits the 75 C stop at once. Start it only
 * from a complete sample whose peak is below 60 C, and give up after 120 s. */
#define ZIZHAN_THERMAL_LOAD_START_MC 60000
#define ZIZHAN_THERMAL_LOAD_WAIT_US 120000000ull
enum {ZizhanThermalLoadWait=0,ZizhanThermalLoadGo=1,ZizhanThermalLoadGiveUp=2};
static inline int zizhan_thermal_load_gate(int desktop,int complete,int32_t peak_mc,uint64_t waited_us){
 if(waited_us>=ZIZHAN_THERMAL_LOAD_WAIT_US){return ZizhanThermalLoadGiveUp;}
 if(!desktop||!complete||peak_mc==INT32_MIN||peak_mc>=ZIZHAN_THERMAL_LOAD_START_MC){return ZizhanThermalLoadWait;}
 return ZizhanThermalLoadGo;
}

typedef struct {
 uint64_t start_us, deadline_us;
 uint8_t stopped, reason;
} ZizhanThermalLoad;

static inline void zizhan_thermal_load_begin(ZizhanThermalLoad *l,uint64_t now_us,
                                             uint32_t seconds){
 if(!l)return;
 l->start_us=now_us;
 l->deadline_us=now_us+(uint64_t)seconds*1000000ull;
 l->stopped=0;l->reason=(uint8_t)ZizhanThermalLoadRun;
}

static inline int zizhan_thermal_load_step(ZizhanThermalLoad *l,uint64_t now_us,
                                          int32_t max_mc,int32_t hot_mc){
 if(!l)return ZizhanThermalLoadDeadline;
 if(l->stopped)return (int)l->reason;
 if(now_us>=l->deadline_us){
  l->stopped=1;l->reason=(uint8_t)ZizhanThermalLoadDeadline;
  return ZizhanThermalLoadDeadline;
 }
 if(max_mc>=hot_mc){
  l->stopped=1;l->reason=(uint8_t)ZizhanThermalLoadHot;
  return ZizhanThermalLoadHot;
 }
 return ZizhanThermalLoadRun;
}

/* Classify the sample clock. Fresh: in order and at most two periods after
 * the last accepted one. Stale: in order but more than two periods later.
 * Backwards: before the last accepted time (out-of-order or 64-bit wrap).
 * Both non-fresh cases clear the streaks and move the baseline to now, so a
 * later sample can be fresh again; neither prevents a clamp in the caller. */
static inline int zizhan_thermal_time_ok(const ZizhanThermalConfig *c,ZizhanThermalState *s,
                                         uint64_t now){
 if(!s->have_last){s->last_time_us=now;s->have_last=1;return ZizhanThermalClockFresh;}
 if(now<s->last_time_us){
  s->last_time_us=now;s->cpu_fresh=0;s->gpu_fresh=0;return ZizhanThermalClockBackwards;
 }
 if(now-s->last_time_us>c->period_us*2u){
  s->last_time_us=now;s->cpu_fresh=0;s->gpu_fresh=0;return ZizhanThermalClockStale;
 }
 s->last_time_us=now;return ZizhanThermalClockFresh;
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

static inline uint8_t zizhan_thermal_raise(uint8_t level,uint8_t max,uint8_t step,int critical){
 if(!step||critical)return max;
 const unsigned next=(unsigned)level+step;
 return (uint8_t)(next>max?max:next);
}

/* Apply one sample. Returns ZizhanThermalFresh/Stale/Invalid and updates the
 * caps in *s. Invalid config leaves the state untouched. */
static inline int zizhan_thermal_step(const ZizhanThermalConfig *c,ZizhanThermalState *s,
                                      const ZizhanThermalSample *x){
 if(!s||!x||!zizhan_thermal_config_ok(c))return ZizhanThermalInvalid;
 const int banks_ok=x->bank_valid[0]&&x->bank_valid[1];
 const int clock=zizhan_thermal_time_ok(c,s,x->time_us);
 const unsigned need=c->release_samples?c->release_samples:1u;
 int32_t cpu_max,gpu_max;int cpu_all,cpu_any,gpu_all,gpu_any;
 zizhan_thermal_scan(x,(unsigned)ZizhanThermalCpuSensors,1,&cpu_max,&cpu_all,&cpu_any);
 zizhan_thermal_scan(x,(unsigned)ZizhanThermalGpuSensors,0,&gpu_max,&gpu_all,&gpu_any);
 if(cpu_any&&cpu_max>=c->cpu_clamp_mc){
  s->cpu_level=zizhan_thermal_raise(s->cpu_level,c->cpu_max_level,c->clamp_step,cpu_max>=c->cpu_clamp_mc+c->critical_margin_mc);
  s->cpu_fresh=0;
 }else if(clock==ZizhanThermalClockFresh&&s->cpu_level>0&&banks_ok&&cpu_all&&
          cpu_max<c->cpu_release_mc){
  if(++s->cpu_fresh>=need){if(s->cpu_level)--s->cpu_level;s->cpu_fresh=0;}
 }else s->cpu_fresh=0;
 if(gpu_any&&gpu_max>=c->gpu_clamp_mc){
  s->gpu_level=zizhan_thermal_raise(s->gpu_level,c->gpu_max_level,c->clamp_step,gpu_max>=c->gpu_clamp_mc+c->critical_margin_mc);
  s->gpu_fresh=0;
 }else if(clock==ZizhanThermalClockFresh&&s->gpu_level>0&&banks_ok&&gpu_all&&
          gpu_max<c->gpu_release_mc){
  if(++s->gpu_fresh>=need){if(s->gpu_level)--s->gpu_level;s->gpu_fresh=0;}
 }else s->gpu_fresh=0;
 return clock==ZizhanThermalClockFresh?ZizhanThermalFresh:ZizhanThermalStale;
}

#endif
