/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* MODEM-01 stage 1b (zizhan_modem=2): boot the SM8475 modem (MPSS, "qcom,cape-modem-pas") once from Android's own firmware and
 * watch it come up, then stop it. Inputs from the live DT: PAS 4, carveout mpss_region 0x8bc00000 + 0x13200000, SMP2P items
 * in 435 / out 428 with remote pid 1, IPCC client 2 (SMP2P signal 2), crash reason SMEM item 421, rails cx + mss, AOSS
 * load_state "modem". The firmware is read in place from modem_b on UFS LUN 4 (ZizhanFat.h, read-only); no partition is
 * written. Nothing here talks QMI, so the modem never leaves its boot-time low-power mode: no radio transmit. It may wait
 * for rmtfs (EFS) and report fatal; either way it is stopped (SMP2P stop, then PAS shutdown) before the worker ends.
 * Needs pong_adsp=0: the AOSS QMP mailbox, SMEM mutex and IPCC IRQ belong to the ADSP worker when that runs. Polls, no IRQ. */
#include <IOKit/IOLib.h>
#include <IOKit/IOBufferMemoryDescriptor.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/thread.h>
#include <kern/clock.h>
#include <mach/mach_time.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "ZizhanFat.h"
#include "QcomMdt.h"
#include "QcomQmp.h"
#include "QcomRpmh.h"
#include "QcomScm.h"
#include "QcomSmem.h"
#include "QcomSmp2p.h"

extern "C" int pong_ufs_read_lun(uint8_t lun, uint64_t byte, void *dst, uint32_t length);
extern "C" bool pong_ufs_ready(void);
extern "C" bool pong_rpmh_vote(uint32_t address, uint32_t value);

namespace {
constexpr uint64_t kSmem = 0x80900000, kSmemSize = 0x200000, kMutex = 0x1f40000 + 3 * 0x1000, kIpcc = 0x0ed18000, kAoss = 0xc300000;
constexpr uint64_t kCmdDb = 0x80860000, kCmdDbSize = 0x20000;
constexpr uint64_t kMpssBase = 0x8bc00000, kMpssSize = 0x13200000;
constexpr uint32_t kPas = 4, kRemote = 1, kItemIn = 435, kItemOut = 428, kItemCrash = 421, kIpccMpss = 2;
constexpr uint32_t kFatal = 1u << 0, kReady = 1u << 1, kHandover = 1u << 2, kStopAck = 1u << 3;
constexpr unsigned kPieces = 40;

struct Win { IOMemoryMap *map; volatile uint8_t *va; };
bool mapWin(Win &w, uint64_t base, uint64_t size, IOOptionBits cache = kIOMapInhibitCache) {
  IODeviceMemory *m = IODeviceMemory::withRange(base, size);
  w.map = m ? m->map(kIOMapAnywhere | cache) : nullptr;
  if (m) m->release();
  w.va = w.map ? reinterpret_cast<volatile uint8_t *>(w.map->getVirtualAddress()) : nullptr;
  return w.va != nullptr;
}
void unmapWin(Win &w) { if (w.map) w.map->release(); w = {}; }
uint32_t rd(const Win &w, uint64_t at) { return *reinterpret_cast<volatile uint32_t *>(w.va + at); }
void wr(const Win &w, uint64_t at, uint32_t v) { *reinterpret_cast<volatile uint32_t *>(w.va + at) = v; __asm__ volatile("dsb sy" ::: "memory"); }

Win gSmemW, gMutex, gIpcc, gAoss;
QcomSmem gSmem;
QcomSmemPartition gPart;
QcomSmp2p gP2p;
QcomQmp gQmp;
zizhan_fat::Volume gVol;
zizhan_fat::File gPiece[kPieces];
bool gPieceOpen[kPieces], gPieceMissing[kPieces];
QcomMdtSplit gSplit;
QcomMdtImage gMdt;
uint8_t gHeaders[16384];
uint8_t gDb[kCmdDbSize];

uint64_t nowMs() { uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000000; }
void ring(uint32_t client, uint32_t signal) { wr(gIpcc, QCOM_IPCC_SEND, qcom_ipcc_id(client, signal)); }
void kickAop(void *) { ring(QCOM_IPCC_CLIENT_AOP, QCOM_IPCC_SIGNAL_GLINK_QMP); }

struct ScmResult { int64_t x0; uint64_t x1; };
ScmResult scm(uint32_t command, uint32_t arginfo, uint64_t a, uint64_t b, uint64_t c) {
  ScmResult result = {-1, 0};
  for (unsigned busy = 0; busy < 20; ++busy) {
    uint64_t function = qcom_scm_fn(QCOM_SCM_SVC_PIL, command), session = 0;
    unsigned resumes = 0;
    for (;;) {
      register uint64_t x0 __asm__("x0") = function, x1 __asm__("x1") = arginfo, x2 __asm__("x2") = a, x3 __asm__("x3") = b,
                        x4 __asm__("x4") = c, x5 __asm__("x5") = 0, x6 __asm__("x6") = session, x7 __asm__("x7") = 0;
      __asm__ volatile("smc #0" : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4), "+r"(x5), "+r"(x6), "+r"(x7)
                       :: "x8", "x9", "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17", "memory");
      result = {(int64_t)x0, x1};
      if ((int64_t)x0 != QCOM_SCM_INTERRUPTED || ++resumes > 100000) break;
      function = QCOM_SCM_INTERRUPTED;
      session = x6;
    }
    if (result.x0 != QCOM_SCM_V2_EBUSY) break;
    IOSleep(30);
  }
  return result;
}
bool scmOk(const ScmResult &r) { return r.x0 == 0 && r.x1 == 0; }
void cleanToPoc(const void *start, size_t length) {
  uint64_t ctr;
  __asm__ volatile("mrs %0, ctr_el0" : "=r"(ctr));
  const uintptr_t line = static_cast<uintptr_t>(4) << ((ctr >> 16) & 15);
  for (uintptr_t a = reinterpret_cast<uintptr_t>(start) & ~(line - 1); a < reinterpret_cast<uintptr_t>(start) + length; a += line)
    __asm__ volatile("dc civac, %0" :: "r"(a) : "memory");
  __asm__ volatile("dsb sy" ::: "memory");
}

bool lockSmem() {
  for (unsigned i = 0; i < 1000; ++i) {
    wr(gMutex, 0, 1);
    if (rd(gMutex, 0) == 1) return true;
    IODelay(10);
  }
  return false;
}
void unlockSmem() { wr(gMutex, 0, 0); }
volatile uint8_t *smemItem(uint16_t id, uint32_t *size) {
  uint32_t at = 0, bytes = 0;
  if (qcom_smem_find(&gSmem, &gPart, id, &at, &bytes)) return nullptr;
  if (size) *size = bytes;
  return gSmemW.va + at;
}

int lunRead(void *, uint64_t off, void *dst, uint32_t len) { return pong_ufs_read_lun(4, off, dst, len); }
int pieceRead(void *, unsigned piece, uint64_t offset, void *to, size_t length) {
  if (piece >= kPieces || gPieceMissing[piece]) return -1;
  if (!gPieceOpen[piece]) {
    char path[24];
    snprintf(path, sizeof path, "image/modem.b%02u", piece);
    if (zizhan_fat::open_path(&gVol, path, &gPiece[piece])) { gPieceMissing[piece] = true; return -2; }
    gPieceOpen[piece] = true;
  }
  return zizhan_fat::file_read(&gVol, &gPiece[piece], offset, to, uint32_t(length));
}
int joinedRead(void *ctx, uint64_t offset, void *to, size_t length) {
  return qcom_mdt_split_read(&gSplit, pieceRead, ctx, offset, to, length);
}

void readCrash() {
  QcomSmemPartition global;
  uint32_t at = 0, size = 0;
  if (qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_GLOBAL, QCOM_SMEM_HOST_GLOBAL, &global) ||
      qcom_smem_find(&gSmem, &global, kItemCrash, &at, &size) || !size) { IOLog("ZIZHAN_MPSS: crash-reason none\n"); return; }
  char text[120];
  size_t n = 0;
  for (; n < sizeof text - 1 && n < size; ++n) { const char c = char(gSmemW.va[at + n]); if (!c) break; text[n] = (c >= 32 && c < 127) ? c : '.'; }
  text[n] = 0;
  IOLog("ZIZHAN_MPSS: crash-reason '%s'\n", text);
}

/* The modem's inbound SMP2P item appears once it runs; the entry "slave-kernel" carries ready/fatal/handover/stop-ack. */
bool slaveBits(uint32_t *bits) {
  if (!gP2p.in) {
    volatile uint8_t *in = smemItem(kItemIn, nullptr);
    if (!in || qcom_smp2p_check_in(reinterpret_cast<volatile uint32_t *>(in), 0, kRemote)) return false;
    gP2p.in = reinterpret_cast<volatile uint32_t *>(in);
  }
  const int index = qcom_smp2p_find_in(&gP2p, "slave-kernel");
  if (index < 0) return false;
  *bits = qcom_smp2p_in_value(&gP2p, index);
  return true;
}

bool qmpLoadState(bool on) {
  const char *cmd = on ? "{class: image, res: load_state, name: modem, val: on}" : "{class: image, res: load_state, name: modem, val: off}";
  if (gQmp.state != QcomQmpOpen) return false;
  if (qcom_qmp_send(&gQmp, cmd) != QcomQmpSending) return false;
  for (unsigned i = 0; i < 1000; ++i) { if (qcom_qmp_step(&gQmp) == QcomQmpOpen) return true; IOSleep(1); }
  qcom_qmp_abandon(&gQmp);
  return false;
}

void worker(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_mpss");
  for (unsigned i = 0; i < 1200 && !pong_ufs_ready(); ++i) IOSleep(100);
  IOSleep(20000);   // after SpringBoard and Wi-Fi settle
  uint64_t t0 = nowMs();
  int stage = 0, rc = 0;
  IOBufferMemoryDescriptor *meta = nullptr;
  bool pasStarted = false;
  uint32_t bits = 0, lastBits = 0xffffffffu;
  int stopEntry = -1;
  QcomRpmhResource cx{}, mss{};
  uint32_t cxIdx = 0, mssIdx = 0;
  Win region = {};
  uint8_t *bounce = nullptr;
  zizhan_fat::GptPart part{};
#define STEP(n, cond) do { stage = (n); if (!(cond)) { goto out; } } while (0)
  STEP(1, pong_ufs_ready());
  STEP(2, mapWin(gSmemW, kSmem, kSmemSize) && mapWin(gMutex, kMutex, 0x1000) && mapWin(gIpcc, kIpcc, 0x1000) && mapWin(gAoss, kAoss, 0x1000));
  gSmem = {gSmemW.va, uint32_t(kSmemSize)};
  STEP(3, !(rc = zizhan_fat::gpt_find(lunRead, nullptr, 4096, "modem_b", &part)) || !(rc = zizhan_fat::gpt_find(lunRead, nullptr, 4096, "modem_a", &part)));
  STEP(4, !(rc = zizhan_fat::mount(&gVol, lunRead, nullptr, part.first_lba * 4096)));
  {
    zizhan_fat::File mdt;
    STEP(5, !(rc = zizhan_fat::open_path(&gVol, "image/modem.mdt", &mdt)) && mdt.size <= sizeof gHeaders);
    STEP(6, !(rc = zizhan_fat::file_read(&gVol, &mdt, 0, gHeaders, mdt.size)));
    STEP(7, !(rc = qcom_mdt_split(&gSplit, gHeaders, mdt.size)));
  }
  STEP(8, !(rc = qcom_mdt_parse(&gMdt, gSplit.size, joinedRead, nullptr)));
  IOLog("ZIZHAN_MPSS: firmware joined=%llu segments=%u span=%llu relocatable=%u\n", gSplit.size, gMdt.count, qcom_mdt_span(&gMdt), gMdt.relocatable ? 1u : 0u);
  STEP(9, qcom_mdt_span(&gMdt) <= kMpssSize);
  STEP(10, !(rc = qcom_smem_partition(&gSmem, QCOM_SMEM_HOST_APPS, kRemote, &gPart)));
  if (lockSmem()) {
    rc = qcom_smem_alloc(&gSmem, &gPart, kItemOut, QCOM_SMP2P_ITEM_BYTES);
    unlockSmem();
  } else rc = -103;
  STEP(11, rc == QcomSmemOk || rc == QcomSmemExists);
  {
    uint32_t outSize = 0;
    volatile uint8_t *out = smemItem(kItemOut, &outSize);
    STEP(12, out && outSize >= QCOM_SMP2P_ITEM_BYTES);
    qcom_smp2p_init_out(&gP2p, reinterpret_cast<volatile uint32_t *>(out), 0, kRemote);
    stopEntry = qcom_smp2p_add_entry(&gP2p, "master-kernel");
    STEP(13, stopEntry >= 0);
    ring(kIpccMpss, QCOM_IPCC_SIGNAL_SMP2P);
  }
  qcom_qmp_init(&gQmp, reinterpret_cast<volatile uint32_t *>(gAoss.va), 0x400, kickAop, nullptr);
  for (unsigned i = 0; i < 2000 && gQmp.state != QcomQmpOpen && gQmp.state >= 0; ++i) { qcom_qmp_step(&gQmp); IOSleep(1); }
  STEP(14, gQmp.state == QcomQmpOpen);
  STEP(15, qmpLoadState(true));
  {
    Win db = {};
    STEP(16, mapWin(db, kCmdDb, kCmdDbSize));
    for (uint64_t i = 0; i < kCmdDbSize; i += 4) { const uint32_t w = rd(db, i); memcpy(gDb + i, &w, 4); }
    unmapWin(db);
  }
  // RPMH_REGULATOR_LEVEL_NOM (384) for both, as the DT's cx-uV-uA/mx-uV-uA ask; CX is shared, so never the top level.
  STEP(17, !qcom_cmd_db_find(gDb, sizeof gDb, "cx.lvl", &cx) && !qcom_cmd_db_find(gDb, sizeof gDb, "mss.lvl", &mss) &&
           !qcom_rpmh_level_index(&cx, 384, &cxIdx) && !qcom_rpmh_level_index(&mss, 384, &mssIdx));
  STEP(18, pong_rpmh_vote(cx.address, cxIdx) && pong_rpmh_vote(mss.address, mssIdx));
  IOLog("ZIZHAN_MPSS: rails cx=%u mss=%u (NOM) load_state=on smp2p_out=ok\n", cxIdx, mssIdx);
  {
    const uint64_t bytes = qcom_mdt_metadata_size(&gMdt);
    meta = IOBufferMemoryDescriptor::inTaskWithPhysicalMask(kernel_task, (IOOptionBits)kIODirectionInOut | (IOOptionBits)kIOMemoryPhysicallyContiguous,
                                                            bytes, 0x00000000fffff000ull);   // below 4 GiB for the secure world
    STEP(19, meta && meta->prepare() == kIOReturnSuccess);
    uint8_t *va = static_cast<uint8_t *>(meta->getBytesNoCopy());
    STEP(20, !(rc = qcom_mdt_metadata(&gMdt, joinedRead, nullptr, va, bytes)));
    cleanToPoc(va, bytes);
    const uint64_t phys = meta->getPhysicalSegment(0, nullptr, kIOMemoryMapperNone);
    const ScmResult sup = scm(0x07u /* PAS_IS_SUPPORTED */, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    const ScmResult pre = scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    IOLog("ZIZHAN_MPSS: is_supported x0=%llx x1=%llx pre_shutdown x0=%llx x1=%llx meta_phys=%llx\n", uint64_t(sup.x0), sup.x1,
          uint64_t(pre.x0), pre.x1, phys);
    const ScmResult r = scm(QCOM_SCM_PAS_INIT_IMAGE, qcom_scm_args(2, QCOM_SCM_VAL, QCOM_SCM_RW, 0), kPas, phys, 0);
    IOLog("ZIZHAN_MPSS: init_image x0=%llx x1=%llx meta=%llu\n", uint64_t(r.x0), r.x1, bytes);
    STEP(21, scmOk(r));
    if (gMdt.relocatable) {
      const ScmResult m = scm(QCOM_SCM_PAS_MEM_SETUP, qcom_scm_args(3, 0, 0, 0), kPas, kMpssBase, qcom_mdt_span(&gMdt));
      STEP(22, scmOk(m));
    }
  }
  pasStarted = true;   // from here a failure must PAS_SHUTDOWN
  {
    STEP(23, mapWin(region, kMpssBase, kMpssSize, kIOMapWriteCombineCache));
    constexpr size_t kChunk = 256 * 1024;
    bounce = static_cast<uint8_t *>(IOMallocData(kChunk));
    STEP(24, bounce != nullptr);
    const uint64_t loadStart = nowMs();
    uint64_t loaded = 0;
    for (unsigned i = 0; !rc && i < gMdt.count; ++i) {
      const QcomMdtSegment &s = gMdt.segment[i];
      uint64_t at = 0;
      if (!s.load) continue;
      if (qcom_mdt_place(&gMdt, i, kMpssBase, kMpssSize, &at)) { rc = -7; break; }
      uint8_t *to = const_cast<uint8_t *>(region.va) + at;
      for (uint64_t done = 0; !rc && done < s.file_size;) {
        const size_t n = s.file_size - done < kChunk ? size_t(s.file_size - done) : kChunk;
        if (joinedRead(nullptr, s.file_offset + done, bounce, n)) { rc = -5; break; }
        memcpy(to + done, bounce, n);
        done += n;
      }
      if (!rc && s.memory_size > s.file_size) bzero(to + s.file_size, size_t(s.memory_size - s.file_size));
      loaded += s.file_size;
    }
    __asm__ volatile("dsb sy" ::: "memory");
    IOLog("ZIZHAN_MPSS: segments loaded=%llu rc=%d ms=%llu\n", loaded, rc, nowMs() - loadStart);
    STEP(25, rc == 0);
  }
  {
    const ScmResult r = scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    IOLog("ZIZHAN_MPSS: auth_and_reset x0=%lld x1=%llu\n", r.x0, r.x1);
    STEP(26, scmOk(r));
  }
  t0 = nowMs();
  for (uint64_t now = t0; now - t0 < 40000; now = nowMs()) {   // up to 10 s for ready, then watch to 40 s
    if (slaveBits(&bits) && bits != lastBits) {
      IOLog("ZIZHAN_MPSS: t=%llums slave-kernel=%x ready=%u fatal=%u handover=%u\n", now - t0, bits, (bits & kReady) ? 1u : 0u,
            (bits & kFatal) ? 1u : 0u, (bits & kHandover) ? 1u : 0u);
      lastBits = bits;
      if (bits & kFatal) { readCrash(); break; }
    }
    if (now - t0 > 10000 && !(lastBits != 0xffffffffu && (lastBits & kReady))) break;
    IOSleep(10);
  }
  stage = 27;
out:
  IOLog("ZIZHAN_MPSS: stage=%d rc=%d bits=%x pas_started=%u\n", stage, rc, lastBits == 0xffffffffu ? 0u : lastBits, pasStarted ? 1u : 0u);
  if (pasStarted) {
    if (stopEntry >= 0) {
      qcom_smp2p_update(&gP2p, stopEntry, 1, 1);
      ring(kIpccMpss, QCOM_IPCC_SIGNAL_SMP2P);
      const uint64_t s0 = nowMs();
      while (nowMs() - s0 < 5000 && !(slaveBits(&bits) && (bits & kStopAck))) IOSleep(10);
      IOLog("ZIZHAN_MPSS: stop-ack=%u after %llums\n", (bits & kStopAck) ? 1u : 0u, nowMs() - s0);
    }
    const ScmResult r = scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    IOLog("ZIZHAN_MPSS: pas_shutdown x0=%lld x1=%llu\n", r.x0, r.x1);
    if (stopEntry >= 0) qcom_smp2p_update(&gP2p, stopEntry, 1, 0);
  }
  if (gQmp.state == QcomQmpOpen) qmpLoadState(false);
  if (bounce) IOFreeData(bounce, 256 * 1024);
  unmapWin(region);
  if (meta) meta->release();
  IOLog("ZIZHAN_MPSS: DONE radio=0 qmi=0 (rails stay voted until reboot)\n");
#undef STEP
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}
}  // namespace

extern "C" void zizhan_mpss_start(void) {
  uint32_t mode = 0, adsp = 0;
  if (!PE_parse_boot_argn("zizhan_modem", &mode, sizeof mode) || mode != 2) return;
  if (PE_parse_boot_argn("pong_adsp", &adsp, sizeof adsp) && adsp) { IOLog("ZIZHAN_MPSS: SKIP needs pong_adsp=0 (AOSS/SMEM owner)\n"); return; }
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) != KERN_SUCCESS) { IOLog("ZIZHAN_MPSS: FAIL worker\n"); return; }
  thread_deallocate(t);
  IOLog("ZIZHAN_MPSS: RUNNING mode=2 pas=%u region=%llx+%llx\n", kPas, kMpssBase, kMpssSize);
}
