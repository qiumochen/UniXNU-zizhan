// SPDX-License-Identifier: MIT
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
  publish();
  unmap_banks();
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start(IOService *provider) {
  uint32_t mode_arg = 0, test_cpu = 0, test_gpu = 0;
  if (!PE_parse_boot_argn("zizhan_thermal", &mode_arg, sizeof mode_arg) || !mode_arg)
    return;
  (void)PE_parse_boot_argn("zizhan_thermal_test_cpu", &test_cpu, sizeof test_cpu);
  (void)PE_parse_boot_argn("zizhan_thermal_test_gpu", &test_gpu, sizeof test_gpu);
  if (zizhan_thermal_tune(mode_arg, test_cpu, test_gpu, 0, &tuning) !=
      ZizhanThermalTuneOk) {
    IOLog("ZIZHAN_THERMAL: SKIP invalid-boot-arg zizhan_thermal=%u test_cpu=%u "
          "test_gpu=%u\n",
          (unsigned)mode_arg, (unsigned)test_cpu, (unsigned)test_gpu);
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
        "gpu_clamp=%d gpu_release=%d test_cpu=%u test_gpu=%u tsens-ro=1 apply=%u\n",
        (unsigned)tuning.mode, (int)policy.cpu_clamp_mc, (int)policy.cpu_release_mc,
        (int)policy.gpu_clamp_mc, (int)policy.gpu_release_mc,
        (unsigned)tuning.test_cpu_c, (unsigned)tuning.test_gpu_c,
        tuning.mode == 2 ? 1u : 0u);
  thread_deallocate(thread);
}

} // namespace zizhan_thermal
