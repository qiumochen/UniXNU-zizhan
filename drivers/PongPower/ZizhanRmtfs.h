/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* MODEM-01 stage 1c-c: a read-only-backed rmtfs (QMI service 14) for the modem's EFS, included by ZizhanMpss.cpp.
 * The modem opens "/boot/modem_fs1|fs2|fsg|fsc", asks for a shared buffer and moves sectors between that buffer and the
 * partitions with RW_IOVEC. Here each partition (UFS LUN 5 modemst1/modemst2/fsg/fsc) is read once into a RAM shadow on
 * OPEN; modem writes land in the shadow only, so nothing on the phone's storage changes. The shared buffer lives in the
 * unused no-map CVP carveout (below 4 GiB) and is assigned HLOS+MSS_MSA RW with the secure-world MP_ASSIGN call first. */
#pragma once

namespace zizhan_rmtfs {
constexpr uint64_t kCvp = 0x9ee00000, kArgs = kCvp + 0x10000, kBuf = kCvp + 0x100000, kBufSize = 0x280000;
constexpr uint32_t kPort = 0x4001, kSector = 512;
constexpr uint32_t kVmidHlos = 3, kVmidMssMsa = 15, kPermRw = 0x6;

struct Part { const char *path, *gpt; uint8_t *shadow; uint64_t bytes; bool open; };
Part gParts[4] = {{"/boot/modem_fs1", "modemst1", nullptr, 0, false}, {"/boot/modem_fs2", "modemst2", nullptr, 0, false},
                  {"/boot/modem_fsg", "fsg", nullptr, 0, false}, {"/boot/modem_fsc", "fsc", nullptr, 0, false}};
Win gBufWin, gArgsWin;
bool gAssigned, gReady;
unsigned gReqs, gReads, gWrites, gFailures;
uint64_t gReadBytes, gWriteBytes;

int lun5(void *, uint64_t off, void *dst, uint32_t len) { return pong_ufs_read_lun(5, off, dst, len); }

ScmResult smcAssign(uint64_t memPhys, uint64_t memSz, uint64_t srcPhys, uint64_t srcSz, uint64_t dstPhys, uint64_t dstSz) {
  // QCOM_SCM_SVC_MP (0x0c) / QCOM_SCM_MP_ASSIGN (0x16): 7 args, the 4th onwards in a buffer whose address goes in x5.
  volatile uint64_t *extra = reinterpret_cast<volatile uint64_t *>(gArgsWin.va + 0x800);
  extra[0] = srcSz; extra[1] = dstPhys; extra[2] = dstSz; extra[3] = 0;
  __asm__ volatile("dsb sy" ::: "memory");
  const uint32_t arginfo = 7u | (1u << 4) /*RO*/ | (0u << 6) | (1u << 8) /*RO*/ | (0u << 10) | (1u << 12) /*RO*/ | (0u << 14) | (0u << 16);
  ScmResult result = {-1, 0};
  for (unsigned busy = 0; busy < 20; ++busy) {
    uint64_t function = qcom_scm_fn(0x0cu, 0x16u), session = 0;
    unsigned resumes = 0;
    for (;;) {
      register uint64_t x0 __asm__("x0") = function, x1 __asm__("x1") = arginfo, x2 __asm__("x2") = memPhys, x3 __asm__("x3") = memSz,
                        x4 __asm__("x4") = srcPhys, x5 __asm__("x5") = kArgs + 0x800, x6 __asm__("x6") = session, x7 __asm__("x7") = 0;
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

/* Lend the shared buffer to the modem (HLOS RW + MSS_MSA RW), as Linux qcom_scm_assign_mem does for rmtfs. */
bool assignBuffer() {
  volatile uint8_t *a = gArgsWin.va;
  uint64_t mem[2] = {kBuf, kBufSize};
  uint32_t src[1] = {kVmidHlos};
  struct Perm { uint32_t vmid, perm; uint64_t ctx; uint32_t ctx_size, unused; } dst[2] = {{kVmidHlos, kPermRw, 0, 0, 0}, {kVmidMssMsa, kPermRw, 0, 0, 0}};
  for (unsigned i = 0; i < sizeof mem; ++i) a[i] = reinterpret_cast<uint8_t *>(mem)[i];
  for (unsigned i = 0; i < sizeof src; ++i) a[0x100 + i] = reinterpret_cast<uint8_t *>(src)[i];
  for (unsigned i = 0; i < sizeof dst; ++i) a[0x200 + i] = reinterpret_cast<uint8_t *>(dst)[i];
  __asm__ volatile("dsb sy" ::: "memory");
  const ScmResult r = smcAssign(kArgs, sizeof mem, kArgs + 0x100, sizeof src, kArgs + 0x200, sizeof dst);
  IOLog("ZIZHAN_RMTFS: assign buf=%llx+%llx to HLOS+MSS_MSA x0=%llx x1=%llx\n", kBuf, kBufSize, uint64_t(r.x0), r.x1);
  return r.x0 == 0;
}

bool setup() {
  if (!mapWin(gArgsWin, kArgs, 0x1000, kIOMapInhibitCache) || !mapWin(gBufWin, kBuf, kBufSize, kIOMapInhibitCache)) {
    IOLog("ZIZHAN_RMTFS: FAIL map\n");
    return false;
  }
  for (uint64_t i = 0; i < kBufSize; i += 4) wr(gBufWin, i, 0);
  gAssigned = assignBuffer();
  gReady = true;
  return true;
}

int openPart(Part &p) {
  if (p.open) return 0;
  zizhan_fat::GptPart g{};
  int rc = zizhan_fat::gpt_find(lun5, nullptr, 4096, p.gpt, &g);
  if (rc) return rc;
  p.bytes = (g.last_lba - g.first_lba + 1) * 4096;
  if (p.bytes > 16 * 1024 * 1024) return -20;
  if (!p.shadow) p.shadow = static_cast<uint8_t *>(IOMallocData(p.bytes));
  if (!p.shadow) return -21;
  rc = pong_ufs_read_lun(5, g.first_lba * 4096, p.shadow, uint32_t(p.bytes));
  if (rc) return rc;
  p.open = true;
  return 0;
}

/* One QMI request to our port: type 0 = request. Returns the response length in r (0 = no reply). */
uint32_t handle(const uint8_t *q, uint32_t n, uint8_t *r) {
  if (n < 7 || q[0] != 0) return 0;
  const uint16_t txn = uint16_t(q[1] | (q[2] << 8)), msg = uint16_t(q[3] | (q[4] << 8)), len = uint16_t(q[5] | (q[6] << 8));
  if (7u + len > n) return 0;
  const uint8_t *tl[8] = {};
  uint16_t tlen[8] = {};
  for (uint32_t at = 7; at + 3 <= 7u + len;) {
    const uint8_t t = q[at];
    const uint16_t l = uint16_t(q[at + 1] | (q[at + 2] << 8));
    if (at + 3 + l > 7u + len) break;
    if (t < 8) { tl[t] = q + at + 3; tlen[t] = l; }
    at += 3 + l;
  }
  ++gReqs;
  uint32_t o = 7;
  uint16_t result = 0, error = 0;
  uint8_t extraT = 0, extra[8] = {};
  uint16_t extraL = 0;
  auto u32 = [](const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return v; };
  switch (msg) {
  case 0x0001: {   // OPEN: path -> caller id
    char path[48] = "";
    if (tl[1]) { const uint32_t k = tlen[1] < sizeof path - 1 ? tlen[1] : sizeof path - 1; memcpy(path, tl[1], k); path[k] = 0; }
    int which = -1;
    for (int i = 0; i < 4; ++i) if (!strcmp(path, gParts[i].path)) which = i;
    const int rc = which >= 0 ? openPart(gParts[which]) : -1;
    IOLog("ZIZHAN_RMTFS: open '%s' -> id=%d rc=%d bytes=%llu\n", path, which, rc, which >= 0 ? gParts[which].bytes : 0);
    if (rc) { result = 1; error = 0x10; ++gFailures; }
    else { extraT = 0x10; const uint32_t id = uint32_t(which); memcpy(extra, &id, 4); extraL = 4; }
    break;
  }
  case 0x0002:     // CLOSE
    break;
  case 0x0003: {   // RW_IOVEC: caller id, direction (0 read, 1 write), iovec[] {sector, buffer offset, sectors}
    const uint32_t id = tl[1] && tlen[1] >= 4 ? u32(tl[1]) : 99;
    const uint8_t dir = tl[2] && tlen[2] >= 1 ? tl[2][0] : 0xff;
    bool ok = id < 4 && gParts[id].open && tl[3] && tlen[3] >= 1 && dir <= 1;
    const uint8_t count = ok ? tl[3][0] : 0;
    ok = ok && 1u + 12u * count <= tlen[3];
    for (unsigned i = 0; ok && i < count; ++i) {
      const uint8_t *v = tl[3] + 1 + 12 * i;
      // phys_offset is the absolute physical address inside the buffer ALLOC_BUFF handed out (as in Linux rmtfs_mem_ptr).
      const uint64_t sector = u32(v), phys = u32(v + 4), bytes = uint64_t(u32(v + 8)) * kSector;
      if (phys < kBuf || phys - kBuf + bytes > kBufSize || sector * kSector + bytes > gParts[id].bytes) {
        IOLog("ZIZHAN_RMTFS: iovec out of range sector=%llu phys=%llx bytes=%llu\n", sector, phys, bytes);
        ok = false; break;
      }
      const uint64_t off = phys - kBuf;
      uint8_t *shadow = gParts[id].shadow + sector * kSector;
      if (dir == 0) {
        for (uint64_t b = 0; b < bytes; b += 4) { uint32_t w; memcpy(&w, shadow + b, 4); wr(gBufWin, off + b, w); }
        gReadBytes += bytes; ++gReads;
      } else {
        for (uint64_t b = 0; b < bytes; b += 4) { const uint32_t w = rd(gBufWin, off + b); memcpy(shadow + b, &w, 4); }
        gWriteBytes += bytes; ++gWrites;
      }
    }
    if (!ok) { result = 1; error = 0x10; ++gFailures; IOLog("ZIZHAN_RMTFS: rw_iovec refused id=%u dir=%u count=%u\n", id, dir, count); }
    break;
  }
  case 0x0004: {   // ALLOC_BUFF: caller id, size -> physical address
    const uint32_t size = tl[2] && tlen[2] >= 4 ? u32(tl[2]) : 0;
    IOLog("ZIZHAN_RMTFS: alloc_buff size=%u -> %llx (assigned=%u)\n", size, kBuf, gAssigned ? 1u : 0u);
    if (!size || size > kBufSize) { result = 1; error = 0x10; ++gFailures; }
    else { extraT = 0x10; const uint64_t a = kBuf; memcpy(extra, &a, 8); extraL = 8; }
    break;
  }
  case 0x0005:     // GET_DEV_ERROR -> status 0
    extraT = 0x10; extra[0] = 0; extraL = 1;
    break;
  default:
    IOLog("ZIZHAN_RMTFS: unknown msg 0x%x len=%u\n", msg, len);
    result = 1; error = 0x1e;
  }
  r[o] = 0x02; r[o + 1] = 4; r[o + 2] = 0;
  memcpy(r + o + 3, &result, 2); memcpy(r + o + 5, &error, 2);
  o += 7;
  if (extraT) { r[o] = extraT; r[o + 1] = uint8_t(extraL); r[o + 2] = 0; memcpy(r + o + 3, extra, extraL); o += 3 + extraL; }
  r[0] = 2; r[1] = uint8_t(txn); r[2] = uint8_t(txn >> 8); r[3] = uint8_t(msg); r[4] = uint8_t(msg >> 8);
  r[5] = uint8_t(o - 7); r[6] = uint8_t((o - 7) >> 8);
  return o;
}
}  // namespace zizhan_rmtfs
