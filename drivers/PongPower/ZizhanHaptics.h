/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Tier 2.4 (PERIPH-01): IOKit SPMI v7 wrapper for PM8350B X-axis Linear Haptics.
 * Default unattended policy: zizhan_haptics_dry=1 (100% read-only observer verification, 0 motor writes).
 * Attended policy (run-video-5.sh): zizhan_haptics_dry=0 zizhan_user_present=1. */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <pexpert/pexpert.h>
#include "ZizhanHapticsCore.h"

namespace zizhan_haptics {

static IOLock *gHapLock = nullptr;
static IOMemoryMap *gCoreMap = nullptr, *gCnfgMap = nullptr, *gObsMap = nullptr, *gChnMap = nullptr;
static volatile uint32_t *gCoreVa = nullptr, *gCnfgVa = nullptr, *gObsVa = nullptr, *gChnVa = nullptr;
static uint32_t gTriggerCount = 0;

static volatile uint32_t *mapSpmi(uint64_t pa, uint64_t len, IOOptionBits extra, IOMemoryMap **out) {
  IODeviceMemory *m = IODeviceMemory::withRange(pa, len);
  *out = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache | extra) : nullptr;
  if (m) m->release();
  return *out ? reinterpret_cast<volatile uint32_t *>((*out)->getVirtualAddress()) : nullptr;
}

static void pauseUs(void *, unsigned us) {
  if (us >= 2000u) IOSleep(us / 1000u);
  else IODelay(us);
}

static int trigger(int pattern) {
  uint32_t en = 1u;
  if (PE_parse_boot_argn("zizhan_haptics", &en, sizeof(en)) && en == 0u) return 0;
  uint32_t dry = 1u, present = 0u;
  (void)PE_parse_boot_argn("zizhan_haptics_dry", &dry, sizeof(dry));
  (void)PE_parse_boot_argn("zizhan_user_present", &present, sizeof(present));
  const bool allowWrite = (dry == 0u && present == 1u);

  if (!gHapLock) gHapLock = IOLockAlloc();
  if (!gHapLock) return -1;

  IOLockLock(gHapLock);
  if (!gCoreVa) gCoreVa = mapSpmi(0x0c400000ull, 0x3000u, kIOMapReadOnly, &gCoreMap);
  if (!gCnfgVa) gCnfgVa = mapSpmi(0x0c42d000ull, 0x4000u, kIOMapReadOnly, &gCnfgMap);
  if (!gObsVa)  gObsVa  = mapSpmi(0x0c440000ull, 0x10000u, 0, &gObsMap);
  if (allowWrite && !gChnVa) gChnVa = mapSpmi(0x0c500000ull, 0x400000u, 0, &gChnMap);

  ZizhanHapticsReceipt rec{};
  int rc = ZHAP_ERR_ARBITER;
  if (gCoreVa && gCnfgVa && gObsVa) {
    const struct br_arbiter arb = {gCoreVa, gCnfgVa, 0u};
    rc = zizhan_haptics_trigger_core(
        &arb, gObsVa, allowWrite ? gChnVa : nullptr,
        pattern, dry ? 1 : 0, present ? 1 : 0,
        pauseUs, nullptr, &rec);
  }
  const uint32_t n = ++gTriggerCount;
  IOLockUnlock(gHapLock);

  if (n <= 12u || (n % 16u) == 0u || rc < 0) {
    IOLog("ZIZHAN_HAPTICS: %s n=%u pat=%d rc=%d dry=%u apid=%d/%d/%d owner=%d/%d/%d rev=%02x/%02x fault=0x%02x vmax=0x%02x reads=%u writes=%u\n",
          rc >= 0 ? "PASS" : "FAIL", n, pattern, rc, dry ? 1u : 0u,
          rec.apid_cfg, rec.apid_ptn, rec.apid_hbst,
          rec.owner_cfg, rec.owner_ptn, rec.owner_hbst,
          rec.cfg_rev2, rec.ptn_rev2, rec.fault_sts, rec.vmax_before,
          rec.reads_done, rec.writes_attempted);
  }
  return rc >= 0 ? 0 : -1;
}

} // namespace zizhan_haptics
