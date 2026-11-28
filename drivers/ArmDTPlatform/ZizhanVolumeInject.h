// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan IO-01: default-off synthetic physical volume-key injector on /dev/pong-buttons.
// Feeds the exact same pong_touch_post(PONG_BUTTON_DOWN/UP, PONG_BUTTON_VOLUME_UP/DOWN)
// queue used by pong_buttons.c, exercising backboardd -> volume18 -> SBVolumeControl -> CoreAudio.
#pragma once
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include "ZizhanVolumeInjectPlan.h"

extern "C" struct vnode *rootvnode;
extern "C" volatile uint32_t pong_zc_flipped_tag __attribute__((weak));
extern "C" void pong_touch_post(uint32_t type, uint32_t finger, uint32_t x, uint32_t y);
extern "C" unsigned pong_volume_key_sample(uint32_t code, int down, uint32_t dt_ms);

namespace zizhan_vol_inject {
enum : uint32_t { VolButtonDown = 4u, VolButtonUp = 5u };
static uint32_t g_mode = 0;
static uint32_t g_delay_s = ZizhanVolDelayDefaultS;

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
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i) {
    IOSleep(100);
  }
  return __atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE) != nullptr;
}

static bool wait_desktop(void) {
  for (unsigned i = 0; i < 600; ++i) {
    if (__atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE)) {
      return true;
    }
    IOSleep(100);
  }
  return false;
}

static void worker(void *, wait_result_t) {
  if (!wait_root()) {
    IOLog("ZIZHAN_VOL_INJECT: SKIP root-timeout\n");
    finish();
    return;
  }
  if (!&pong_zc_flipped_tag) {
    IOLog("ZIZHAN_VOL_INJECT: SKIP no-desktop-signal\n");
    finish();
    return;
  }
  if (!wait_desktop()) {
    IOLog("ZIZHAN_VOL_INJECT: SKIP desktop-timeout\n");
    finish();
    return;
  }
  IOSleep(g_delay_s * 1000u);
  if (g_mode == ZizhanVolInjectPaced) {
    struct StepSpec { uint32_t key; uint32_t hold_ms; uint32_t gap_ms; };
    const StepSpec specs[3] = {
      { ZizhanVolKeyDown, ZizhanVolHumanClickMs, ZizhanVolClickGapMs },
      { ZizhanVolKeyDown, ZizhanVolHumanClickMs, ZizhanVolDirGapMs },
      { ZizhanVolKeyUp, ZizhanVolLongHoldMs, 0u }
    };
    unsigned pulses_total = 0;
    for (unsigned s = 0; s < 3u; ++s) {
      const char *name = (specs[s].key == ZizhanVolKeyUp) ? "up" : "down";
      unsigned pulses = pong_volume_key_sample(specs[s].key, 1, 0u);
      for (uint32_t elapsed = 0; elapsed < specs[s].hold_ms; elapsed += 16u) {
        IOSleep(16);
        pulses += pong_volume_key_sample(specs[s].key, 1, 16u);
      }
      (void)pong_volume_key_sample(specs[s].key, 0, 0u);
      pulses_total += pulses;
      IOLog("ZIZHAN_VOL_INJECT: paced=%u key=%s hold_ms=%u pulses=%u t_ms=%llu\n",
            s + 1u, name, specs[s].hold_ms, pulses, (unsigned long long)uptime_ms());
      if (specs[s].gap_ms) {
        IOSleep(specs[s].gap_ms);
      }
    }
    IOLog("ZIZHAN_VOL_INJECT: DONE mode=3 pulses=%u\n", pulses_total);
    finish();
    return;
  }
  const unsigned total = zizhan_vol_total_clicks(g_mode);
  for (unsigned i = 0; i < total; ++i) {
    const uint32_t key = zizhan_vol_click_key(g_mode, i);
    const char *name = (key == ZizhanVolKeyUp) ? "up" : "down";
    IOLog("ZIZHAN_VOL_INJECT: click=%u key=%s phase=down t_ms=%llu\n",
          i + 1u, name, (unsigned long long)uptime_ms());
    pong_touch_post(VolButtonDown, key, 0, 0);
    IOSleep(ZizhanVolHoldMs);
    pong_touch_post(VolButtonUp, key, 0, 0);
    IOLog("ZIZHAN_VOL_INJECT: click=%u key=%s phase=up t_ms=%llu\n",
          i + 1u, name, (unsigned long long)uptime_ms());
    const uint32_t gap = zizhan_vol_after_gap_ms(g_mode, i);
    if (gap) {
      IOSleep(gap);
    }
  }
  IOLog("ZIZHAN_VOL_INJECT: DONE mode=%u up=%u down=%u\n",
        (unsigned)g_mode, zizhan_vol_up_clicks(g_mode), zizhan_vol_down_clicks(g_mode));
  finish();
}

static void start(void) {
  uint32_t inject = 0;
  if (!PE_parse_boot_argn("zizhan_vol_inject", &inject, sizeof inject) || !inject) {
    return;
  }
  const int mode = zizhan_vol_inject_mode(inject);
  if (mode == ZizhanVolInjectSkipMode) {
    IOLog("ZIZHAN_VOL_INJECT: SKIP unsupported-mode inject=%u\n", (unsigned)inject);
    return;
  }
  uint32_t req_delay = 0;
  (void)PE_parse_boot_argn("zizhan_vol_delay_s", &req_delay, sizeof req_delay);
  g_mode = inject;
  g_delay_s = zizhan_vol_delay_s(req_delay);
  thread_t thread = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
    IOLog("ZIZHAN_VOL_INJECT: FAIL worker-start\n");
    return;
  }
  IOLog("ZIZHAN_VOL_INJECT: RUNNING mode=%u delay_s=%u up=%u down=%u hold_ms=%u\n",
        (unsigned)g_mode, (unsigned)g_delay_s, zizhan_vol_up_clicks(g_mode),
        zizhan_vol_down_clicks(g_mode), (unsigned)ZizhanVolHoldMs);
  thread_deallocate(thread);
}

} // namespace zizhan_vol_inject
