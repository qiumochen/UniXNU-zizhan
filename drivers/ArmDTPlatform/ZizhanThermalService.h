// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan THERM-01: default-off periodic TSENS sampling and thermal caps.
// Mode 1 samples and logs only. Mode 2 also publishes the logical CPU/GPU cap
// levels consumed by PongCpuPolicy.h and PongAdrenoPlatform.cpp. No TSENS
// threshold, IRQ, reset or firmware write, and no thermal service is
// published; the 115 C reset monitor stays the last hardware protection.
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceTreeSupport.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include "ZizhanDailyProfile.h"
#include <string.h>
#include "ZizhanThermalPolicy.h"
#include "ZizhanTsensCore.h"

extern "C" struct vnode *rootvnode;
// Defined in ArmDTPlatform.cpp. The CPU/GPU governors read them with a weak
// declaration so a reduced kernel without this service still links.
extern "C" volatile uint32_t pong_cpu_thermal_level;
extern "C" volatile uint32_t pong_gpu_thermal_level;
// PongAdrenoScanout.cpp: nonzero after a frame was presented. Weak so a
// reduced build without scanout still starts the thermal service.
extern "C" volatile uint32_t pong_zc_flipped_tag __attribute__((weak));

namespace zizhan_thermal {

static zizhan_tsens::Inputs inputs;
static ZizhanThermalTuning tuning;
static ZizhanThermalConfig policy;
static ZizhanThermalState state;
static ZizhanThermalSample sample;
static zizhan_tsens::Observation observation[ZizhanThermalBanks];
static IOMemoryDescriptor *descriptor[ZizhanThermalBanks][2];
static IOMemoryMap *map[ZizhanThermalBanks][2];
// Load validation only: four busy kernel threads (half the cores), a local
// deadline in each thread, and a stop flag set by the sampler on a real
// reading at or above 75 C.
static const unsigned LoadThreads = 4;
static volatile uint32_t load_stop;
static uint64_t load_deadline_abs;
static uint32_t load_started, load_done;
static ZizhanThermalLoad load;

static uint64_t uptime_us(void) {
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
  return ns / 1000u;
}

static void publish(void) {
  const uint32_t cpu = tuning.mode == 2 ? state.cpu_level : 0u;
  const uint32_t gpu = tuning.mode == 2 ? state.gpu_level : 0u;
  __atomic_store_n(&pong_cpu_thermal_level, cpu, __ATOMIC_RELEASE);
  __atomic_store_n(&pong_gpu_thermal_level, gpu, __ATOMIC_RELEASE);
}

static void unmap_banks(void) {
  for (unsigned b = 0; b < ZizhanThermalBanks; ++b) {
    for (unsigned k = 0; k < 2; ++k) {
      if (map[b][k]) { map[b][k]->release(); map[b][k] = nullptr; }
      if (descriptor[b][k]) { descriptor[b][k]->release(); descriptor[b][k] = nullptr; }
    }
  }
}

static bool map_banks(void) {
  for (unsigned b = 0; b < ZizhanThermalBanks; ++b) {
    const zizhan_tsens::Bank &bank = inputs.bank[b];
    descriptor[b][0] = IOMemoryDescriptor::withPhysicalAddress(
        bank.srot, bank.srot_bytes, kIODirectionIn);
    descriptor[b][1] = IOMemoryDescriptor::withPhysicalAddress(
        bank.tm, bank.tm_bytes, kIODirectionIn);
    if (!descriptor[b][0] || !descriptor[b][1]) { unmap_banks(); return false; }
    map[b][0] = descriptor[b][0]->map(kIOMapAnywhere | kIOMapInhibitCache);
    map[b][1] = descriptor[b][1]->map(kIOMapAnywhere | kIOMapInhibitCache);
    if (!map[b][0] || !map[b][1] || !map[b][0]->getVirtualAddress() ||
        !map[b][1]->getVirtualAddress()) {
      unmap_banks();
      return false;
    }
  }
  return true;
}

// The sample lives in a namespace static, not on the 16 KiB thread stack.
static void build_sample(uint64_t time_us) {
  memset(&sample, 0, sizeof sample);
  sample.time_us = time_us;
  for (unsigned b = 0; b < ZizhanThermalBanks; ++b) {
    if (!map[b][0] || !map[b][1]) continue;
    const volatile uint8_t *s =
        reinterpret_cast<const volatile uint8_t *>(map[b][0]->getVirtualAddress());
    const volatile uint8_t *t =
        reinterpret_cast<const volatile uint8_t *>(map[b][1]->getVirtualAddress());
    observation[b] = zizhan_tsens::observe([s, t](bool is_srot, uint32_t offset) -> uint32_t {
      const volatile uint32_t *reg =
          reinterpret_cast<const volatile uint32_t *>((is_srot ? s : t) + offset);
      const uint32_t value = *reg;
      __asm__ volatile("dmb oshld" ::: "memory");
      return value;
    });
    const zizhan_tsens::Observation &out = observation[b];
    if (out.state != zizhan_tsens::State::Pass &&
        out.state != zizhan_tsens::State::Invalid) continue;
    sample.bank_valid[b] = 1;
    for (unsigned n = 0; n < ZizhanThermalSensors; ++n) {
      if (out.sensors[n].state != zizhan_tsens::State::Pass) continue;
      sample.valid_mask[b] |= (uint16_t)(1u << n);
      sample.temp_mc[b][n] = out.sensors[n].milli_celsius;
    }
  }
}

// Peak across every valid sensor, used only for the bounded-load stop. The
// policy itself keeps its CPU/GPU groups separate.
static int32_t sample_peak(unsigned *bank, unsigned *index) {
  int32_t peak = INT32_MIN;
  if (bank) *bank = 0;
  if (index) *index = 0;
  for (unsigned b = 0; b < ZizhanThermalBanks; ++b) {
    if (!sample.bank_valid[b]) continue;
    for (unsigned n = 0; n < ZizhanThermalSensors; ++n) {
      if (!(sample.valid_mask[b] & (uint16_t)(1u << n))) continue;
      if (sample.temp_mc[b][n] <= peak) continue;
      peak = sample.temp_mc[b][n];
      if (bank) *bank = b;
      if (index) *index = n;
    }
  }
  return peak;
}

static void load_worker(void *arg, wait_result_t) {
  (void)arg;
  uint64_t slice = 0;
  nanoseconds_to_absolutetime(4000000ull, &slice);
  for (;;) {
    if (__atomic_load_n(&load_stop, __ATOMIC_ACQUIRE)) break;
    const uint64_t now = mach_absolute_time();
    if (now >= load_deadline_abs) break;
    const uint64_t until = now + slice < load_deadline_abs ? now + slice
                                                           : load_deadline_abs;
    while (mach_absolute_time() < until)
      for (volatile uint32_t i = 0; i < 4096; ++i) {}
    IOSleep(1);
  }
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

// The load gate (worker loop) uses the first presented frame as the desktop proxy; a reduced build
// without the scanout symbol falls back to 60 s of uptime.
static void start_load(void) {
  if (!tuning.load_seconds) return;
  __atomic_store_n(&load_stop, 0, __ATOMIC_RELEASE);
  load_started = 0;
  load_done = 0;
  const uint64_t now_us = uptime_us();
  zizhan_thermal_load_begin(&load, now_us, tuning.load_seconds);
  uint64_t ns = 0;
  nanoseconds_to_absolutetime((uint64_t)tuning.load_seconds * 1000000000ull, &ns);
  load_deadline_abs = mach_absolute_time() + ns;
  for (unsigned i = 0; i < LoadThreads; ++i) {
    thread_t thread = THREAD_NULL;
    if (kernel_thread_start(load_worker, (void *)(uintptr_t)i, &thread) == KERN_SUCCESS) {
      thread_deallocate(thread);
      ++load_started;
    }
  }
  if (!load_started) {
    IOLog("ZIZHAN_THERMAL: load SKIP thread-start seconds=%u\n",
          (unsigned)tuning.load_seconds);
    return;
  }
  IOLog("ZIZHAN_THERMAL: load start seconds=%u threads=%u hot_stop_mc=%d "
        "desktop=%s\n",
        (unsigned)tuning.load_seconds, (unsigned)load_started,
        (int)ZIZHAN_THERMAL_LOAD_HOT_MC,
        (&pong_zc_flipped_tag) ? "first-flip" : "root-settle");
}

static void service_load(uint64_t now_us, int32_t peak, unsigned bank,
                         unsigned index) {
  if (!load_started || load_done) return;
  const int result =
      zizhan_thermal_load_step(&load, now_us, peak, ZIZHAN_THERMAL_LOAD_HOT_MC);
  if (result == ZizhanThermalLoadRun) return;
  __atomic_store_n(&load_stop, 1, __ATOMIC_RELEASE);
  load_done = 1;
  const uint64_t elapsed_ms = now_us > load.start_us ? (now_us - load.start_us) / 1000u : 0;
  if (result == ZizhanThermalLoadHot)
    IOLog("ZIZHAN_THERMAL: load stop reason=hot max=%d bank=%u sensor=%u "
          "elapsed_ms=%llu threads=%u\n",
          (int)peak, bank, index, (unsigned long long)elapsed_ms,
          (unsigned)load_started);
  else
    IOLog("ZIZHAN_THERMAL: load stop reason=deadline elapsed_ms=%llu threads=%u\n",
          (unsigned long long)elapsed_ms, (unsigned)load_started);
}

static const char *sample_reason(int status, uint8_t prev_cpu, uint8_t prev_gpu,
                                 bool complete) {
  if (status == ZizhanThermalStale)
    return (state.cpu_level || state.gpu_level) ? "HOLD_STALE" : "STALE";
  if (state.cpu_level > prev_cpu && state.gpu_level > prev_gpu) return "CPU_GPU_HOT";
  if (state.cpu_level > prev_cpu) return "CPU_HOT";
  if (state.gpu_level > prev_gpu) return "GPU_HOT";
  if (state.cpu_level < prev_cpu || state.gpu_level < prev_gpu) return "RELEASE";
  if (!complete) return "INVALID";
  return "NONE";
}

static void log_sample(int status, uint8_t prev_cpu, uint8_t prev_gpu,
                       int32_t cpu_max, int32_t gpu_max, int cpu_any, int gpu_any,
                       bool complete) {
  const int32_t shown_cpu = cpu_any ? cpu_max : -1;
  const int32_t shown_gpu = gpu_any ? gpu_max : -1;
  IOLog("ZIZHAN_THERMAL: sample t_us=%llu mode=%u cpu=%u gpu=%u max_cpu=%d max_gpu=%d "
        "valid=%04x/%04x stale=%u reason=%s\n",
        (unsigned long long)sample.time_us, (unsigned)tuning.mode,
        (unsigned)state.cpu_level, (unsigned)state.gpu_level,
        (int)shown_cpu, (int)shown_gpu,
        (unsigned)sample.valid_mask[0], (unsigned)sample.valid_mask[1],
        status == ZizhanThermalStale ? 1u : 0u,
        sample_reason(status, prev_cpu, prev_gpu, complete));
}

static void log_summary(uint32_t samples, uint32_t stale, uint32_t incomplete,
                        int32_t cpu_max, int32_t gpu_max, int cpu_any, int gpu_any) {
  IOLog("ZIZHAN_THERMAL: summary max_cpu=%d max_gpu=%d cap=%u/%u mode=%u "
        "samples=%u stale=%u invalid=%u apply=%u\n",
        (int)(cpu_any ? cpu_max : -1), (int)(gpu_any ? gpu_max : -1),
        (unsigned)state.cpu_level, (unsigned)state.gpu_level,
        (unsigned)tuning.mode, (unsigned)samples, (unsigned)stale,
        (unsigned)incomplete, tuning.mode == 2 ? 1u : 0u);
}

static void worker(void *, wait_result_t) {
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i)
    IOSleep(100);
  if (!__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE)) {
    IOLog("ZIZHAN_THERMAL: SKIP root-timeout mode=%u\n", (unsigned)tuning.mode);
    publish();
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
    return;
  }
  if (!map_banks()) {
    IOLog("ZIZHAN_THERMAL: SKIP map-failed mode=%u\n", (unsigned)tuning.mode);
    publish();
    thread_terminate(current_thread());
    thread_block(THREAD_CONTINUE_NULL);
    return;
  }
  IOSleep(5000);
  // The load waits for the desktop and a cool complete sample (see ZIZHAN_THERMAL_LOAD_START_MC).
  bool load_pending = tuning.load_seconds != 0;
  uint64_t load_wait_from = 0;
  uint32_t samples = 0, stale = 0, incomplete = 0, since_summary = 0;
  for (;;) {
    const uint64_t now_us = uptime_us();
    build_sample(now_us);
    const uint8_t prev_cpu = state.cpu_level, prev_gpu = state.gpu_level;
    const int status = zizhan_thermal_step(&policy, &state, &sample);
    if (status == ZizhanThermalInvalid) {
      IOLog("ZIZHAN_THERMAL: FAIL policy-invalid mode=%u\n", (unsigned)tuning.mode);
      break;
    }
    publish();
    int32_t cpu_max = INT32_MIN, gpu_max = INT32_MIN;
    int cpu_all = 0, cpu_any = 0, gpu_all = 0, gpu_any = 0;
    zizhan_thermal_scan(&sample, (unsigned)ZizhanThermalCpuSensors, 1,
                        &cpu_max, &cpu_all, &cpu_any);
    zizhan_thermal_scan(&sample, (unsigned)ZizhanThermalGpuSensors, 0,
                        &gpu_max, &gpu_all, &gpu_any);
    const bool complete = sample.bank_valid[0] && sample.bank_valid[1] &&
                          cpu_all && gpu_all;
    unsigned hot_bank = 0, hot_index = 0;
    const int32_t peak = sample_peak(&hot_bank, &hot_index);
    if (load_pending) {
      const int desktop = (&pong_zc_flipped_tag) ? (__atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE) != 0)
                                                 : (now_us >= 60000000ull);
      if (desktop && !load_wait_from) load_wait_from = now_us;
      const int gate = zizhan_thermal_load_gate(desktop, complete, peak,
                                                load_wait_from ? now_us - load_wait_from : 0);
      if (gate == ZizhanThermalLoadGo) { load_pending = false; start_load(); }
      else if (gate == ZizhanThermalLoadGiveUp) {
        load_pending = false;
        IOLog("ZIZHAN_THERMAL: load SKIP not-cool peak=%d waited_s=120\n", (int)peak);
      }
    }
    service_load(now_us, peak, hot_bank, hot_index);
    if (status == ZizhanThermalStale) ++stale;
    if (!complete) ++incomplete;
    ++samples;
    log_sample(status, prev_cpu, prev_gpu, cpu_max, gpu_max, cpu_any, gpu_any, complete);
    if (++since_summary >= 10) {
      since_summary = 0;
      log_summary(samples, stale, incomplete, cpu_max, gpu_max, cpu_any, gpu_any);
    }
    IOSleep(1000);
  }
  __atomic_store_n(&load_stop, 1, __ATOMIC_RELEASE);
  publish();
  unmap_banks();
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start(IOService *provider) {
  uint32_t mode_arg = 0, test_cpu = 0, test_gpu = 0, load_s = 0;
  (void)PE_parse_boot_argn("zizhan_thermal_load", &load_s, sizeof load_s);
  const bool explicit_mode = PE_parse_boot_argn("zizhan_thermal", &mode_arg, sizeof mode_arg);
  if (!explicit_mode && zizhan_daily::profile_on()) {
    mode_arg = zizhan_daily::kThermalMode;
    test_cpu = zizhan_daily::kThermalCpuC;
    test_gpu = zizhan_daily::kThermalGpuC;
  }
  if (!mode_arg) {
    if (load_s)
      IOLog("ZIZHAN_THERMAL: SKIP load-without-mode load=%u\n", (unsigned)load_s);
    return;
  }
  (void)PE_parse_boot_argn("zizhan_thermal_test_cpu", &test_cpu, sizeof test_cpu);
  (void)PE_parse_boot_argn("zizhan_thermal_test_gpu", &test_gpu, sizeof test_gpu);
  if (zizhan_thermal_tune(mode_arg, test_cpu, test_gpu, load_s, &tuning) !=
      ZizhanThermalTuneOk) {
    IOLog("ZIZHAN_THERMAL: SKIP invalid-boot-arg zizhan_thermal=%u test_cpu=%u "
          "test_gpu=%u load=%u\n",
          (unsigned)mode_arg, (unsigned)test_cpu, (unsigned)test_gpu,
          (unsigned)load_s);
    return;
  }
  IORegistryEntry *chosen = provider->childFromPath("chosen", gIODTPlane);
  OSData *data = chosen ? OSDynamicCast(OSData, chosen->getProperty("zizhan-tsens-v2"))
                        : nullptr;
  const bool valid = data && data->getLength() == sizeof(inputs);
  if (valid) memcpy(&inputs, data->getBytesNoCopy(), sizeof(inputs));
  if (chosen) chosen->release();
  if (!valid || !zizhan_tsens::inputs_ok(inputs)) {
    IOLog("ZIZHAN_THERMAL: SKIP invalid-or-missing-board-inputs mode=%u\n",
          (unsigned)tuning.mode);
    return;
  }
  policy = zizhan_thermal_default_config();
  policy.cpu_clamp_mc = tuning.cpu_clamp_mc;
  policy.cpu_release_mc = tuning.cpu_release_mc;
  policy.gpu_clamp_mc = tuning.gpu_clamp_mc;
  policy.gpu_release_mc = tuning.gpu_release_mc;
  // UX: a hot reading adds 4 levels instead of jumping to the lowest one (ux-b: the UI crawled for ~60 s after one spike);
  // clamp + 10 C still jumps to the lowest level. zizhan_thermal_step=0 restores the v1 jump.
  {
    uint32_t stepArg = 4;
    (void)PE_parse_boot_argn("zizhan_thermal_step", &stepArg, sizeof stepArg);
    policy.clamp_step = uint8_t(stepArg > 20 ? 20 : stepArg);
    policy.critical_margin_mc = 10000;   // therm-s1: one 1 s sample can rise 12 C under load; 5 C skipped every step
  }
  zizhan_thermal_init(&state);
  if (!zizhan_thermal_config_ok(&policy)) {
    IOLog("ZIZHAN_THERMAL: SKIP invalid-policy mode=%u\n", (unsigned)tuning.mode);
    return;
  }
  thread_t thread = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &thread) != KERN_SUCCESS) {
    IOLog("ZIZHAN_THERMAL: FAIL worker-start mode=%u\n", (unsigned)tuning.mode);
    return;
  }
  IOLog("ZIZHAN_THERMAL: RUNNING mode=%u period_ms=1000 cpu_clamp=%d cpu_release=%d "
        "gpu_clamp=%d gpu_release=%d test_cpu=%u test_gpu=%u load_s=%u "
        "tsens-ro=1 apply=%u\n",
        (unsigned)tuning.mode, (int)policy.cpu_clamp_mc, (int)policy.cpu_release_mc,
        (int)policy.gpu_clamp_mc, (int)policy.gpu_release_mc,
        (unsigned)tuning.test_cpu_c, (unsigned)tuning.test_gpu_c,
        (unsigned)tuning.load_seconds,
        tuning.mode == 2 ? 1u : 0u);
  thread_deallocate(thread);
}

} // namespace zizhan_thermal
