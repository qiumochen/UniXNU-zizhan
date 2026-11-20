/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_POWER_INJECT_PLAN_H
#define ZIZHAN_POWER_INJECT_PLAN_H
#include <stdint.h>

/* POWER-02 S1 power-key injection schedule. Pure constants and helpers so the
 * host tests can prove the timing without kernel APIs. Two short presses:
 * DOWN, 120 ms, UP, 8 s, DOWN, 120 ms, UP, starting 20 s after the first
 * presented frame. The hold is far below the 3 s power-key reset, and the
 * presses are far apart. inject=1 only runs with observe=1. */
enum {
 ZizhanPowerInjectDelayMs=20000u,
 ZizhanPowerPressHoldMs=120u,
 ZizhanPowerPressGapMs=8000u,
 ZizhanPowerPressCount=2u
};

/* zizhan_power_inject=N (2..10): N off/on cycles (2N presses) 5 s apart, so ten cycles finish
 * well inside the 210 s autoreset window. N=1 keeps the S1 schedule above. */
enum {ZizhanPowerCyclesMax=10u,ZizhanPowerCycleGapMs=5000u};
/* zizhan_power_inject=100 (POWER-03 standby): one press turns the display off, the second press after
 * standby_s seconds (zizhan_power_standby_s, 30..1200, default 480) turns it back on. */
enum {ZizhanPowerStandbyMode=100u,ZizhanPowerStandbyDefaultS=480u,ZizhanPowerStandbyMinS=30u,ZizhanPowerStandbyMaxS=1200u};
static inline uint32_t zizhan_power_standby_s(uint32_t requested){
 if(!requested)return ZizhanPowerStandbyDefaultS;
 if(requested<ZizhanPowerStandbyMinS)return ZizhanPowerStandbyMinS;
 if(requested>ZizhanPowerStandbyMaxS)return ZizhanPowerStandbyMaxS;
 return requested;
}
static inline unsigned zizhan_power_inject_presses(uint32_t inject){
 if(inject==ZizhanPowerStandbyMode)return 2u;
 return inject>=2u&&inject<=ZizhanPowerCyclesMax?2u*inject:ZizhanPowerPressCount;
}
static inline uint32_t zizhan_power_inject_gap_ms_s(uint32_t inject,uint32_t standby_s){
 if(inject==ZizhanPowerStandbyMode)return zizhan_power_standby_s(standby_s)*1000u;
 return inject>=2u?ZizhanPowerCycleGapMs:ZizhanPowerPressGapMs;
}
static inline uint32_t zizhan_power_inject_gap_ms(uint32_t inject){return zizhan_power_inject_gap_ms_s(inject,0u);}

enum {
 ZizhanPowerInjectOff=0,
 ZizhanPowerInjectRun=1,
 ZizhanPowerInjectSkipObserve=2,
 ZizhanPowerInjectSkipMode=3
};

static inline int zizhan_power_inject_mode(uint32_t inject,uint32_t observe){
 if(!inject)return ZizhanPowerInjectOff;
 if(inject>ZizhanPowerCyclesMax&&inject!=ZizhanPowerStandbyMode)return ZizhanPowerInjectSkipMode;
 if(observe!=1u)return ZizhanPowerInjectSkipObserve;
 return ZizhanPowerInjectRun;
}

/* Milliseconds from the first DOWN (press 0) to this press's DOWN. */
static inline uint32_t zizhan_power_inject_down_ms(unsigned press){
 return press*(ZizhanPowerPressHoldMs+ZizhanPowerPressGapMs);
}

/* Milliseconds from the first DOWN to this press's UP. */
static inline uint32_t zizhan_power_inject_up_ms(unsigned press){
 return zizhan_power_inject_down_ms(press)+ZizhanPowerPressHoldMs;
}

#if defined(__cplusplus)
static_assert(ZizhanPowerPressHoldMs<=200u,"power press must stay below the 3 s reset hold");
static_assert(ZizhanPowerPressGapMs>=5000u,"power presses must be at least 5 s apart");
static_assert(ZizhanPowerPressCount==2u,"S1 injects exactly two presses");
#else
_Static_assert(ZizhanPowerPressHoldMs<=200u,"power press must stay below the 3 s reset hold");
_Static_assert(ZizhanPowerPressGapMs>=5000u,"power presses must be at least 5 s apart");
_Static_assert(ZizhanPowerPressCount==2u,"S1 injects exactly two presses");
#endif

#endif
