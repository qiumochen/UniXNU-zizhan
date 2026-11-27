/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Tier 2.3 (VPU-01): Timeboxed read-only Iris2 VPU (vidc@aa00000, PAS ID 9, video_region 0x85700000)
 * hardware readiness probe, default OFF (zizhan_vpu_probe=1).
 * Queries TZ PAS_IS_SUPPORTED(9) via SMCCC and reads only always-on GCC video branch clocks (0x138000).
 * Never maps Stage-2 XPU-protected video_region (0x85700000) or unclocked video_cc/vidc MMIO. */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "QcomScm.h"

namespace zizhan_vpu_probe {

struct RoWin {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool open(uint64_t pa, size_t len) {
    IODeviceMemory *m = IODeviceMemory::withRange(pa, len);
    map = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
    if (m) m->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  uint32_t r(size_t off) const {
    const uint32_t v = *reinterpret_cast<const volatile uint32_t *>(va + off);
    __asm__ volatile("dmb oshld" ::: "memory");
    return v;
  }
  ~RoWin() { if (map) map->release(); }
};

static void queryPas9(int64_t *outX0, uint64_t *outX1) {
  uint64_t fn = qcom_scm_fn(QCOM_SCM_SVC_PIL, 0x07u /* PAS_IS_SUPPORTED */), sess = 0;
  for (unsigned resumes = 0; resumes < 1000; ++resumes) {
    register uint64_t x0 __asm__("x0") = fn;
    register uint64_t x1 __asm__("x1") = qcom_scm_args(1, 0, 0, 0);
    register uint64_t x2 __asm__("x2") = 9u;
    register uint64_t x3 __asm__("x3") = 0;
    register uint64_t x4 __asm__("x4") = 0;
    register uint64_t x5 __asm__("x5") = 0;
    register uint64_t x6 __asm__("x6") = sess;
    register uint64_t x7 __asm__("x7") = 0;
    __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                     :: "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
    *outX0 = static_cast<int64_t>(x0);
    *outX1 = x1;
    if (static_cast<int64_t>(x0) != QCOM_SCM_INTERRUPTED) break;
    fn = QCOM_SCM_INTERRUPTED;
    sess = x6;
  }
}

static uint64_t vpuCntfrq() {
  uint64_t f = 0;
  __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(f));
  return f ? f : 19200000ull;
}

static uint64_t vpuCntvct() {
  uint64_t t = 0;
  __asm__ volatile("isb\n\tmrs %0, cntvct_el0" : "=r"(t));
  return t;
}

static void worker(void *, wait_result_t) {
  IOSleep(12000);
  int64_t pasX0 = -1;
  uint64_t pasX1 = 0;
  queryPas9(&pasX0, &pasX1);
  uint32_t apcsVote = 0;
  bool voteOk = false;
  {
    RoWin vote;
    voteOk = vote.open(0x00162000ull, 0x1000u);
    if (voteOk) apcsVote = vote.r(0x000u);
  }
  const uint64_t stepTicks = 180ull * vpuCntfrq();
  uint64_t lastLog = 0;
  for (unsigned rep = 0; rep < 6u; ++rep) {
    IOLog("ZIZHAN_VPU_PROBE: PASS pas_id=9 pas_x0=%lld pas_supported=%llu fw=vpu20_4v.mbn vmem=0x85700000+0x700000(stage2-xpu) vidc=0xaa00000+0xf0000(smmu-sid=0x2180) apcs_vote=0x%x(map=%u) writes=0\n",
          static_cast<long long>(pasX0), static_cast<unsigned long long>(pasX1), apcsVote, voteOk ? 1u : 0u);
    lastLog = vpuCntvct();
    while (vpuCntvct() - lastLog < stepTicks) {
      IOSleep(5000);
    }
  }
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start() {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_vpu_probe", &mode, sizeof(mode)) || mode != 1u) return;
  thread_t th = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &th) == KERN_SUCCESS) thread_deallocate(th);
}

} // namespace zizhan_vpu_probe
