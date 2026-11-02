// SPDX-License-Identifier: MIT
// Unattended bring-up for zizhan (MIX Fold 2, SM8475), adapted from sheng's BootReset.c (UniXNU sheng 08cde20):
// with boot-arg pong_autoreset=<seconds> (default 300 since 10-31; 0 = off), write "bootloader" into the PMIC SDAM reboot-reason cell and arm the Gunyah
// hypervisor watchdog (60 s bite), pinged from a kernel thread until <seconds> after start. Any later reset -- the watchdog
// biting after a panic or hang, or the deadline passing -- lands in fastboot with DRAM (and the ramoops console) intact,
// so the host can fastboot-boot RamoopsDump without anyone pressing keys. To keep rounds short the deadline and panics
// shorten the bite to 1 s instead of waiting 60 s (PSCI SYSTEM_RESET also lands in fastboot but loses the ramoops, g23), and pong_autoreset_sb=<s> ends the round <s> seconds
// after SpringBoard starts. DarwinBoot24 marks the cell and arms the watchdog before the kernel runs, covering early panics. zizhan's live DT matches sheng's facts exactly:
// spmi@c400000 core/chnls/obsrvr/cnfg = 0xc400000/0xc500000/0xc440000/0xc42d000, qcom,ee = 0, SDAM 0x7100 (PPID 0x071)
// cell 0x48 bits 7:1 with mode-bootloader = 2, and a qcom,gh-watchdog node.
#pragma once
#include <kern/thread.h>
#include <machine/machine_routines.h>
#include <mach/mach_time.h>
#include "BootResetCore.h"

// Kernel-internal, not in the exported Kernel.framework headers (sheng declares them the same way).
extern "C" vm_offset_t ml_io_map_unmappable(vm_offset_t phys_addr, vm_size_t size, uint32_t flags);
extern "C" void ml_io_unmap(vm_offset_t addr, vm_size_t sz);
extern "C" void proc_iterate(unsigned int flags, int (*callout)(struct proc *, void *), void *arg,
                             int (*filterfn)(struct proc *, void *), void *filterarg);
extern "C" int proc_pid(struct proc *p);
extern "C" void proc_name(int pid, char *buf, int size);
// osfmk/kern/debug.c (patches/xnu-ipados18/0004): called after the panic log is complete, instead of spinning forever.
extern "C" void (*pong_panic_reset_hook)(void);
// osfmk/kern/thread.c (patches/xnu-ipados18/0005): plan A2's thread-state sampler.
extern "C" void pong_thread_sampler_start(const char *proc, unsigned seconds) __attribute__((weak));
#if XNU_PONG_ADRENO_PLATFORM
// PongAdrenoScanout.cpp: whole-boot frame-rate summary, printed before the deadline reset.
extern "C" void pong_scanout_summary(void) __attribute__((weak));
// PongAdreno/submit/NativeAdrenoSubmit.cpp: the GPU submission/completion timeline (pong_gtrace=<s>), same start as the sampler.
extern "C" void pong_gtrace_start(unsigned seconds) __attribute__((weak));
#else
// Without the GPU platform neither exists, and the static kernel link takes no undefined weak symbol.
static inline void pong_scanout_summary(void) {}
static void (*const pong_gtrace_start)(unsigned seconds) = nullptr;
#endif
// NativeUfs/PongUfsTransport.cpp: copy the ramoops zones to the pinned klog file (pong_klog_4k), 1 = deadline, 2 = panic,
// 3 = DPU-flip mark, 4 = power-key long press.
extern "C" int pong_klog_flush(int reason) __attribute__((weak));
// bsd/dev/pong_buttons.c (patches/xnu-ipados18/0010): called from the key-polling thread once the power key has been held for
// pong_button_reset ms (default 3000). Absent in kernels without that patch (weak), present but unused without pong_buttons=1.
extern "C" void (*pong_button_reset_hook)(void) __attribute__((weak));

namespace pong_boot_reset {
enum : uint64_t {SpmiCore = 0xc400000, SpmiCoreSize = 0x3000, SpmiObsrvr = 0xc440000, SpmiChnls = 0xc500000, SpmiCnfg = 0xc42d000,
                 SpmiCnfgSize = 0x4000};
enum : uint32_t {GunyahWdtControl = 0x86000005u, GunyahWdtStatus = 0x86000006u, GunyahWdtPing = 0x86000007u,
                 GunyahWdtSetTime = 0x86000008u, GunyahWdtEnable = 3, GunyahWdtDisable = 2, GunyahWdtTimeoutMs = 60000,
                 GunyahWdtSoonMs = 1000, PsciFeatures = 0x8400000Au, PsciSystemReset2 = 0xC4000012u};

static uint64_t smc2(uint64_t fid, uint64_t a1, uint64_t a2, uint64_t *r2) {
  register uint64_t x0 __asm__("x0") = fid, x1 __asm__("x1") = a1, x2 __asm__("x2") = a2, x3 __asm__("x3") = 0;
  __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3) :: "memory", "x4", "x5", "x6", "x7", "x8", "x9", "x10", "x11",
                   "x12", "x13", "x14", "x15", "x16", "x17");
  if (r2) *r2 = x2;
  return x0;
}

// ml_io_map hands out permanent mappings that ml_io_unmap cannot free (sheng int-105); use unmappable ones (7 = VM_WIMG_IO).
static volatile uint32_t *map(uint64_t phys, uint64_t size) {
  return (volatile uint32_t *)ml_io_map_unmappable((vm_offset_t)phys, (vm_size_t)size, 7);
}

// Writes "bootloader" into the reboot-reason cell and reads it back. 0 = verified.
static int markBootloader(char *text, unsigned cap) {
  br_arbiter a = {};
  br_found f = {};
  uint8_t before = 0xff, after = 0xff;
  int rc = 0;
  volatile uint32_t *obs = nullptr, *chn = nullptr;
  uint64_t obsSize = 0, chnSize = 0;
  a.ee = 0;
  a.core = map(SpmiCore, SpmiCoreSize);
  a.cnfg = map(SpmiCnfg, SpmiCnfgSize);
  if (!a.core || !a.cnfg) rc = 1;
  if (!rc && br_find(&a, &f)) rc = 2;
  if (!rc && f.owner != a.ee) rc = 5;
  if (!rc) {
    obsSize = ((uint64_t)BR_OBS_APID_STRIDE * ((uint64_t)f.apid + 1) + 0xfff) & ~0xfffull;
    chnSize = (uint64_t)BR_CHNL_APID_STRIDE * ((uint64_t)f.apid + 1);
    obs = map(SpmiObsrvr, obsSize);
    chn = map(SpmiChnls, chnSize);
    if (!obs || !chn) rc = 3;
  }
  if (!rc) rc = br_set_bootloader(obs, chn, &a, f.apid, &before, &after);
  snprintf(text, cap, "spmi v%08x apids=%u apid=%d owner=%d cell %02x -> %02x rc=%d", a.core ? f.version : 0, a.core ? f.apids : 0,
           f.apid, f.owner, before, after, rc);
  if (a.core) ml_io_unmap((vm_offset_t)a.core, SpmiCoreSize);
  if (a.cnfg) ml_io_unmap((vm_offset_t)a.cnfg, SpmiCnfgSize);
  if (obs) ml_io_unmap((vm_offset_t)obs, (vm_size_t)obsSize);
  if (chn) ml_io_unmap((vm_offset_t)chn, (vm_size_t)chnSize);
  return rc;
}

static uint32_t gDeadline, gAfterSpringBoard, gPanicHold = 2, pong_reset_warm = 1, gSampleSeconds, gTraceSeconds, gTraceDelay = 20;
// g152/g152b (2026-10-31): the pinger reached its deadline and then hung inside pong_scanout_summary (a display-register access that
// never answered, or the klog flush behind a lock a stuck thread held); the Gunyah watchdog did not bite in 7 minutes without pings.
// So a second thread, the guard, watches the pinger: once the pinger has passed its deadline (gPingerEnd) or the absolute deadline
// has passed, and no reset has happened within gGuardGrace seconds, it resets by itself, touching nothing but the SMC calls (no
// IOLog, no klog, no display registers). The DRAM ramoops survives the warm reset for RamoopsDump. pong_autoreset_guard=<s> (0 off).
static volatile uint64_t gPingerEnd;   // absolute time at which the pinger leaves its loop (shortened when SpringBoard is seen)
static volatile bool gResetting;
static uint32_t gGuardGrace = 20;
static char gSampleProc[32] = "backboardd";
static volatile uint32_t gPings;
static uint64_t counterFrequency() {
  uint64_t f = 0;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
  return f ? f : 19200000;
}
// PSCI SYSTEM_RESET2 with SYSTEM_WARM_RESET (type 0) when the firmware has it: the reset Android's warm reboot uses, which
// keeps DRAM in self-refresh. PSCI SYSTEM_RESET (cold) left the ramoops console unreadable (g23), and the watchdog bite keeps
// it but flips bits, more the longer the round (g58-g65 lost every late DIAG record to a flipped signature). Without
// SYSTEM_RESET2 it is the bite, 1 s from now; nothing pings after this, so it always comes.
static int64_t gWarmReset = -99;
static void resetNow() {
  if (gWarmReset == -99) gWarmReset = (int64_t)smc2(PsciFeatures, PsciSystemReset2, 0, nullptr);
  if (gWarmReset == 0 && pong_reset_warm) smc2(PsciSystemReset2, 0, 0, nullptr);   // returns only if refused
  smc2(GunyahWdtControl, GunyahWdtDisable, 0, nullptr);
  smc2(GunyahWdtSetTime, GunyahWdtSoonMs, GunyahWdtSoonMs, nullptr);
  smc2(GunyahWdtControl, GunyahWdtEnable, 0, nullptr);
}
static void panicReset() {
  if (pong_klog_flush) pong_klog_flush(2);
  const uint64_t f = counterFrequency(), t = mach_absolute_time();
  while (mach_absolute_time() - t < gPanicHold * f) {}
  resetNow();
}
static int findSpringBoard(struct proc *p, void *found) {
  char name[32] = {};
  proc_name(proc_pid(p), name, sizeof(name));
  if (!memcmp(name, "SpringBoard", sizeof("SpringBoard"))) *(bool *)found = true;
  return 0;   // PROC_RETURNED
}
static bool springBoardRunning() {
  bool found = false;
  proc_iterate(1 /* PROC_ALLPROCLIST */, findSpringBoard, &found, nullptr, nullptr);
  return found;
}
static void pinger(void *, wait_result_t) {
  const uint64_t f = counterFrequency(), start = mach_absolute_time();
  uint64_t end = (uint64_t)gDeadline * f;
  bool seen = false, sampled = false, traced = false;
  uint64_t seenAt = 0;
  for (uint64_t now = 0; now < end; now = mach_absolute_time() - start) {
    smc2(GunyahWdtPing, 0, 0, nullptr);
    gPings = gPings + 1;
    if (gAfterSpringBoard && !seen && springBoardRunning()) {
      seen = true;
      seenAt = now;
      if (now + (uint64_t)gAfterSpringBoard * f < end) end = now + (uint64_t)gAfterSpringBoard * f;
      gPingerEnd = start + end;
      IOLog("PONG_AUTORESET: SpringBoard up at %llus; resetting %us later\n", now / f, gAfterSpringBoard);
    }
    // pong_tsample=<s>: sample pong_tsample_proc's threads for <s> seconds from 20 s after SpringBoard (with the synthetic swipes).
    if (seen && !sampled && gSampleSeconds && now - seenAt >= 20 * f && pong_thread_sampler_start) {
      sampled = true;
      pong_thread_sampler_start(gSampleProc, gSampleSeconds);
    }
    if (seen && !traced && gTraceSeconds && now - seenAt >= (uint64_t)gTraceDelay * f && pong_gtrace_start) {
      traced = true;
      pong_gtrace_start(gTraceSeconds);
    }
    IOSleep(1000);
  }
  gPingerEnd = mach_absolute_time();
  pong_scanout_summary();
  IOLog("PONG_AUTORESET: deadline reached after %u pings; watchdog bites in 1 s\n", gPings);
  gResetting = true;
  IOSleep(200);
  if (pong_klog_flush) pong_klog_flush(1);
  resetNow();
}

static uint32_t gKlogPeriod, gGuardTicks;   // pong_klog_period=<s>: copy the console to the UFS klog every <s> seconds (reason 5)
static void guard(void *, wait_result_t) {
  const uint64_t f = counterFrequency(), start = mach_absolute_time();
  const uint64_t grace = (uint64_t)gGuardGrace * f, cap = start + (uint64_t)gDeadline * f + grace;
  for (;;) {
    IOSleep(1000);
    // Rounds that die without reaching the warm reset (pinger stuck, cold watchdog bite, DRAM console garbled: g152l/o/p)
    // still leave their console this way. Diagnostics only: each copy is ~1 MB of UFS writes.
    if (gKlogPeriod && pong_klog_flush && ++gGuardTicks % gKlogPeriod == 0 && !gResetting) pong_klog_flush(5);
    if (gResetting) return;   // the pinger is resetting: its watchdog bite (1 s) is on the way
    const uint64_t now = mach_absolute_time(), end = gPingerEnd;
    if ((end && now > end + grace) || now > cap) break;
  }
  gResetting = true;
  resetNow();   // PSCI SYSTEM_RESET2 (warm, keeps DRAM) and the 1 s watchdog bite behind it
}
static int armWatchdog(uint32_t deadline, char *text, unsigned cap) {
  uint64_t since = 0;
  const int64_t status = (int64_t)smc2(GunyahWdtStatus, 0, 0, &since);
  if (status != 0) {snprintf(text, cap, "watchdog status %lld, not armed", (long long)status); return 1;}
  smc2(GunyahWdtControl, GunyahWdtDisable, 0, nullptr);
  const int64_t set = (int64_t)smc2(GunyahWdtSetTime, GunyahWdtTimeoutMs, GunyahWdtTimeoutMs, nullptr);
  if (set) {snprintf(text, cap, "watchdog set-time %lld, not armed", (long long)set); return 2;}
  gDeadline = deadline;
  thread_t thread;
  if (kernel_thread_start(pinger, nullptr, &thread) != KERN_SUCCESS) {snprintf(text, cap, "watchdog: no thread"); return 3;}
  thread_deallocate(thread);
  gPingerEnd = mach_absolute_time() + (uint64_t)deadline * counterFrequency();
  if (gGuardGrace && kernel_thread_start(guard, nullptr, &thread) == KERN_SUCCESS) thread_deallocate(thread);
  const int64_t on = (int64_t)smc2(GunyahWdtControl, GunyahWdtEnable, 0, nullptr);
  snprintf(text, cap, "watchdog enable=%lld, pings until %us, bite 60 s later (last ping %llu ms ago)", (long long)on, deadline,
           (unsigned long long)since);
  return on ? 4 : 0;
}

// Power key held for pong_button_reset ms (xnu side, pong_buttons.c): end the round now, exactly like the deadline -- warm reset
// into fastboot with the DRAM and klog logs intact. The only way back from a running iOS besides the deadline: XNU has no USB
// device stack and no network here, so the host cannot reach it (10-31: four rounds without pong_autoreset sat in iOS until
// the user long-pressed the PMIC 10 s combination, which looked like four hard hangs).
static bool gMarked;
static void buttonReset() {
  char text[200];
  if (gResetting) return;
  gResetting = true;
  if (!gMarked) {   // pong_autoreset=0: the cell was never written; without it the reset boots Android
    gMarked = markBootloader(text, sizeof(text)) == 0;
    IOLog("PONG_AUTORESET: mark bootloader (power key): %s\n", text);
  }
  IOLog("PONG_AUTORESET: power key held: resetting into fastboot now\n");
  pong_scanout_summary();
  IOSleep(200);
  if (pong_klog_flush) pong_klog_flush(4);
  resetNow();
}

// Called once from the platform expert. The deadline defaults to 300 s (DefaultDeadline); only an explicit pong_autoreset=0
// leaves the watchdog unarmed (10-31: rounds whose command line lacked the arg never came back). The power-key hook is
// installed either way.
enum : uint32_t {DefaultDeadline = 300};
static void start(void) {
  uint32_t deadline = DefaultDeadline;
  char text[200];
  if (&pong_button_reset_hook) pong_button_reset_hook = buttonReset;
  const bool given = PE_parse_boot_argn("pong_autoreset", &deadline, sizeof(deadline));
  if (!deadline) {
    IOLog("PONG_AUTORESET: off (pong_autoreset=0); power key held %s still resets\n", &pong_button_reset_hook ? "3 s" : "(no hook in this kernel)");
    return;
  }
  if (!given) IOLog("PONG_AUTORESET: no pong_autoreset boot-arg: using the default %us\n", deadline);
  int rc = markBootloader(text, sizeof(text));
  IOLog("PONG_AUTORESET: mark bootloader: %s\n", text);
  if (rc) return;   // without the cell a watchdog reset would boot Android and overwrite the ramoops console
  gMarked = true;
  PE_parse_boot_argn("pong_autoreset_sb", &gAfterSpringBoard, sizeof(gAfterSpringBoard));
  PE_parse_boot_argn("pong_panic_hold", &gPanicHold, sizeof(gPanicHold));
  PE_parse_boot_argn("pong_reset_warm", &pong_reset_warm, sizeof(pong_reset_warm));
  PE_parse_boot_argn("pong_autoreset_guard", &gGuardGrace, sizeof(gGuardGrace));
  PE_parse_boot_argn("pong_klog_period", &gKlogPeriod, sizeof(gKlogPeriod));
  PE_parse_boot_argn("pong_tsample", &gSampleSeconds, sizeof(gSampleSeconds));
  PE_parse_boot_argn("pong_tsample_proc", gSampleProc, sizeof(gSampleProc));
  PE_parse_boot_argn("pong_gtrace", &gTraceSeconds, sizeof(gTraceSeconds));
  PE_parse_boot_argn("pong_gtrace_delay", &gTraceDelay, sizeof(gTraceDelay));
  gWarmReset = (int64_t)smc2(PsciFeatures, PsciSystemReset2, 0, nullptr);
  if (armWatchdog(deadline, text, sizeof(text)) == 0) pong_panic_reset_hook = panicReset;
  IOLog("PONG_AUTORESET: %s; after SpringBoard %us, panic reset after %us; PSCI SYSTEM_RESET2 %s (%lld), warm reset %s\n", text,
        gAfterSpringBoard, gPanicHold, gWarmReset == 0 ? "supported" : "absent", (long long)gWarmReset, pong_reset_warm ? "on" : "off");
}
}  // namespace pong_boot_reset
