// SPDX-License-Identifier: MIT
// zizhan-only, default-off, one-shot TSENS-v2 observation. No enable, reset,
// IRQ, threshold, clock, EPSS or firmware writes; no thermal service published.
#pragma once
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include "ZizhanTsensCore.h"

extern "C" struct vnode *rootvnode;

namespace zizhan_tsens {
static Inputs inputs;

static const char *reason(State s) {
  switch (s) {
    case State::UnsupportedVersion: return "unsupported-version";
    case State::Disabled: return "disabled";
    case State::InReset: return "in-reset";
    case State::Invalid: return "invalid-sensor";
    default: return "none";
  }
}
static void worker(void *, wait_result_t) {
  // Let normal boot progress; this is a finite observer, not a monitoring
  // daemon or a return mechanism. The device cycle owns its independent limit.
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i)
    IOSleep(100);
  if (!__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE)) {
    IOLog("ZIZHAN_TSENS: SKIP root-timeout finished=1\n");
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
    return;
  }
  IOSleep(5000);
  unsigned passed = 0, failed = 0, skipped = 0;
  for (unsigned i = 0; i < 2; ++i) {
    const Bank &b = inputs.bank[i];
    IOMemoryDescriptor *srot = IOMemoryDescriptor::withPhysicalAddress(
        b.srot, b.srot_bytes, kIODirectionIn);
    IOMemoryDescriptor *tm = IOMemoryDescriptor::withPhysicalAddress(
        b.tm, b.tm_bytes, kIODirectionIn);
    IOMemoryMap *srot_map = srot ? srot->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    IOMemoryMap *tm_map = tm ? tm->map(kIOMapAnywhere | kIOMapInhibitCache) : nullptr;
    if (!srot_map || !tm_map || !srot_map->getVirtualAddress() || !tm_map->getVirtualAddress()) {
      IOLog("ZIZHAN_TSENS: bank=%u FAIL map\n", i);
      failed++;
    } else {
      const volatile uint8_t *s = reinterpret_cast<const volatile uint8_t *>(srot_map->getVirtualAddress());
      const volatile uint8_t *t = reinterpret_cast<const volatile uint8_t *>(tm_map->getVirtualAddress());
      Observation out = observe([s, t](bool is_srot, uint32_t offset) -> uint32_t {
        const volatile uint32_t *reg =
            reinterpret_cast<const volatile uint32_t *>((is_srot ? s : t) + offset);
        uint32_t value = *reg;
        __asm__ volatile("dmb oshld" ::: "memory");
        return value;
      });
      IOLog("ZIZHAN_TSENS: bank=%u version=0x%x control=0x%x raw-trdy=0x%x reason=%s\n",
            i, out.version, out.control, out.raw_trdy, reason(out.state));
      if (out.state == State::Pass || out.state == State::Invalid) {
        for (unsigned n = 0; n < 16; ++n) {
          const Sample &v = out.sensors[n];
          if (v.state == State::Pass) {
            IOLog("ZIZHAN_TSENS: bank=%u sensor=%u PASS raw=0x%x milliC=%d\n",
                  i, n, v.raw, v.milli_celsius);
            passed++;
          } else {
            IOLog("ZIZHAN_TSENS: bank=%u sensor=%u FAIL raw=0x%x invalid\n", i, n, v.raw);
            failed++;
          }
        }
      } else {
        IOLog("ZIZHAN_TSENS: bank=%u SKIP prerequisite=%s\n", i, reason(out.state));
        skipped++;
      }
    }
    if (tm_map) tm_map->release();
    if (srot_map) srot_map->release();
    if (tm) tm->release();
    if (srot) srot->release();
  }
  IOLog("ZIZHAN_TSENS: %s samples=%u failed=%u skipped-banks=%u readonly=1 finished=1\n",
        failed ? "FAIL" : skipped ? "SKIP" : "PASS", passed, failed, skipped);
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start(IOService *provider) {
  uint32_t enabled = 0;
  if (!PE_parse_boot_argn("zizhan_tsens", &enabled, sizeof(enabled)) || !enabled) return;
  if (enabled != 1) {
    IOLog("ZIZHAN_TSENS: SKIP unsupported-mode\n");
    return;
  }
  IORegistryEntry *chosen = provider->childFromPath("chosen", gIODTPlane);
  OSData *data = chosen ? OSDynamicCast(OSData, chosen->getProperty("zizhan-tsens-v2")) : nullptr;
  bool valid = data && data->getLength() == sizeof(inputs);
  if (valid) memcpy(&inputs, data->getBytesNoCopy(), sizeof(inputs));
  if (chosen) chosen->release();
  if (!valid || !inputs_ok(inputs)) {
    IOLog("ZIZHAN_TSENS: SKIP invalid-or-missing-board-inputs\n");
    return;
  }
  thread_t thread;
  if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
    IOLog("ZIZHAN_TSENS: FAIL worker-start\n");
    return;
  }
  IOLog("ZIZHAN_TSENS: RUNNING banks=2 one-shot=1 readonly=1\n");
  thread_deallocate(thread);
}
} // namespace zizhan_tsens
