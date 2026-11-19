/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* MODEM-01 stage 1a (zizhan_modem=1): read-only proof that the modem firmware and EFS can be reached from iPadOS.
 * Android keeps them on UFS LUN 4 (modem_a/b = NON-HLOS vfat, "image/modem.mdt" + modem.b00..b31, ~110 MB) and LUN 5
 * (modemst1/modemst2/fsg/fsc, the EFS that rmtfs serves). This walks the GPTs, mounts modem_b (falls back to modem_a)
 * with ZizhanFat.h, reads every segment once and logs sizes and FNV-1a hashes, then lists the EFS partitions.
 * Nothing is written and the modem is not started. Reads go through NativeUfs pong_ufs_read_lun (LUN 4/5 READ(10) only). */
#pragma once
#include <IOKit/IOLib.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "ZizhanFat.h"

extern "C" int pong_ufs_read_lun(uint8_t lun, uint64_t byte, void *dst, uint32_t length);
extern "C" bool pong_ufs_ready(void);

namespace zizhan_modem_probe {
static int lun_read(void *ctx, uint64_t off, void *dst, uint32_t len) {
  return pong_ufs_read_lun(uint8_t(uintptr_t(ctx)), off, dst, len);
}
static uint64_t fnv(uint64_t h, const uint8_t *p, uint32_t n) {
  for (uint32_t i = 0; i < n; ++i) { h ^= p[i]; h *= 1099511628211ull; }
  return h;
}
static zizhan_fat::Volume gVol;   // 4 KiB FAT cache: keep it off the kernel stack

static void worker(void *, wait_result_t) {
  for (unsigned i = 0; i < 1200 && !pong_ufs_ready(); ++i) IOSleep(100);
  if (!pong_ufs_ready()) { IOLog("ZIZHAN_MODEM: SKIP ufs-not-ready\n"); thread_terminate(current_thread()); thread_block(THREAD_CONTINUE_NULL); return; }
  void *lun4 = reinterpret_cast<void *>(uintptr_t(4)), *lun5 = reinterpret_cast<void *>(uintptr_t(5));
  zizhan_fat::GptPart part{};
  const char *which = "modem_b";
  int rc = zizhan_fat::gpt_find(lun_read, lun4, 4096, which, &part);
  if (rc) { which = "modem_a"; rc = zizhan_fat::gpt_find(lun_read, lun4, 4096, which, &part); }
  IOLog("ZIZHAN_MODEM: lun4 gpt=%d part=%s lba=%llu..%llu\n", rc, which, part.first_lba, part.last_lba);
  if (!rc) rc = zizhan_fat::mount(&gVol, lun_read, lun4, part.first_lba * 4096);
  if (!rc) IOLog("ZIZHAN_MODEM: fat type=%u cluster=%u clusters=%u\n", gVol.type, gVol.cluster_bytes, gVol.cluster_count);
  uint8_t *buf = rc ? nullptr : static_cast<uint8_t *>(IOMalloc(256 * 1024));
  uint64_t total = 0;
  unsigned found = 0;
  const uint64_t started = mach_absolute_time();
  for (int seg = -1; buf && seg < 64; ++seg) {
    char path[32];
    if (seg < 0) strlcpy(path, "image/modem.mdt", sizeof path);
    else snprintf(path, sizeof path, "image/modem.b%02d", seg);
    zizhan_fat::File f;
    if (zizhan_fat::open_path(&gVol, path, &f)) continue;
    uint64_t h = 1469598103934665603ull;
    int r = 0;
    for (uint64_t at = 0; at < f.size && !r; at += 256 * 1024) {
      const uint32_t n = uint32_t(f.size - at < 256 * 1024 ? f.size - at : 256 * 1024);
      r = zizhan_fat::file_read(&gVol, &f, at, buf, n);
      if (!r) h = fnv(h, buf, n);
    }
    ++found;
    total += f.size;
    if (seg < 0 || seg == 23 || seg == 31 || r) IOLog("ZIZHAN_MODEM: %s size=%u read=%d fnv=%016llx\n", path, f.size, r, h);
  }
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time() - started, &ns);
  if (buf) IOFree(buf, 256 * 1024);
  IOLog("ZIZHAN_MODEM: firmware files=%u bytes=%llu ms=%llu\n", found, total, ns / 1000000);
  static const char *const efs[] = {"modemst1", "modemst2", "fsg", "fsc"};
  for (const char *name : efs) {
    zizhan_fat::GptPart p{};
    const int g = zizhan_fat::gpt_find(lun_read, lun5, 4096, name, &p);
    IOLog("ZIZHAN_MODEM: lun5 %s gpt=%d blocks=%llu\n", name, g, g ? 0ull : p.last_lba - p.first_lba + 1);
  }
  IOLog("ZIZHAN_MODEM: DONE readonly=1 modem_started=0\n");
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start() {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_modem", &mode, sizeof mode) || mode != 1) return;
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) != KERN_SUCCESS) { IOLog("ZIZHAN_MODEM: FAIL worker\n"); return; }
  thread_deallocate(t);
  IOLog("ZIZHAN_MODEM: RUNNING mode=1 readonly=1\n");
}
}  // namespace zizhan_modem_probe
