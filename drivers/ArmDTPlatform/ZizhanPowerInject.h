// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan POWER-02 S1: default-off, two short power-key injections after the
// first presented frame. This only feeds the existing /dev/pong-buttons queue;
// it writes no PMIC/display register, does not call the reset hook and does
// not suppress a real panic. The hold is 120 ms, far below the 3 s reset.
#pragma once
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include "ZizhanDailyProfile.h"
#include "ZizhanPowerInjectPlan.h"

extern "C" struct vnode *rootvnode;
// PongAdrenoScanout.cpp: nonzero after the first presented frame. Weak so a
// reduced build without scanout skips injection instead of guessing a desktop.
extern "C" volatile uint32_t pong_zc_flipped_tag __attribute__((weak));
// From patches/xnu-ipados18/0001-pong-darwin24.patch (bsd/sys/pong_input.h,
// bsd/dev/pong_input.c). The same call and codes are used by pong_buttons.c.
extern "C" void pong_touch_post(uint32_t type, uint32_t finger, uint32_t x, uint32_t y);
enum : uint32_t { PongButtonDown = 4u, PongButtonUp = 5u, PongButtonPower = 0u };

namespace zizhan_power_inject {
static uint32_t inject_value = 1;

static uint64_t uptime_ms(void) {
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
  return ns / 1000000u;
}

static void finish(void) {
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static bool wait_root(void) {
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i)
    IOSleep(100);
  return __atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE) != nullptr;
}

static bool wait_desktop(void) {
  for (unsigned i = 0; i < 600; ++i) {
    if (__atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE)) return true;
    IOSleep(100);
  }
  return false;
}

static void worker(void *, wait_result_t) {
  if (!wait_root()) {
    IOLog("ZIZHAN_POWER_INJECT: SKIP root-timeout\n");
    finish();
    return;
  }
  if (!&pong_zc_flipped_tag) {
    IOLog("ZIZHAN_POWER_INJECT: SKIP no-desktop-signal\n");
    finish();
    return;
  }
  if (!wait_desktop()) {
    IOLog("ZIZHAN_POWER_INJECT: SKIP desktop-timeout\n");
    finish();
    return;
  }
  IOSleep(ZizhanPowerInjectDelayMs);
  const unsigned presses = zizhan_power_inject_presses(inject_value);
  uint32_t standby_s = 0;
  (void)PE_parse_boot_argn("zizhan_power_standby_s", &standby_s, sizeof standby_s);
  const uint32_t gap_ms = zizhan_power_inject_gap_ms_s(inject_value, standby_s);
  for (unsigned press = 0; press < presses; ++press) {
    const uint64_t down = uptime_ms();
    IOLog("ZIZHAN_POWER_INJECT: press=%u phase=down t_ms=%llu\n",
          press + 1, (unsigned long long)down);
    pong_touch_post(PongButtonDown, PongButtonPower, 0, 0);
    IOSleep(ZizhanPowerPressHoldMs);
    const uint64_t up = uptime_ms();
    pong_touch_post(PongButtonUp, PongButtonPower, 0, 0);
    IOLog("ZIZHAN_POWER_INJECT: press=%u phase=up t_ms=%llu\n",
          press + 1, (unsigned long long)up);
    if (press + 1 < presses) IOSleep(gap_ms);
  }
  IOLog("ZIZHAN_POWER_INJECT: DONE presses=%u\n", presses);
  finish();
}

static void start(void) {
  uint32_t inject = 0, observe = 0;
  if (!PE_parse_boot_argn("zizhan_power_inject", &inject, sizeof inject) || !inject)
    return;
  (void)zizhan_daily::arg("zizhan_power_observe", &observe, zizhan_daily::kPowerObserve);
  const int mode = zizhan_power_inject_mode(inject, observe);
  if (mode == ZizhanPowerInjectSkipObserve) {
    IOLog("ZIZHAN_POWER_INJECT: SKIP observe-required inject=%u observe=%u\n",
          (unsigned)inject, (unsigned)observe);
    return;
  }
  if (mode == ZizhanPowerInjectSkipMode) {
    IOLog("ZIZHAN_POWER_INJECT: SKIP unsupported-mode inject=%u\n",
          (unsigned)inject);
    return;
  }
  if (mode != ZizhanPowerInjectRun) return;
  inject_value = inject;
  thread_t thread = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
    IOLog("ZIZHAN_POWER_INJECT: FAIL worker-start\n");
    return;
  }
  IOLog("ZIZHAN_POWER_INJECT: RUNNING presses=%u hold_ms=%u gap_ms=%u delay_ms=%u\n",
        zizhan_power_inject_presses(inject), (unsigned)ZizhanPowerPressHoldMs,
        (unsigned)zizhan_power_inject_gap_ms(inject), (unsigned)ZizhanPowerInjectDelayMs);
  thread_deallocate(thread);
}

} // namespace zizhan_power_inject
