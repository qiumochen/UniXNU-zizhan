/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Track 1 (POWER-02): Deep screen-off platform sleep coordinator for Xiaomi MIX Fold 2 (SM8475).
 * Connects ArmCpu's platform sleep hook (armcpu_sleep_register) and IOPMrootDomain's sleep/wake
 * notifications to SM8475 wake sources (Hall lid GPIO 62 on TLMM summary INTID 240, PMK8350 PON
 * power key via SPMI observer, and CNTV wake/watchdog timer), peripheral quiesce/resume (L3/CPU
 * floor, CX/MX + NoC/DDR BCM votes, Wi-Fi 802.11 PS, ZizhanCpuOffline handoff), and fuel-gauge
 * coulomb-counter A/B battery telemetry. */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IOService.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IOMessage.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include "PongBootReset.h"
#include "ZizhanCpuOffline.h"
#include "ZizhanSleepPlan.h"

struct armcpu_sleep_board {
  void (*prepare)(void);
  int (*should_wake)(uint32_t intid);
  void (*restore)(void);
  const uint32_t *wake_spis;
  uint32_t wake_spi_count;
  int (*tick)(void);
  uint32_t tick_ms;
};
struct armcpu_sleep_stats {
  uint32_t phase, entries, spurious, last_intid, last_reason, cpu_off_failures;
  uint64_t asleep_ticks;
  uint32_t masked, ticks, unarmed;
};
static_assert(sizeof(armcpu_sleep_board) == 56, "armcpu_sleep_board layout mismatch");
static_assert(sizeof(armcpu_sleep_stats) == 48, "armcpu_sleep_stats layout mismatch");

extern "C" {
int armcpu_spi_attach(uint32_t intid, bool edge, void (*handler)(void *), void *context,
                      uint32_t *readback, uint64_t *router);
void armcpu_spi_set_awake_enable(uint32_t intid, bool enable);
int armcpu_sleep_enabled(void);
void armcpu_sleep_set_wake_after(uint32_t seconds);
int armcpu_sleep_register(const struct armcpu_sleep_board *board);
extern volatile struct armcpu_sleep_stats armcpu_sleep_stats;

int zizhan_pmrd_boot_complete(void);
int zizhan_pmrd_request_sleep(void);
void zizhan_pmrd_claim_wake(const char *reason, int user_active);

int pong_buttons_ready(void);
int pong_buttons_sleep_check(void);
void pong_touch_post(uint32_t type, uint32_t finger, uint32_t x, uint32_t y);
int zizhan_display_power_state(void);
extern volatile uint32_t pong_zc_flipped_tag __attribute__((weak));

void pong_cpu_policy_set_sleep(int asleep);
void zizhan_standby_rpmh_sleep(int low);
void pong_wifi_set_system_sleep(int asleep);
void zizhan_mpss_set_system_sleep(int asleep);
void zizhan_outer_panel_sleep(int asleep);
int pong_battery_request_refresh(int slow);
int pong_battery_gauge_snapshot(int32_t *charge_uah, int32_t *volt_mv, int32_t *curr_ma, uint32_t *cycles);
int zizhan_audio_sleep_busy(void);
int zizhan_cam_sleep_busy(void);
}

namespace zizhan_sleep {

constexpr uint64_t kTlmmPhys = 0x0f100000ULL;
constexpr uint32_t kGpioStride = 0x1000U;
constexpr uint32_t kIo = 0x4U, kIntrCfg = 0x8U, kIntrStatus = 0xcU;
static uint32_t gTickMs = 250U;

static IOMemoryMap *gTlmmMap;
static uintptr_t gTlmm;
static uint32_t gSavedCfg[ZIZHAN_SLEEP_GPIO_COUNT];
static uint32_t gAsleepCfg[ZIZHAN_SLEEP_GPIO_COUNT];
static const uint32_t kWakeSpis[] = {ZIZHAN_SLEEP_LID_INTID};

static volatile int gLidClosedAtSleep;
static volatile int gWakeSource;
static volatile uint32_t gTickCount;
static volatile uint32_t gWdtPingsInSleep;
static volatile uint32_t gSleepInProgress;
static volatile uint32_t gHoldLegacy;
static volatile uint32_t gWakeCompletedCount;
static volatile uint64_t gSleepEntryCntv;
static volatile uint64_t gLastAsleepMs;
static uint32_t gAutoSleepS = 4;
static uint32_t gWakeAfterS = 0;
static uint32_t gCycles = 0;
static uint32_t gStartDelayS = 15;
static uint32_t gAwakeBetweenS = 8;
static uint32_t gAbWindowS = 0;
static uint32_t gWakeLightDisplay = 1;

static uint32_t tlmmRead(uint32_t gpio, uint32_t off) {
  uint32_t v;
  __asm__ volatile("ldr %w0, [%1]" : "=r"(v) : "r"(gTlmm + uintptr_t(gpio) * kGpioStride + off) : "memory");
  return v;
}
static void tlmmWrite(uint32_t gpio, uint32_t off, uint32_t v) {
  __asm__ volatile("str %w0, [%1]" :: "r"(v), "r"(gTlmm + uintptr_t(gpio) * kGpioStride + off) : "memory");
}
static uint64_t cntfrq() {
  uint64_t hz = 0;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(hz));
  return hz ? hz : 19200000ULL;
}
static uint64_t cntvct() {
  uint64_t v = 0;
  __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(v));
  return v;
}
static uint64_t wallMs(uint64_t deltaTicks) {
  const uint64_t hz = cntfrq();
  return (deltaTicks * 1000ULL) / hz;
}

static void tlmmWakeIrq(void *) {
  /* Masked immediately if it ever fires after wake before GICD_ISENABLER restore completes. */
  armcpu_spi_set_awake_enable(ZIZHAN_SLEEP_LID_INTID, false);
}

static int lidClosedNow() {
  if (!gTlmm) return 0;
  return zizhan_sleep_lid_closed(tlmmRead(ZIZHAN_SLEEP_LID_GPIO, kIo));
}

static void boardPrepare() {
  gWakeSource = ZizhanWakeNone;
  gTickCount = 0;
  if (!gTlmm) return;
  for (uint32_t i = 0; i < ZIZHAN_SLEEP_GPIO_COUNT; ++i) {
    gSavedCfg[i] = zizhan_sleep_pin_usable(i) ? tlmmRead(i, kIntrCfg) : 0u;
  }
  zizhan_sleep_tlmm_keep(gSavedCfg, gAsleepCfg);
  const int closed = gLidClosedAtSleep || lidClosedNow();
  gLidClosedAtSleep = closed;
  gAsleepCfg[ZIZHAN_SLEEP_LID_GPIO] = zizhan_sleep_lid_cfg(gSavedCfg[ZIZHAN_SLEEP_LID_GPIO], closed);
  for (uint32_t i = 0; i < ZIZHAN_SLEEP_GPIO_COUNT; ++i) {
    if (zizhan_sleep_pin_usable(i) && gAsleepCfg[i] != gSavedCfg[i]) {
      tlmmWrite(i, kIntrCfg, gAsleepCfg[i]);
    }
  }
  tlmmWrite(ZIZHAN_SLEEP_LID_GPIO, kIntrStatus, 0u);
  __asm__ volatile("dsb sy" ::: "memory");
}

static int boardShouldWake(uint32_t intid) {
  if (intid == ZIZHAN_SLEEP_LID_INTID && gTlmm) {
    if (zizhan_sleep_lid_changed(tlmmRead(ZIZHAN_SLEEP_LID_GPIO, kIo), gLidClosedAtSleep)) {
      gWakeSource = ZizhanWakeLid;
      tlmmWrite(ZIZHAN_SLEEP_LID_GPIO, kIntrStatus, 0u);
      __asm__ volatile("dsb sy" ::: "memory");
      return 1;
    }
    tlmmWrite(ZIZHAN_SLEEP_LID_GPIO, kIntrStatus, 0u);
    __asm__ volatile("dsb sy" ::: "memory");
  }
  return 0;
}

static int boardTick() {
  const uint32_t idx = gTickCount++;
  const int deadlineSoon = pong_boot_reset::deadline_due_soon(8u) ? 1 : 0;
  const int pwr = pong_buttons_sleep_check();
  const int lid = gTlmm ? zizhan_sleep_lid_changed(tlmmRead(ZIZHAN_SLEEP_LID_GPIO, kIo), gLidClosedAtSleep) : 0;
  int doPing = 0;
  const int src = zizhan_sleep_tick_plan(idx, gTickMs, deadlineSoon, pwr, lid, &doPing);
  if (doPing) {
    pong_boot_reset::ping_from_sleep();
    gWdtPingsInSleep = gWdtPingsInSleep + 1;
  }
  if (src != ZizhanWakeNone) {
    gWakeSource = src;
    return 1;
  }
  return 0;
}

static void boardRestore() {
  if (!gTlmm) return;
  tlmmWrite(ZIZHAN_SLEEP_LID_GPIO, kIntrStatus, 0u);
  for (uint32_t i = 0; i < ZIZHAN_SLEEP_GPIO_COUNT; ++i) {
    if (zizhan_sleep_pin_usable(i) && gAsleepCfg[i] != gSavedCfg[i]) {
      tlmmWrite(i, kIntrCfg, gSavedCfg[i]);
    }
  }
  __asm__ volatile("dsb sy" ::: "memory");
}

static armcpu_sleep_board gBoard = {
    boardPrepare,
    boardShouldWake,
    boardRestore,
    kWakeSpis,
    1u,
    boardTick,
    250U,
};

static void postShortPowerKey() {
  pong_touch_post(4u, 0u, 0u, 0u);
  IOSleep(90);
  pong_touch_post(5u, 0u, 0u, 0u);
}

static void lightDisplayAfterWake(void *, wait_result_t) {
  IOSleep(400);
  if (zizhan_display_power_state() == 0 && !lidClosedNow()) {
    IOLog("ZIZHAN_SLEEP: lighting display after wake (source=%s)\n",
          zizhan_sleep_wake_name(armcpu_sleep_stats.last_reason, gWakeSource));
    postShortPowerKey();
  }
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static IOReturn pmNotify(void *, void *, natural_t message, IOService *, void *, vm_size_t) {
  switch (message) {
  case kIOMessageSystemWillSleep:
    __atomic_store_n(&gSleepInProgress, 1u, __ATOMIC_RELEASE);
    gLidClosedAtSleep = lidClosedNow();
    gSleepEntryCntv = cntvct();
    /* Re-online any user-offlined cores so IOCPUSleepKernel cleanly puts all 7 secondary cores (1..7)
     * into REASON_SYSTEM PSCI CPU_OFF and wakes all 7 on resume. */
    zizhan_cpu_offline::prepare_system_sleep();
    pong_wifi_set_system_sleep(1);
    zizhan_mpss_set_system_sleep(1);
    zizhan_outer_panel_sleep(1);
    pong_cpu_policy_set_sleep(1);
    zizhan_standby_rpmh_sleep(1);
    IOLog("ZIZHAN_SLEEP: WILL_SLEEP lid=%s power_key_ready=%d wake_after=%us tick_ms=%u\n",
          gLidClosedAtSleep ? "closed" : "open", pong_buttons_ready(), gWakeAfterS, gTickMs);
    break;
  case kIOMessageSystemWillPowerOn: {
    zizhan_standby_rpmh_sleep(0);
    pong_cpu_policy_set_sleep(0);
    pong_wifi_set_system_sleep(0);
    zizhan_mpss_set_system_sleep(0);
    zizhan_outer_panel_sleep(0);
    const char *why = zizhan_sleep_wake_name(armcpu_sleep_stats.last_reason, gWakeSource);
    const int userWake = (gWakeSource == ZizhanWakePower ||
                          (gWakeSource == ZizhanWakeLid && !lidClosedNow()) ||
                          (armcpu_sleep_stats.last_reason == 2u && gWakeLightDisplay)) ? 1 : 0;
    zizhan_pmrd_claim_wake(why, userWake);
    break;
  }
  case kIOMessageSystemHasPoweredOn: {
    const uint64_t asleepMs = wallMs(armcpu_sleep_stats.asleep_ticks);
    __atomic_store_n(&gLastAsleepMs, asleepMs, __ATOMIC_RELEASE);
    zizhan_cpu_offline::finish_system_wake();
    const char *why = zizhan_sleep_wake_name(armcpu_sleep_stats.last_reason, gWakeSource);
    IOLog("ZIZHAN_SLEEP: WOKE source=%s intid=%u asleep_ms=%llu entries=%u ticks=%u wdt_pings=%u spurious=%u masked=%u cpu_off_fail=%u\n",
          why, armcpu_sleep_stats.last_intid, static_cast<unsigned long long>(asleepMs),
          armcpu_sleep_stats.entries, armcpu_sleep_stats.ticks, gWdtPingsInSleep,
          armcpu_sleep_stats.spurious, armcpu_sleep_stats.masked, armcpu_sleep_stats.cpu_off_failures);
    __atomic_fetch_add(&gWakeCompletedCount, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&gSleepInProgress, 0u, __ATOMIC_RELEASE);
    const bool wantLight = (gWakeSource == ZizhanWakePower ||
                            (gWakeSource == ZizhanWakeLid && !lidClosedNow()) ||
                            (armcpu_sleep_stats.last_reason == 2u && gWakeLightDisplay && !gAbWindowS && !gCycles));
    if (wantLight) {
      thread_t th = THREAD_NULL;
      if (kernel_thread_start(lightDisplayAfterWake, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
    }
    break;
  }
  default:
    break;
  }
  return kIOReturnSuccess;
}

static bool sampleBatteryFresh(int32_t *q_uah, int32_t *v_mv, int32_t *i_ma) {
  uint32_t before = 0, cur = 0;
  (void)pong_battery_gauge_snapshot(nullptr, nullptr, nullptr, &before);
  if (pong_battery_request_refresh(0) == 0) {
    for (unsigned i = 0; i < 25; ++i) {
      IOSleep(100);
      if (pong_battery_gauge_snapshot(q_uah, v_mv, i_ma, &cur) == 0 && cur > before) return true;
    }
  }
  return pong_battery_gauge_snapshot(q_uah, v_mv, i_ma, &cur) == 0;
}

static bool waitDesktopReady() {
  for (unsigned i = 0; i < 600; ++i) {
    if (&pong_zc_flipped_tag && __atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE)) return true;
    IOSleep(100);
  }
  return false;
}

static void runAbBenchmark() {
  if (!waitDesktopReady()) {
    IOLog("ZIZHAN_SLEEP_AB: SKIP desktop-timeout\n");
    return;
  }
  IOSleep(25000); /* allow USB off (ZIZHAN_USB_OFF_AT=15), Wi-Fi, and ADSP battery to settle */
  /* Phase A: Legacy display-off standby (CPUs 2..7 off, CPU 0+1 & XNU awake) */
  __atomic_store_n(&gHoldLegacy, 1u, __ATOMIC_RELEASE);
  if (zizhan_display_power_state() != 0) postShortPowerKey();
  IOSleep(4000);
  int32_t qa0 = 0, va0 = 0, ia0 = 0, qa1 = 0, va1 = 0, ia1 = 0;
  const bool haveA0 = sampleBatteryFresh(&qa0, &va0, &ia0);
  const uint64_t ta0 = cntvct();
  IOLog("ZIZHAN_SLEEP_AB: phase=legacy_standby start dur_s=%u q0_uah=%d v0_mv=%d i0_ma=%d\n",
        gAbWindowS, qa0, va0, ia0);
  int64_t sumIa = 0, sumVa = 0;
  uint32_t cntA = 0;
  for (uint32_t s = 0; s < gAbWindowS && !pong_boot_reset::deadline_due_soon(25u); ++s) {
    IOSleep(1000);
    if ((s % 5u) == 4u) {
      int32_t qs = 0, vs = 0, is_ma = 0;
      uint32_t cyc = 0;
      if (pong_battery_gauge_snapshot(&qs, &vs, &is_ma, &cyc) == 0 && vs > 2500) {
        sumIa += is_ma;
        sumVa += vs;
        ++cntA;
      }
    }
  }
  const uint64_t dtA_ms = wallMs(cntvct() - ta0);
  const bool haveA1 = sampleBatteryFresh(&qa1, &va1, &ia1);
  /* Xiaomi SM8475 BmCharge reports mAh (1000 uAh steps), so combine gauge current mean with coulomb delta */
  const int32_t meanIa_ma = cntA ? int32_t(sumIa / cntA) : ((ia0 + ia1) / 2);
  const int32_t meanVa_mv = cntA ? int32_t(sumVa / cntA) : ((va0 + va1) / 2);
  const int32_t legacyDischargeMa = (meanIa_ma < 0) ? -meanIa_ma : 0;
  const int32_t legacyMw = int32_t((int64_t(legacyDischargeMa) * meanVa_mv + 500ll) / 1000ll);
  IOLog("ZIZHAN_SLEEP_AB: phase=legacy_standby end dt_ms=%llu q_mah=%d->%d v_mv=%d i_end_ma=%d samples=%u avg_ma=%d avg_mw=%d\n",
        static_cast<unsigned long long>(dtA_ms), qa0, qa1, meanVa_mv, ia1, cntA, legacyDischargeMa, legacyMw);

  /* Brief wake between A and B, then unlock IOPMrootDomain System Boot Complete for Phase B */
  if (zizhan_display_power_state() == 0) postShortPowerKey();
  IOSleep(6000);
  (void)zizhan_pmrd_boot_complete();
  IOSleep(1000);

  /* Phase B: True IOPMrootDomain platform sleep (CPUs 1..7 PSCI CPU_OFF, CPU 0 masked WFI, L3/CX/MX/Bus lowered) */
  __atomic_store_n(&gHoldLegacy, 0u, __ATOMIC_RELEASE);
  gWakeAfterS = gAbWindowS;
  armcpu_sleep_set_wake_after(gAbWindowS);
  if (zizhan_display_power_state() != 0) postShortPowerKey();
  IOSleep(2500);
  int32_t qb0 = 0, vb0 = 0, ib0 = 0, qb1 = 0, vb1 = 0, ib1 = 0;
  const bool haveB0 = sampleBatteryFresh(&qb0, &vb0, &ib0);
  (void)haveA0; (void)haveA1; (void)haveB0;
  const uint64_t tb0 = cntvct();
  const uint32_t wakesBefore = __atomic_load_n(&gWakeCompletedCount, __ATOMIC_ACQUIRE);
  IOLog("ZIZHAN_SLEEP_AB: phase=platform_sleep start dur_s=%u q0_mah=%d v0_mv=%d i0_ma=%d\n",
        gAbWindowS, qb0, vb0, ib0);
  const int reqRc = zizhan_pmrd_request_sleep();
  if (reqRc == 0) {
    for (unsigned w = 0; w < (gAbWindowS + 30u) * 10u; ++w) {
      if (__atomic_load_n(&gWakeCompletedCount, __ATOMIC_ACQUIRE) > wakesBefore) break;
      IOSleep(100);
    }
  }
  const uint64_t dtB_ms = wallMs(cntvct() - tb0);
  IOSleep(250); /* sample PM8350B IBAT EMA immediately on wake while display is still off */
  const bool haveB1 = sampleBatteryFresh(&qb1, &vb1, &ib1);
  (void)haveB1;
  /* Hardware PM8350B fuel gauge updates IBAT continuously across sleep; ib1 is the wake-instant sleep discharge */
  const int32_t sleepDischargeMa = (ib1 < 0) ? -ib1 : 0;
  const int32_t meanVb_mv = (vb0 > 2500 && vb1 > 2500) ? ((vb0 + vb1) / 2) : 4300;
  const int32_t sleepMw = int32_t((int64_t(sleepDischargeMa) * meanVb_mv + 500ll) / 1000ll);
  IOLog("ZIZHAN_SLEEP_AB: phase=platform_sleep end req_rc=%d dt_ms=%llu asleep_ms=%llu q_mah=%d->%d v1_mv=%d i1_ma=%d avg_ma=%d avg_mw=%d\n",
        reqRc, static_cast<unsigned long long>(dtB_ms),
        static_cast<unsigned long long>(__atomic_load_n(&gLastAsleepMs, __ATOMIC_ACQUIRE)),
        qb0, qb1, vb1, ib1, sleepDischargeMa, sleepMw);

  if (zizhan_display_power_state() == 0) postShortPowerKey();
  const int32_t savedPct = (legacyMw > 0 && sleepMw > 0)
      ? int32_t(((int64_t)(legacyMw - sleepMw) * 100ll) / legacyMw)
      : 0;
  IOLog("ZIZHAN_SLEEP_AB_SUMMARY: legacy_ma=%d legacy_mw=%d deep_sleep_ma=%d deep_sleep_mw=%d reduction_pct=%d wakes=%u\n",
        legacyDischargeMa, legacyMw, sleepDischargeMa, sleepMw, savedPct,
        __atomic_load_n(&gWakeCompletedCount, __ATOMIC_ACQUIRE) - wakesBefore);
  __atomic_store_n(&gHoldLegacy, 1u, __ATOMIC_RELEASE); /* keep awake after benchmark finishes */
}

static void runCyclesTest() {
  if (!waitDesktopReady()) {
    IOLog("ZIZHAN_SLEEP_CYCLES: SKIP desktop-timeout\n");
    return;
  }
  __atomic_store_n(&gHoldLegacy, 1u, __ATOMIC_RELEASE); /* coordinator drives explicit sleep requests per cycle */
  IOSleep(gStartDelayS * 1000u);
  if (gStartDelayS > 20u) {
    (void)zizhan_pmrd_boot_complete();
    IOSleep(500);
  }
  const uint32_t sleepS = gWakeAfterS ? gWakeAfterS : 12u;
  armcpu_sleep_set_wake_after(sleepS);
  gWakeAfterS = sleepS;

  uint32_t passed = 0;
  for (uint32_t c = 1; c <= gCycles && !pong_boot_reset::deadline_due_soon(20u); ++c) {
    const uint32_t wakes0 = __atomic_load_n(&gWakeCompletedCount, __ATOMIC_ACQUIRE);
    if (zizhan_display_power_state() != 0) {
      postShortPowerKey();
      for (unsigned w = 0; w < 40 && zizhan_display_power_state() != 0; ++w) IOSleep(100);
    }
    IOSleep(800);
    int32_t qc0 = 0, vc0 = 0, ic0 = 0, qc1 = 0, vc1 = 0, ic1 = 0;
    (void)sampleBatteryFresh(&qc0, &vc0, &ic0);
    const int rc = zizhan_pmrd_request_sleep();
    for (unsigned w = 0; w < (sleepS + 25u) * 10u; ++w) {
      if (__atomic_load_n(&gWakeCompletedCount, __ATOMIC_ACQUIRE) > wakes0) break;
      IOSleep(100);
    }
    const bool woke = __atomic_load_n(&gWakeCompletedCount, __ATOMIC_ACQUIRE) > wakes0;
    IOSleep(250);
    (void)sampleBatteryFresh(&qc1, &vc1, &ic1);
    const int32_t cycMa = (ic1 < 0) ? -ic1 : 0;
    const int32_t cycMw = int32_t((int64_t(cycMa) * ((vc1 > 2500) ? vc1 : 4300) + 500ll) / 1000ll);
    IOLog("ZIZHAN_SLEEP_BATT: cycle=%u/%u asleep_ms=%llu q_mah=%d->%d v_mv=%d->%d i0_ma=%d i1_ma=%d deep_sleep_ma=%d deep_sleep_mw=%d\n",
          c, gCycles, static_cast<unsigned long long>(__atomic_load_n(&gLastAsleepMs, __ATOMIC_ACQUIRE)),
          qc0, qc1, vc0, vc1, ic0, ic1, cycMa, cycMw);
    const uint32_t flip0 = &pong_zc_flipped_tag ? __atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE) : 0u;
    if (zizhan_display_power_state() == 0) {
      postShortPowerKey();
      for (unsigned w = 0; w < 40 && zizhan_display_power_state() != 1; ++w) IOSleep(100);
    }
    IOSleep(1500);
    const uint32_t flip1 = &pong_zc_flipped_tag ? __atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE) : 0u;
    const int dispOn = zizhan_display_power_state() == 1 ? 1 : 0;
    const bool ok = (rc == 0) && woke && dispOn;
    if (ok) ++passed;
    IOLog("ZIZHAN_SLEEP_CYCLES: cycle=%u/%u rc=%d woke=%d disp_on=%d flips=%u->%u asleep_ms=%llu %s\n",
          c, gCycles, rc, woke ? 1 : 0, dispOn, flip0, flip1,
          static_cast<unsigned long long>(__atomic_load_n(&gLastAsleepMs, __ATOMIC_ACQUIRE)),
          ok ? "PASS" : "FAIL");
    if (c < gCycles) {
      IOSleep(gAwakeBetweenS * 1000u);
    }
  }
  IOLog("ZIZHAN_SLEEP_CYCLES: DONE passed=%u/%u entries=%u cpu_off_fail=%u\n",
        passed, gCycles, armcpu_sleep_stats.entries, armcpu_sleep_stats.cpu_off_failures);
}

static void coordinator(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_sleep");
  uint32_t rb = 0;
  uint64_t route = 0;
  const int spiRc = armcpu_spi_attach(ZIZHAN_SLEEP_LID_INTID, false, tlmmWakeIrq, nullptr, &rb, &route);
  if (spiRc == kIOReturnSuccess) {
    armcpu_spi_set_awake_enable(ZIZHAN_SLEEP_LID_INTID, false);
  }
  const int regRc = armcpu_sleep_register(&gBoard);
  IONotifier *notifier = nullptr;
  for (unsigned tries = 0; tries < 120 && !notifier; ++tries) {
    if (IOService::getPMRootDomain()) {
      notifier = registerSleepWakeInterest(pmNotify, nullptr, nullptr);
    }
    if (!notifier) IOSleep(250);
  }
  /* Wait until desktop is up. Hold off zizhan_pmrd_boot_complete() during Phase A when running A/B benchmark. */
  (void)waitDesktopReady();
  IOSleep(5000);
  int bootRc = 0;
  if (gAbWindowS < 15 && !(gCycles > 0 && gStartDelayS > 20u)) {
    bootRc = zizhan_pmrd_boot_complete();
    IOSleep(500);
  }
  IOLog("ZIZHAN_SLEEP: RUNNING spi240=%d reg=%d notifier=%d pmrd_boot=%d autosleep_s=%u wake_s=%u cycles=%u ab_s=%u tick_ms=%u\n",
        spiRc, regRc, notifier ? 1 : 0, bootRc, gAutoSleepS, gWakeAfterS, gCycles, gAbWindowS, gTickMs);
  if (regRc != 0 || !notifier) {
    IOLog("ZIZHAN_SLEEP: FAIL prerequisite reg=%d notifier=%d\n", regRc, notifier ? 1 : 0);
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
    return;
  }

  if (gAbWindowS >= 15) {
    runAbBenchmark();
  } else if (gCycles > 0) {
    runCyclesTest();
  }

  uint32_t offSeconds = 0;
  for (;;) {
    IOSleep(1000);
    const int dispOff = (zizhan_display_power_state() == 0) ? 1 : 0;
    if (!dispOff) {
      offSeconds = 0;
      continue;
    }
    ++offSeconds;
    const int due = zizhan_autosleep_due(
        dispOff,
        int(__atomic_load_n(&gSleepInProgress, __ATOMIC_ACQUIRE)),
        int(__atomic_load_n(&gHoldLegacy, __ATOMIC_ACQUIRE)),
        zizhan_audio_sleep_busy(),
        zizhan_cam_sleep_busy(),
        pong_boot_reset::deadline_due_soon(15u) ? 1 : 0,
        offSeconds,
        gAutoSleepS);
    if (due) {
      offSeconds = 0;
      IOLog("ZIZHAN_SLEEP: auto-sleep triggered after %us display-off\n", gAutoSleepS);
      (void)zizhan_pmrd_request_sleep();
    }
  }
}

static void start(IOService *) {
  if (!armcpu_sleep_enabled()) return;
  if (PE_parse_boot_argn("zizhan_sleep_tick_ms", &gTickMs, sizeof(gTickMs))) {
    if (gTickMs < 50U) gTickMs = 50U;
    if (gTickMs > 1000U) gTickMs = 1000U;
  }
  gBoard.tick_ms = gTickMs;
  (void)PE_parse_boot_argn("zizhan_autosleep_s", &gAutoSleepS, sizeof(gAutoSleepS));
  if (!PE_parse_boot_argn("zizhan_sleep_wake_s", &gWakeAfterS, sizeof(gWakeAfterS))) {
    (void)PE_parse_boot_argn("platform-sleep-wake-after", &gWakeAfterS, sizeof(gWakeAfterS));
  }
  (void)PE_parse_boot_argn("zizhan_sleep_cycles", &gCycles, sizeof(gCycles));
  (void)PE_parse_boot_argn("zizhan_sleep_start_s", &gStartDelayS, sizeof(gStartDelayS));
  (void)PE_parse_boot_argn("zizhan_sleep_awake_s", &gAwakeBetweenS, sizeof(gAwakeBetweenS));
  (void)PE_parse_boot_argn("zizhan_sleep_ab", &gAbWindowS, sizeof(gAbWindowS));
  (void)PE_parse_boot_argn("zizhan_sleep_wake_display", &gWakeLightDisplay, sizeof(gWakeLightDisplay));
  if (gCycles > 50) gCycles = 50;
  if (gStartDelayS < 3) gStartDelayS = 3;
  if (gStartDelayS > 600) gStartDelayS = 600;
  if (gAwakeBetweenS < 3) gAwakeBetweenS = 3;

  IODeviceMemory *mem = IODeviceMemory::withRange(kTlmmPhys, IOByteCount(ZIZHAN_SLEEP_GPIO_COUNT) * kGpioStride);
  gTlmmMap = mem ? mem->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
  if (mem) mem->release();
  gTlmm = gTlmmMap ? static_cast<uintptr_t>(gTlmmMap->getVirtualAddress()) : 0;
  if (!gTlmm) {
    IOLog("ZIZHAN_SLEEP: FAIL tlmm-map\n");
    return;
  }
  thread_t th = THREAD_NULL;
  if (kernel_thread_start(coordinator, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
}

} // namespace zizhan_sleep
