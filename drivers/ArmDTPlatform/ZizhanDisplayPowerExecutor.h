/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_DISPLAY_POWER_EXECUTOR_H
#define ZIZHAN_DISPLAY_POWER_EXECUTOR_H
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ZizhanDisplayPower.h"

/* POWER-02 S3 executor, kept free of kernel APIs so the host tests can drive
 * the same state machine with fake DSI/brightness operations.
 *
 * The real callbacks live in PongAdrenoScanout.cpp (short FIFO DCS under the
 * PongDsiPower lease) and PongBacklight.cpp (the existing work-loop brightness
 * write). Panel waits are the L18 live-DT values:
 *   off  0x28 wait 5 ms, 0x10 wait 120 ms
 *   on   0x11 wait 120 ms, 0x29 wait 120 ms
 * An off failure after the panel was touched always attempts the on sequence
 * and the saved brightness; the state machine completes failure so the stable
 * target stays On and no caller sees a successful off. */

typedef struct {
 int (*backlight_get)(uint32_t *level, void *context);
 int (*backlight_set)(uint32_t level, uint32_t timeout_ms, void *context);
 int (*dsi_sequence)(const uint8_t *dcs, const uint16_t *wait_ms, uint32_t count,
                     int low_power, void *context);
} ZizhanDisplayPowerOps;

typedef struct {
 ZizhanDisplayPower state;
 uint32_t saved_level;
 uint8_t have_saved;
} ZizhanDisplayPowerRuntime;

enum {
 ZizhanDisplayPowerOk=0,
 ZizhanDisplayPowerNoOp=1,
 ZizhanDisplayPowerFailed=-1,
 ZizhanDisplayPowerRejected=-2
};

enum {
 ZizhanDisplayPowerFaultNone=0,
 ZizhanDisplayPowerFaultBacklightGet=1,
 ZizhanDisplayPowerFaultBacklightSet=2,
 ZizhanDisplayPowerFaultDsi=3
};

typedef struct {
 int result;
 int fault;
 ZizhanDisplayTarget target;
 uint32_t sequence;
 uint8_t has_sequence;
 uint8_t restored;
 uint8_t dsi_failed;
 uint32_t saved_level;
} ZizhanDisplayPowerReport;

static const uint8_t ZizhanDisplayOffCommands[2] = {0x28, 0x10};
static const uint16_t ZizhanDisplayOffWaits[2] = {5, 120};
static const uint8_t ZizhanDisplayOnCommands[2] = {0x11, 0x29};
static const uint16_t ZizhanDisplayOnWaits[2] = {120, 120};

/* 0 = default off, 1 = run, -1 = unsupported boot-arg value. */
static inline int zizhan_display_power_mode(uint32_t enabled){
 if(!enabled)return 0;
 return enabled==1u?1:-1;
}

static inline void zizhan_display_power_runtime_init(ZizhanDisplayPowerRuntime *rt){
 if(!rt)return;
 zizhan_display_init(&rt->state,ZizhanDisplayStateOn);
 rt->saved_level=0;
 rt->have_saved=0;
}

static inline int zizhan_display_power_off(ZizhanDisplayPowerRuntime *rt,
                                           const ZizhanDisplayPowerOps *ops,void *context,
                                           ZizhanDisplayPowerReport *report){
 uint32_t level=0;
 if(ops->backlight_get(&level,context)){
  report->fault=ZizhanDisplayPowerFaultBacklightGet;
  return ZizhanDisplayPowerFailed;
 }
 rt->saved_level=level;rt->have_saved=1;report->saved_level=level;
 if(ops->backlight_set(0,1000,context)){
  report->fault=ZizhanDisplayPowerFaultBacklightSet;
  return ZizhanDisplayPowerFailed;
 }
 const int dsi=ops->dsi_sequence(ZizhanDisplayOffCommands,ZizhanDisplayOffWaits,2,1,context);
 if(!dsi)return ZizhanDisplayPowerOk;
 report->dsi_failed=1;
 report->fault=ZizhanDisplayPowerFaultDsi;
 const int wake=ops->dsi_sequence(ZizhanDisplayOnCommands,ZizhanDisplayOnWaits,2,1,context);
 const int brightness=wake==0?ops->backlight_set(level,1000,context):-1;
 report->restored=(wake==0&&brightness==0)?1u:0u;
 return ZizhanDisplayPowerFailed;
}

static inline int zizhan_display_power_on(ZizhanDisplayPowerRuntime *rt,
                                          const ZizhanDisplayPowerOps *ops,void *context,
                                          ZizhanDisplayPowerReport *report){
 const int dsi=ops->dsi_sequence(ZizhanDisplayOnCommands,ZizhanDisplayOnWaits,2,1,context);
 if(dsi){
  report->dsi_failed=1;
  report->fault=ZizhanDisplayPowerFaultDsi;
  return ZizhanDisplayPowerFailed;
 }
 if(ops->backlight_set(rt->saved_level,1000,context)){
  report->fault=ZizhanDisplayPowerFaultBacklightSet;
  return ZizhanDisplayPowerFailed;
 }
 return ZizhanDisplayPowerOk;
}

/* One request = at most one panel transaction. The S1 queue/sequence and
 * completion state decide duplicate/no-op, unknown/rejected and busy. */
static inline int zizhan_display_power_execute(ZizhanDisplayPowerRuntime *rt,
                                               const ZizhanDisplayPowerOps *ops,
                                               void *context,uint32_t count,
                                               const uint64_t *in,size_t bytes,
                                               uint64_t time_us,
                                               ZizhanDisplayPowerReport *report){
 ZizhanDisplayPowerReport local;
 if(!report)report=&local;
 memset(report,0,sizeof *report);
 report->result=ZizhanDisplayPowerRejected;
 if(!rt||!ops||!ops->backlight_get||!ops->backlight_set||!ops->dsi_sequence)
  return ZizhanDisplayPowerRejected;
 ZizhanDisplayReceipt r=zizhan_display_request(&rt->state,count,in,bytes,time_us);
 report->target=r.target;report->sequence=r.sequence;report->has_sequence=r.has_sequence;
 if(r.result==ZizhanDisplayNoOp){
  report->result=ZizhanDisplayPowerNoOp;
  return ZizhanDisplayPowerNoOp;
 }
 if(r.result!=ZizhanDisplayAccepted){
  report->result=ZizhanDisplayPowerRejected;
  return ZizhanDisplayPowerRejected;
 }
 zizhan_display_set_busy(&rt->state,1);
 const int rc=r.target==ZizhanDisplayTargetOff?
  zizhan_display_power_off(rt,ops,context,report):
  zizhan_display_power_on(rt,ops,context,report);
 ZizhanDisplayReceipt done;
 zizhan_display_complete(&rt->state,r.sequence,rc==ZizhanDisplayPowerOk,&done);
 zizhan_display_set_busy(&rt->state,0);
 report->result=rc;
 return rc;
}

#endif
