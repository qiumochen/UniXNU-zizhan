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
static ZizhanDisplayPowerRuntime runtime;
static uint8_t runtime_ready;
static uint8_t mode_parsed;
static uint8_t unsupported_logged;
static uint32_t mode_raw;

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
} // namespace zizhan_display_power_service

/* FOLD-01: 0 off, 1 on, 2 transitioning; on until the first request (the panel boots lit). */
extern "C" int zizhan_display_power_state(void) {
  using namespace zizhan_display_power_service;
  if (mode() <= 0) return -1;
  return runtime_ready ? (int)runtime.state.state : 1;
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
  if (!runtime_ready) {
    zizhan_display_power_runtime_init(&runtime);
    runtime_ready = 1;
  }
  const ZizhanDisplayPowerOps ops = {backlight_get, backlight_set, dsi_sequence};
  ZizhanDisplayPowerReport report;
  const int result = zizhan_display_power_execute(&runtime, &ops, nullptr, count, in,
                                                  bytes, uptime_us(), &report);
  if (result == ZizhanDisplayPowerOk) {
    if (pong_touch_set_paused) pong_touch_set_paused(report.target == ZizhanDisplayTargetOff ? 1 : 0);
    IOLog("ZIZHAN_DISPLAY_POWER: PASS target=%s sequence=%u saved=%u state=%u\n",
          report.target == ZizhanDisplayTargetOff ? "off" : "on",
          (unsigned)report.sequence, (unsigned)runtime.saved_level,
          (unsigned)runtime.state.state);
    uint32_t te_check = 0;
    if (PE_parse_boot_argn("zizhan_display_te", &te_check, sizeof te_check) && te_check == 1) {
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
    if (pong_touch_set_paused && runtime.state.state == 1) pong_touch_set_paused(0);
    IOLog("ZIZHAN_DISPLAY_POWER: FAIL fault=%u target=%s sequence=%u saved=%u "
          "restored=%u dsi_failed=%u state=%u\n",
          report.fault,
          report.target == ZizhanDisplayTargetOff ? "off" : "on",
          (unsigned)report.sequence, (unsigned)runtime.saved_level,
          (unsigned)report.restored, (unsigned)report.dsi_failed,
          (unsigned)runtime.state.state);
  }
  return result == ZizhanDisplayPowerOk || result == ZizhanDisplayPowerNoOp ? 0 : -1;
}
