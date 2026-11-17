/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOLib.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <libkern/c++/OSData.h>
#include "ZizhanHallState.h"

extern "C" struct vnode *rootvnode;

namespace zizhan_hall {
struct Inputs {
  uint64_t tlmm_base;
  uint32_t tlmm_bytes;
  uint32_t gpio_offset;
  uint32_t gpio;
  uint32_t active_low;
  uint32_t debounce_ms;
};

static bool read32(IORegistryEntry *entry, const char *key, uint32_t &value) {
  auto data = OSDynamicCast(OSData, entry->getProperty(key));
  if (!data || data->getLength() != sizeof(value)) return false;
  memcpy(&value, data->getBytesNoCopy(), sizeof(value));
  return true;
}

static bool read64(IORegistryEntry *entry, const char *key, uint64_t &value) {
  auto data = OSDynamicCast(OSData, entry->getProperty(key));
  if (!data || data->getLength() != sizeof(value)) return false;
  memcpy(&value, data->getBytesNoCopy(), sizeof(value));
  return true;
}

static bool inputs(IOService *platform, Inputs &out) {
  auto node = platform->childFromPath("arm-io/pong-touch", gIODTPlane);
  if (!node) return false;
  auto identity = OSDynamicCast(OSData, node->getProperty("compatible"));
  const char expected[] = "st,fts-zizhan-pri";
  const bool valid = identity && identity->getLength() == sizeof(expected) &&
      memcmp(identity->getBytesNoCopy(), expected, sizeof(expected)) == 0 &&
      read64(node, "tlmm-base", out.tlmm_base) &&
      read32(node, "tlmm-size", out.tlmm_bytes) &&
      read32(node, "gpio-offset", out.gpio_offset) &&
      read32(node, "hall-gpio", out.gpio) &&
      read32(node, "hall-active-low", out.active_low) &&
      read32(node, "hall-debounce-ms", out.debounce_ms) &&
      out.tlmm_base == 0x0f000000ULL && out.tlmm_bytes == 0x200000 &&
      out.gpio_offset == 0x100000 && out.gpio == 62 &&
      out.active_low == 1 && out.debounce_ms == 15 &&
      out.gpio_offset + out.gpio * 0x1000ULL + sizeof(uint32_t) < out.tlmm_bytes;
  node->release();
  return valid;
}

static uint64_t nowNs() {
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
  return ns;
}

static const char *rawName(bool high) { return high ? "high" : "low"; }

static void worker(void *arg, wait_result_t) {
  auto *copy = static_cast<Inputs *>(arg);
  const Inputs config = *copy;
  IODelete(copy, Inputs, 1);
  IOLog("ZIZHAN_HALL: RUNNING gpio=%u active_low=%u debounce_ms=%u readonly=1 display_switch=0\n",
        config.gpio, config.active_low, config.debounce_ms);

  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i)
    IOSleep(100);
  if (!__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE)) {
    IOLog("ZIZHAN_HALL: SKIP root-timeout finished=1\n");
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
    return;
  }

  // Share read access to this pin's page only. Never change mux, direction,
  // pull, interrupt enable/status or the existing touch driver's pins.
  const uint64_t page = config.tlmm_base + config.gpio_offset + uint64_t(config.gpio) * 0x1000;
  auto memory = IODeviceMemory::withRange(page, 0x1000);
  auto map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
  if (memory) memory->release();
  if (!map || !map->getVirtualAddress()) {
    if (map) map->release();
    IOLog("ZIZHAN_HALL: FAIL tlmm-map finished=1\n");
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
    return;
  }

  auto control = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
  auto input = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress() + 4);
  const uint64_t begin = nowNs();
  const uint64_t debounce_ns = uint64_t(config.debounce_ms) * 1000000ULL;
  State state{};
  unsigned reads = 0, changes = 0, reports = 0;
  bool baseline = false;
  bool prerequisite_failed = false;

  while (nowNs() - begin < 120ULL * 1000000000ULL && reads < 24000) {
    const uint32_t cfg = *control;
    __asm__ volatile("dmb oshld" ::: "memory");
    // Function bits 5:2 must be GPIO and bit 9 must not drive an output.
    if ((cfg & 0x3cu) || (cfg & (1u << 9))) {
      IOLog("ZIZHAN_HALL: SKIP pin-not-gpio-input cfg=0x%x\n", cfg);
      prerequisite_failed = true;
      break;
    }
    const uint32_t raw = *input;
    __asm__ volatile("dmb oshld" ::: "memory");
    const bool high = (raw & 1u) != 0;
    const bool asserted = config.active_low ? !high : high;
    const uint64_t when = nowNs();
    const auto update = sample(state, asserted, when, debounce_ns);
    ++reads;
    if (update.event == Event::Baseline || update.event == Event::Transition) {
      baseline = true;
      if (update.event == Event::Transition) ++changes;
      if (reports++ < 64) IOLog("ZIZHAN_HALL: %s raw=0x%x level=%s asserted=%u at_ns=%llu\n",
            update.event == Event::Baseline ? "BASELINE" : "TRANSITION",
            raw, rawName(high), update.stable ? 1u : 0u,
            static_cast<unsigned long long>(when));
    }
    IOSleep(5);
  }

  IOLog("ZIZHAN_HALL: %s baseline=%u transitions=%u reads=%u readonly=1 fold_qualified=0 finished=1\n",
        prerequisite_failed ? "SKIP" : baseline ? "PASS observer" : "FAIL observer", baseline ? 1u : 0u,
        changes, reads);
  map->release();
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start(IOService *platform) {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_hall_observe", &mode, sizeof(mode)) || !mode) return;
  if (mode != 1) {
    IOLog("ZIZHAN_HALL: SKIP unsupported-mode finished=1\n");
    return;
  }

  Inputs config{};
  if (!inputs(platform, config)) {
    IOLog("ZIZHAN_HALL: SKIP invalid-or-missing-board-inputs finished=1\n");
    return;
  }
  thread_t thread = THREAD_NULL;
  auto *copy = IONew(Inputs, 1);
  if (!copy) {
    IOLog("ZIZHAN_HALL: FAIL input-allocation finished=1\n");
    return;
  }
  *copy = config;
  if (kernel_thread_start(worker, copy, &thread) != KERN_SUCCESS) {
    IODelete(copy, Inputs, 1);
    IOLog("ZIZHAN_HALL: FAIL worker-start finished=1\n");
    return;
  }
  thread_deallocate(thread);
}
} // namespace zizhan_hall
