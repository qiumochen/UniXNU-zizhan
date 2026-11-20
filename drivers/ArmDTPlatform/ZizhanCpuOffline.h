/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* POWER-04 (daily default, zizhan_cpu_offline=0 or processor_exit=0 turns it off; XNU patch 0028): while the display is off, take the A710 and
 * X2 cores (CPU 2..7 here: the mid/big clusters plus two of the four A510) offline through the scheduler's own user-request
 * path (processor_exit_from_user -> PSCI CPU_OFF in ArmCpu::quiesceCPU), and start them again when it comes back on
 * (processor_start_from_user -> PSCI CPU_ON). The boot core and CPU 1 always stay. Every transition is logged with its
 * result; nothing else changes. */
#pragma once
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "ZizhanDailyProfile.h"

extern "C" {
int zizhan_display_power_state(void);
processor_t cpu_to_processor(int cpu);
kern_return_t processor_exit_from_user(processor_t processor);
kern_return_t processor_start_from_user(processor_t processor);
}

namespace zizhan_cpu_offline {
constexpr unsigned kFirst = 2, kLast = 7;
static uint32_t gOffline;   // bit per CPU taken offline by us

static void apply(bool off) {
  uint64_t t0 = 0, t1 = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &t0);
  char res[64]; unsigned k = 0;
  for (unsigned cpu = kFirst; cpu <= kLast; ++cpu) {
    processor_t p = cpu_to_processor(int(cpu));
    if (!p) continue;
    kern_return_t r;
    if (off) {
      if (gOffline & (1u << cpu)) continue;
      r = processor_exit_from_user(p);
      if (r == KERN_SUCCESS) gOffline |= 1u << cpu;
    } else {
      if (!(gOffline & (1u << cpu))) continue;
      r = processor_start_from_user(p);
      if (r == KERN_SUCCESS) gOffline &= ~(1u << cpu);
    }
    k += snprintf(res + k, sizeof res - k, " %u:%d", cpu, r);
  }
  absolutetime_to_nanoseconds(mach_absolute_time(), &t1);
  IOLog("ZIZHAN_CPU_OFFLINE: display %s -> %s cpus%s offline_mask=%x in %llu ms\n", off ? "off" : "on", off ? "exit" : "start", res,
        gOffline, (t1 - t0) / 1000000);
}

static void worker(void *, wait_result_t) {
  int last = 1;
  unsigned offFor = 0;
  for (;;) {
    IOSleep(500);
    const int state = zizhan_display_power_state();
    if (state == 0) {
      if (++offFor == 4 && !gOffline) apply(true);   // off for 2 s
    } else {
      offFor = 0;
      if (gOffline) apply(false);
    }
    last = state;
  }
  (void)last;
}

static void start() {
  uint32_t on = 0, exitOk = 1;
  if (!zizhan_daily::arg("zizhan_cpu_offline", &on, zizhan_daily::kCpuOffline) || !on) return;
  if (PE_parse_boot_argn("processor_exit", &exitOk, sizeof exitOk) && !exitOk) {
    IOLog("ZIZHAN_CPU_OFFLINE: SKIP processor_exit=0\n");
    return;
  }
  thread_t th = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
  IOLog("ZIZHAN_CPU_OFFLINE: RUNNING cpus %u..%u follow the display\n", kFirst, kLast);
}
}  // namespace zizhan_cpu_offline
