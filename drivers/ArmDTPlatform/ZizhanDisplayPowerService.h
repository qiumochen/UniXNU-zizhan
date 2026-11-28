/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* POWER-02 S3 kernel wrapper: default-off selector-12 executor. */
#pragma once
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <pexpert/pexpert.h>
#include "ZizhanDailyProfile.h"
#include <stdint.h>
#include "ZizhanDisplayPowerExecutor.h"
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>

/* Panel-side evidence for S3: the inner panel's TE line (GPIO86, mdp_vsync) pulses every frame while the panel
 * scans out and stops in sleep. Read-only sampling of the pad level for ~100 ms after each transition. */
static unsigned zizhan_te_edges(unsigned pin, unsigned *samples_out) {
  auto memory = IODeviceMemory::withRange(0xf100000ull + uint64_t(pin) * 0x1000, 0x1000);
  auto map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
  if (memory) memory->release();
  if (!map) { if (samples_out) *samples_out = 0; return ~0u; }
  auto io = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress() + 4);
  unsigned edges = 0, samples = 0, last = *io & 1u;
  for (; samples < 5000; ++samples) {
    const unsigned v = *io & 1u;
    if (v != last) ++edges;
    last = v;
    IODelay(20);
  }
  map->release();
  if (samples_out) *samples_out = samples;
  return edges;
}

/* Implemented in PongAdrenoScanout.cpp and PongBacklight.cpp. Weak so a
 * reduced kernel without the display/backlight components still links. */
extern "C" int zizhan_dsi_power_sequence(const uint8_t *dcs,const uint16_t *wait_ms,
                                         uint32_t count,int low_power)
    __attribute__((weak));
extern "C" int pong_backlight_level(uint32_t *level) __attribute__((weak));
extern "C" int pong_backlight_set(uint32_t level,uint32_t timeout_ms) __attribute__((weak));
extern "C" void pong_touch_set_paused(int paused) __attribute__((weak));

namespace zizhan_display_power_service {
static IOLock *service_lock;
static ZizhanDisplayPowerRuntime runtime;
static uint8_t runtime_ready;
static uint8_t mode_parsed;
static uint8_t unsupported_logged;
static uint32_t mode_raw;
static uint8_t cover_muted;
static uint32_t cover_saved_bl;

static void init(void) {
  if (!service_lock) {
    service_lock = IOLockAlloc();
  }
}

static uint64_t uptime_us(void) {
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
  return ns / 1000u;
}

static int mode(void) {
  if (!mode_parsed) {
    mode_raw = 0;
    (void)zizhan_daily::arg("zizhan_display_power", &mode_raw, zizhan_daily::kDisplayPower);
    mode_parsed = 1;
  }
  return zizhan_display_power_mode(mode_raw);
}

static int backlight_get(uint32_t *level, void *) {
  if (!pong_backlight_level) return -1;
  return pong_backlight_level(level);
}

static int backlight_set(uint32_t level, uint32_t timeout_ms, void *) {
  if (!pong_backlight_set) return -1;
  return pong_backlight_set(level, timeout_ms);
}

static int dsi_sequence(const uint8_t *dcs, const uint16_t *wait_ms, uint32_t count,
                        int low_power, void *) {
  if (!zizhan_dsi_power_sequence) return -1;
  return zizhan_dsi_power_sequence(dcs, wait_ms, count, low_power);
}

/* Virtual ops used when the inner panel is physically muted for outer-desktop mode (cover_muted == 1)
 * and IODisplayWrangler sleeps/wakes system display state while still folded. */
static int cover_bl_get(uint32_t *level, void *) {
  *level = cover_saved_bl;
  return 0;
}
static int cover_bl_set(uint32_t level, uint32_t, void *) {
  if (level) {
    cover_saved_bl = level;
  }
  return 0;
}
static int cover_dsi_noop(const uint8_t *, const uint16_t *, uint32_t, int, void *) {
  return 0;
}
} // namespace zizhan_display_power_service

/* FOLD-01: 0 off, 1 on, 2 transitioning; on until the first request (the panel boots lit). */
extern "C" int zizhan_display_power_state(void) {
  using namespace zizhan_display_power_service;
  if (mode() <= 0) return -1;
  return runtime_ready ? (int)runtime.state.state : 1;
}

/* M2 outer-desktop mode (zizhan_outer_fold=2): mutes ONLY the inner 8.02" OLED + backlight + inner touch
 * while keeping zizhan_display_power_state() == 1 so iPadOS, Adreno GPU, CPUs, and Wi-Fi remain awake for
 * live interactive cover scanout; restores inner OLED + backlight + touch on unfold. */
extern "C" int zizhan_inner_panel_cover_mute(int muted) {
  using namespace zizhan_display_power_service;
  if (mode() <= 0) {
    return 0;
  }
  if (!pong_backlight_level || !pong_backlight_set || !zizhan_dsi_power_sequence) {
    return -1;
  }
  if (service_lock) {
    IOLockLock(service_lock);
  }
  if (!runtime_ready) {
    zizhan_display_power_runtime_init(&runtime);
    runtime_ready = 1;
  }
  int rc = 0;
  if (muted) {
    if (!cover_muted) {
      if (runtime.state.state == ZizhanDisplayStateOn) {
        uint32_t lvl = 0;
        if (pong_backlight_level(&lvl) == 0 && lvl > 0) {
          cover_saved_bl = lvl;
        } else if (!cover_saved_bl) {
          cover_saved_bl = 307u;
        }
        (void)pong_backlight_set(0, 1000);
        const int dsi = zizhan_dsi_power_sequence(ZizhanDisplayOffCommands, ZizhanDisplayOffWaits, 2, 1);
        if (dsi == 0) {
          cover_muted = 1;
          if (pong_touch_set_paused) {
            pong_touch_set_paused(1);
          }
          unsigned te_samples = 0;
          const unsigned te = zizhan_te_edges(86, &te_samples);
          IOLog("ZIZHAN_DISPLAY_POWER: cover-mute PASS target=off saved=%u gpio86_edges=%u samples=%u state=%u\n",
                (unsigned)cover_saved_bl, te, te_samples, (unsigned)runtime.state.state);
        } else {
          (void)zizhan_dsi_power_sequence(ZizhanDisplayOnCommands, ZizhanDisplayOnWaits, 2, 1);
          (void)pong_backlight_set(cover_saved_bl, 1000);
          rc = -1;
          IOLog("ZIZHAN_DISPLAY_POWER: cover-mute FAIL target=off dsi=%d restored_bl=%u\n",
                dsi, (unsigned)cover_saved_bl);
        }
      } else {
        cover_saved_bl = runtime.saved_level;
        cover_muted = 1;
        if (pong_touch_set_paused) {
          pong_touch_set_paused(1);
        }
        IOLog("ZIZHAN_DISPLAY_POWER: cover-mute PASS target=off already-asleep saved=%u\n",
              (unsigned)cover_saved_bl);
      }
    }
  } else if (cover_muted) {
    cover_muted = 0;
    if (runtime.state.state == ZizhanDisplayStateOn) {
      const int dsi = zizhan_dsi_power_sequence(ZizhanDisplayOnCommands, ZizhanDisplayOnWaits, 2, 1);
      const int bl = (dsi == 0) ? pong_backlight_set(cover_saved_bl, 1000) : -1;
      if (pong_touch_set_paused) {
        pong_touch_set_paused(0);
      }
      IOSleep(60);
      unsigned te_samples = 0;
      const unsigned te = zizhan_te_edges(86, &te_samples);
      rc = (dsi == 0 && bl == 0) ? 0 : -1;
      IOLog("ZIZHAN_DISPLAY_POWER: cover-mute %s target=on restored=%u dsi=%d bl=%d gpio86_edges=%u state=%u\n",
            rc == 0 ? "PASS" : "FAIL", (unsigned)cover_saved_bl, dsi, bl, te, (unsigned)runtime.state.state);
    } else {
      runtime.saved_level = cover_saved_bl;
      runtime.have_saved = 1;
      IOLog("ZIZHAN_DISPLAY_POWER: cover-mute PASS target=on deferred-to-wake saved=%u\n",
            (unsigned)cover_saved_bl);
    }
  }
  if (service_lock) {
    IOLockUnlock(service_lock);
  }
  return rc;
}

extern "C" int zizhan_display_power_request(uint32_t count,const uint64_t *in,
                                            size_t bytes) {
  using namespace zizhan_display_power_service;
  const int current = mode();
  if (current == 0) return 0;
  if (current < 0) {
    if (!unsupported_logged) {
      IOLog("ZIZHAN_DISPLAY_POWER: SKIP unsupported-mode zizhan_display_power=%u\n",
            (unsigned)mode_raw);
      unsupported_logged = 1;
    }
    return 0;
  }
  if (!pong_backlight_level || !pong_backlight_set || !zizhan_dsi_power_sequence) {
    IOLog("ZIZHAN_DISPLAY_POWER: SKIP missing-executor backlight=%u/%u dsi=%u\n",
          pong_backlight_level ? 1u : 0u, pong_backlight_set ? 1u : 0u,
          zizhan_dsi_power_sequence ? 1u : 0u);
    return -1;
  }
  if (service_lock) {
    IOLockLock(service_lock);
  }
  if (!runtime_ready) {
    zizhan_display_power_runtime_init(&runtime);
    runtime_ready = 1;
  }
  const ZizhanDisplayPowerOps phys_ops = {backlight_get, backlight_set, dsi_sequence};
  const ZizhanDisplayPowerOps muted_ops = {cover_bl_get, cover_bl_set, cover_dsi_noop};
  const ZizhanDisplayPowerOps *use_ops = cover_muted ? &muted_ops : &phys_ops;
  ZizhanDisplayPowerReport report;
  const int result = zizhan_display_power_execute(&runtime, use_ops, nullptr, count, in,
                                                  bytes, uptime_us(), &report);
  if (result == ZizhanDisplayPowerOk) {
    if (pong_touch_set_paused) {
      pong_touch_set_paused((cover_muted || report.target == ZizhanDisplayTargetOff) ? 1 : 0);
    }
    IOLog("ZIZHAN_DISPLAY_POWER: PASS target=%s sequence=%u saved=%u state=%u cover_muted=%u\n",
          report.target == ZizhanDisplayTargetOff ? "off" : "on",
          (unsigned)report.sequence, (unsigned)runtime.saved_level,
          (unsigned)runtime.state.state, (unsigned)cover_muted);
    uint32_t te_check = 0;
    if (!cover_muted && PE_parse_boot_argn("zizhan_display_te", &te_check, sizeof te_check) && te_check == 1) {
      if (report.target == ZizhanDisplayTargetOn) IOSleep(150);   // let the panel resume scanning out
      unsigned te_samples = 0;
      const unsigned te = zizhan_te_edges(86, &te_samples);
      IOLog("ZIZHAN_DISPLAY_POWER: te target=%s gpio86_edges=%u samples=%u\n",
            report.target == ZizhanDisplayTargetOff ? "off" : "on", te, te_samples);
    }
  } else if (result == ZizhanDisplayPowerNoOp) {
    IOLog("ZIZHAN_DISPLAY_POWER: NOOP target=%s state=%u\n",
          report.target == ZizhanDisplayTargetOff ? "off" : "on",
          (unsigned)runtime.state.state);
  } else if (result == ZizhanDisplayPowerRejected) {
    IOLog("ZIZHAN_DISPLAY_POWER: REJECT reason=%u request=%u accepted=%u noops=%u "
          "unknown=%u busy=%u queue-full=%u\n",
          (unsigned)runtime.state.last_failure,
          (unsigned)runtime.state.requests, (unsigned)runtime.state.accepted,
          (unsigned)runtime.state.noops, (unsigned)runtime.state.rejected_unknown,
          (unsigned)runtime.state.rejected_busy,
          (unsigned)runtime.state.rejected_queue_full);
  } else {
    if (pong_touch_set_paused && runtime.state.state == 1 && !cover_muted) pong_touch_set_paused(0);
    IOLog("ZIZHAN_DISPLAY_POWER: FAIL fault=%u target=%s sequence=%u saved=%u "
          "restored=%u dsi_failed=%u state=%u\n",
          report.fault,
          report.target == ZizhanDisplayTargetOff ? "off" : "on",
          (unsigned)report.sequence, (unsigned)runtime.saved_level,
          (unsigned)report.restored, (unsigned)report.dsi_failed,
          (unsigned)runtime.state.state);
  }
  if (service_lock) {
    IOLockUnlock(service_lock);
  }
  return result == ZizhanDisplayPowerOk || result == ZizhanDisplayPowerNoOp ? 0 : -1;
}
