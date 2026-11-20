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
#include "QcomGlink.h"
#include "QcomMdt.h"
#include "QcomQmp.h"
#include "QcomRpmh.h"
#include "QcomScm.h"
#include "QcomSmem.h"
#include "QcomSmp2p.h"

extern "C" int pong_ufs_read_lun(uint8_t lun, uint64_t byte, void *dst, uint32_t length);
extern "C" bool pong_ufs_ready(void);
extern "C" bool pong_rpmh_vote(uint32_t address, uint32_t value);
extern "C" volatile uint32_t pong_zc_flipped_tag __attribute__((weak));

namespace {
constexpr uint64_t kSmem = 0x80900000, kSmemSize = 0x200000, kMutex = 0x1f40000 + 3 * 0x1000, kIpcc = 0x0ed18000, kAoss = 0xc300000;
constexpr uint64_t kCmdDb = 0x80860000, kCmdDbSize = 0x20000;
constexpr uint64_t kMpssBase = 0x8bc00000, kMpssSize = 0x13200000;
/* Android gives the modem PAS device the linux,cma pool (alloc-ranges below 4 GiB) for its metadata; our contiguous
 * sub-4 GiB allocation fails, so the metadata goes into the unused, no-map CVP carveout instead (iPadOS never runs CVP). */
constexpr uint64_t kMetaBase = 0x9ee00000, kMetaSize = 0x10000;
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

/* Stage 1c-a: GLINK on the MPSS SMEM edge (descriptor 478, our FIFO 479, the modem's 480; IPCC client 2 signal 0) and the
 * "IPCRTR" channel; QRTR v1 with APPS as node 1. We answer HELLO and log every control packet (the modem's NEW_SERVER list,
 * its lookups); no QMI is sent. Polled from the worker, no IRQ. */

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

#include "ZizhanRmtfs.h"

void ring(uint32_t client, uint32_t signal);
bool lockSmem();
void unlockSmem();
volatile uint8_t *smemItem(uint16_t id, uint32_t *size);
QcomGlink gGlink;
QcomGlinkChannel *gIpcrtr;
uint8_t gIpcrtrStore[4 * 0x4400];
bool gGlinkOn, gHelloSent;
unsigned gCtlLogged, gDataSeen;
constexpr uint32_t kQrtrNodeApps = 1, kQrtrPortCtrl = 0xfffffffeu;
void kickGlink(void *) { ring(kIpccMpss, QCOM_IPCC_SIGNAL_GLINK_QMP); }
void qrtrSendHello() {
  uint32_t pkt[8 + 5] = {};
  pkt[0] = 1; pkt[1] = 2;                     // version 1, HELLO
  pkt[2] = kQrtrNodeApps; pkt[3] = kQrtrPortCtrl;
  pkt[5] = 20;                                // payload: le32 cmd + 16 bytes
  pkt[6] = 0xffffffffu; pkt[7] = kQrtrPortCtrl;
  pkt[8] = 2;                                 // cmd HELLO
  const int rc = qcom_glink_send(&gGlink, gIpcrtr, pkt, sizeof pkt);
  IOLog("ZIZHAN_MPSS: qrtr hello sent rc=%d\n", rc);
  gHelloSent = rc == QcomGlinkOk;
}
/* Stage 1c-b: a minimal pd-mapper. The modem asks the APPS SERVREG locator (QMI service 0x40, QRTR instance 0x101) which
 * protection domains provide a service; Linux answers from the firmware's *.jsn. modemr.jsn: domain msm/modem/root_pd,
 * instance 180, services tms/servreg and gps/gps_service. Anything else gets an empty list. */
constexpr uint32_t kPdmPort = 0x4000, kModemNode = 0, kDmsClientPort = 0x4002;
/* Stage 1d: two read-only QMI DMS requests (service 2): Get Revision (0x23) and Get Operating Mode (0x2d). Nothing that
 * changes state (no Set Operating Mode, no NAS/WDS), and no identifiers (no IMEI/serial request). */
uint32_t gDmsPort, gUimPort;
bool gDmsAsked;
constexpr uint32_t kUimClientPort = 0x4003;

unsigned gPdmRequests;
void qrtrSend(uint32_t type, uint32_t dstNode, uint32_t dstPort, uint32_t srcPort, const uint8_t *payload, uint32_t len) {
  static uint8_t pkt[32 + 512];
  if (len > 512) return;
  uint32_t h[8] = {1, type, kQrtrNodeApps, srcPort, 0, len, dstNode, dstPort};
  memcpy(pkt, h, 32);
  memcpy(pkt + 32, payload, len);
  const uint32_t total = (32 + len + 3) & ~3u;
  for (uint32_t i = 32 + len; i < total; ++i) pkt[i] = 0;
  const int rc = qcom_glink_send(&gGlink, gIpcrtr, pkt, total);
  if (rc != QcomGlinkOk) IOLog("ZIZHAN_MPSS: qrtr send type=%u rc=%d\n", type, rc);
}
void pdmAnnounce() {
  uint32_t c[5] = {4, 0x40, 0x101, kQrtrNodeApps, kPdmPort};   // NEW_SERVER service 0x40 version 1 instance 1
  qrtrSend(4, kModemNode, kQrtrPortCtrl, kQrtrPortCtrl, reinterpret_cast<const uint8_t *>(c), sizeof c);
  IOLog("ZIZHAN_MPSS: pd-mapper announced service=0x40 instance=0x101 port=0x%x\n", kPdmPort);
  if (zizhan_rmtfs::gReady) {
    uint32_t r[5] = {4, 14, 0x1, kQrtrNodeApps, zizhan_rmtfs::kPort};   // NEW_SERVER rmtfs (service 14, version 1, instance 0 -> 0x1)
    qrtrSend(4, kModemNode, kQrtrPortCtrl, kQrtrPortCtrl, reinterpret_cast<const uint8_t *>(r), sizeof r);
    IOLog("ZIZHAN_MPSS: rmtfs announced service=14 instance=0x1 port=0x%x assigned=%u\n", zizhan_rmtfs::kPort,
          zizhan_rmtfs::gAssigned ? 1u : 0u);
  }
}
void pdmHandle(uint32_t srcNode, uint32_t srcPort, const uint8_t *q, uint32_t n) {
  if (n < 7) return;
  const uint8_t type = q[0];
  const uint16_t txn = uint16_t(q[1] | (q[2] << 8)), msg = uint16_t(q[3] | (q[4] << 8)), len = uint16_t(q[5] | (q[6] << 8));
  if (type != 0 || 7u + len > n) return;
  char name[80] = "";
  for (uint32_t at = 7; at + 3 <= 7u + len;) {
    const uint8_t t = q[at];
    const uint16_t l = uint16_t(q[at + 1] | (q[at + 2] << 8));
    if (at + 3 + l > 7u + len) break;
    if (t == 0x01) { const uint32_t k = l < sizeof name - 1 ? l : sizeof name - 1; memcpy(name, q + at + 3, k); name[k] = 0; }
    at += 3 + l;
  }
  ++gPdmRequests;
  uint8_t r[256];
  uint32_t o = 7;
  auto tlv = [&](uint8_t t, const uint8_t *v, uint16_t l) { r[o] = t; r[o + 1] = uint8_t(l); r[o + 2] = uint8_t(l >> 8); memcpy(r + o + 3, v, l); o += 3 + l; };
  const uint8_t ok[4] = {0, 0, 0, 0};
  tlv(0x02, ok, 4);
  const bool ours = msg == 0x21 && (!strcmp(name, "tms/servreg") || !strcmp(name, "gps/gps_service"));
  const uint16_t total = ours ? 1 : 0, rev = 1;
  if (msg == 0x21) {
    tlv(0x10, reinterpret_cast<const uint8_t *>(&total), 2);
    tlv(0x11, reinterpret_cast<const uint8_t *>(&rev), 2);
    uint8_t list[96];
    uint32_t L = 0;
    list[L++] = uint8_t(total);
    if (ours) {
      static const char dom[] = "msm/modem/root_pd";
      list[L++] = uint8_t(sizeof dom - 1);
      memcpy(list + L, dom, sizeof dom - 1); L += sizeof dom - 1;
      const uint32_t inst = 180, data = 0;
      memcpy(list + L, &inst, 4); L += 4;
      list[L++] = 0;                       // service_data_valid
      memcpy(list + L, &data, 4); L += 4;
    }
    tlv(0x12, list, uint16_t(L));
  }
  r[0] = 2; r[1] = uint8_t(txn); r[2] = uint8_t(txn >> 8); r[3] = uint8_t(msg); r[4] = uint8_t(msg >> 8);
  r[5] = uint8_t(o - 7); r[6] = uint8_t((o - 7) >> 8);
  qrtrSend(1, srcNode, srcPort, kPdmPort, r, o);
  IOLog("ZIZHAN_MPSS: pd-mapper req msg=0x%x txn=%u name='%s' from %u:%x -> %u domain(s)\n", msg, txn, name, srcNode, srcPort, total);
}
void ipcrtrState(void *, QcomGlinkChannel *, int up) {
  IOLog("ZIZHAN_MPSS: glink IPCRTR %s\n", up ? "up" : "down");
}
void ipcrtrReceive(void *, QcomGlinkChannel *, const uint8_t *d, uint32_t n) {
  if (n < 32) { IOLog("ZIZHAN_MPSS: qrtr short packet %u\n", n); return; }
  uint32_t h[8];
  memcpy(h, d, sizeof h);
  if (h[0] != 1) { IOLog("ZIZHAN_MPSS: qrtr version %u len=%u\n", h[0], n); return; }
  const uint32_t type = h[1];
  if (type == 1) {   // DATA (QMI)
    if (h[7] == kPdmPort && h[5] <= n - 32) { pdmHandle(h[2], h[3], d + 32, h[5]); return; }
    if (h[7] == kUimClientPort && h[5] >= 7 && h[5] <= n - 32) {   // UIM Get Card Status: result + first bytes only
      const uint8_t *q = d + 32;
      uint16_t result = 0xffff, error = 0xffff;
      if (h[5] >= 14 && q[7] == 0x02) { memcpy(&result, q + 10, 2); memcpy(&error, q + 12, 2); }
      char hex[3 * 24 + 1] = "";
      unsigned k = 0;
      for (unsigned i = 14; i < h[5] && i < 14 + 24; ++i) k += snprintf(hex + k, sizeof hex - k, "%02x ", q[i]);
      IOLog("ZIZHAN_MPSS: uim card status result=%u error=%u tlv-bytes=[%s]\n", result, error, hex);
      return;
    }
    if (h[7] == kDmsClientPort && h[5] >= 7 && h[5] <= n - 32) {
      const uint8_t *q = d + 32;
      const uint16_t msg = uint16_t(q[3] | (q[4] << 8)), len = uint16_t(q[5] | (q[6] << 8));
      char text[96] = "";
      uint16_t result = 0xffff, error = 0xffff;
      uint8_t mode = 0xff;
      for (uint32_t at = 7; at + 3 <= 7u + len && at + 3 <= h[5];) {
        const uint8_t t = q[at];
        const uint16_t l = uint16_t(q[at + 1] | (q[at + 2] << 8));
        if (at + 3 + l > h[5]) break;
        if (t == 0x02 && l >= 4) { memcpy(&result, q + at + 3, 2); memcpy(&error, q + at + 5, 2); }
        if (t == 0x01 && msg == 0x23) { const uint32_t k = l < sizeof text - 1 ? l : sizeof text - 1; memcpy(text, q + at + 3, k); text[k] = 0; }
        if (t == 0x01 && msg == 0x2d && l >= 1) mode = q[at + 3];
        at += 3 + l;
      }
      for (char *c = text; *c; ++c) if (*c < 32 || *c > 126) *c = '.';
      IOLog("ZIZHAN_MPSS: dms resp msg=0x%x result=%u error=%u revision='%s' operating_mode=%u\n", msg, result, error, text, mode);
      return;
    }
    if (h[7] == zizhan_rmtfs::kPort && h[5] <= n - 32) {
      static uint8_t reply[128];
      const uint32_t len = zizhan_rmtfs::handle(d + 32, h[5], reply);
      if (len) qrtrSend(1, h[2], h[3], zizhan_rmtfs::kPort, reply, len);
      return;
    }
    if (gDataSeen++ < 8) IOLog("ZIZHAN_MPSS: qrtr data %u:%x -> %u:%x size=%u\n", h[2], h[3], h[6], h[7], h[5]);
    return;
  }
  uint32_t c[5] = {};
  memcpy(c, d + 32, n - 32 < sizeof c ? n - 32 : sizeof c);
  if (type == 4 && c[0] == 4 && c[1] == 2 && c[3] == kModemNode) gDmsPort = c[4];
  if (type == 4 && c[0] == 4 && c[1] == 11 && c[3] == kModemNode) gUimPort = c[4];
  if (gCtlLogged++ < 60)
    IOLog("ZIZHAN_MPSS: qrtr ctrl type=%u from %u:%x cmd=%u service=%u instance=%x node=%u port=%x\n", type, h[2], h[3], c[0], c[1],
          c[2], c[3], c[4]);
  if (type == 2 && !gHelloSent) { qrtrSendHello(); if (gHelloSent) pdmAnnounce(); }
}
void glinkStart() {
  int rc = QcomSmemOk;
  if (lockSmem()) {
    rc = qcom_smem_alloc(&gSmem, &gPart, 478, 32);
    if (rc == QcomSmemOk || rc == QcomSmemExists) rc = qcom_smem_alloc(&gSmem, &gPart, 479, 0x4000);
    unlockSmem();
  } else rc = -103;
  uint32_t dsz = 0, tsz = 0;
  volatile uint8_t *desc = smemItem(478, &dsz), *txf = smemItem(479, &tsz);
  if ((rc != QcomSmemOk && rc != QcomSmemExists) || !desc || !txf) { IOLog("ZIZHAN_MPSS: glink smem rc=%d\n", rc); return; }
  volatile uint32_t *dw = reinterpret_cast<volatile uint32_t *>(desc);
  const QcomGlinkFifo tx = {&dw[1], &dw[0], txf, tsz}, rx = {&dw[3], &dw[2], nullptr, 0};
  qcom_glink_init(&gGlink, tx, rx, kickGlink, nullptr);
  static const QcomGlinkClient client = {ipcrtrState, ipcrtrReceive};
  gIpcrtr = qcom_glink_channel(&gGlink, "IPCRTR", 0x4400, 4, gIpcrtrStore, &client, nullptr, 1);
  gGlinkOn = gIpcrtr != nullptr;
  IOLog("ZIZHAN_MPSS: glink start desc=%u tx=%u channel=%u\n", dsz, tsz, gGlinkOn ? 1u : 0u);
  zizhan_rmtfs::setup();
}
void glinkService() {
  if (!gGlinkOn) return;
  if (!gGlink.rx.data) {
    uint32_t size = 0;
    if (volatile uint8_t *rx = smemItem(480, &size)) qcom_glink_set_rx(&gGlink, rx, size);
  }
  const int r = qcom_glink_poll(&gGlink);
  if (r < 0 && !gGlink.broken) IOLog("ZIZHAN_MPSS: glink poll %d\n", r);
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
  // modem-6: loading 108 MB while the first desktop frame releases the boot console (a one-time full-panel clear with
  // preemption off) pushed that clear past the 6 ms preemption limit and panicked. Start only after the first frame.
  for (unsigned i = 0; i < 1200 && !(&pong_zc_flipped_tag && __atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE)); ++i) IOSleep(100);
  IOLog("ZIZHAN_MPSS: desktop frame seen=%u, starting in 10 s\n", (&pong_zc_flipped_tag && pong_zc_flipped_tag) ? 1u : 0u);
  IOSleep(10000);
  uint64_t t0 = nowMs(), nextReport = 0;
  uint32_t watchS = 60;
  (void)PE_parse_boot_argn("zizhan_modem_watch_s", &watchS, sizeof watchS);
  if (watchS < 30 || watchS > 1800) watchS = 60;
  // zizhan_modem_cycles=2|3: after a clean stop, boot the modem again in the same session (restart/cleanup check). Later
  // cycles only wait for ready and stop again: GLINK/QRTR stay on the first cycle's state, so no QMI there.
  // modem-18: the second load reset the whole SoC (no log survived), so leave this at 1 outside a deliberate test.
  uint32_t cycles = 1, cycle = 0;
  (void)PE_parse_boot_argn("zizhan_modem_cycles", &cycles, sizeof cycles);
  if (cycles < 1 || cycles > 3) cycles = 1;
  bool readySeen = false;
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
again:
  IOLog("ZIZHAN_MPSS: cycle %u/%u begin\n", cycle + 1, cycles);
  {
    const uint64_t bytes = qcom_mdt_metadata_size(&gMdt);
    Win metaWin = {};
    STEP(19, bytes <= kMetaSize && mapWin(metaWin, kMetaBase, kMetaSize, kIOMapInhibitCache));
    uint8_t *staging = static_cast<uint8_t *>(IOMallocData(kMetaSize));
    STEP(20, staging && !(rc = qcom_mdt_metadata(&gMdt, joinedRead, nullptr, staging, bytes)));
    for (uint64_t i = 0; i < bytes; i += 4) {
      uint32_t w = 0;
      memcpy(&w, staging + i, bytes - i < 4 ? size_t(bytes - i) : 4);
      wr(metaWin, i, w);
    }
    IOFreeData(staging, kMetaSize);
    unmapWin(metaWin);
    const uint64_t phys = kMetaBase;
    const ScmResult sup = scm(0x07u /* PAS_IS_SUPPORTED */, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    {   // which PAS ids does the secure world accept from this VM? (ADSP = 1 is known to boot)
      uint64_t yes = 0, err = 0;
      for (unsigned id = 0; id < 48; ++id) {
        const ScmResult q = scm(0x07u, qcom_scm_args(1, 0, 0, 0), id, 0, 0);
        if (q.x0) err |= 1ull << id; else if (q.x1) yes |= 1ull << id;
      }
      IOLog("ZIZHAN_MPSS: pas_supported_mask=%llx error_mask=%llx\n", yes, err);
    }
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
  nextReport = t0 + 10000;
  for (uint64_t now = t0; now - t0 < uint64_t(cycle ? 30 : watchS) * 1000; now = nowMs()) {   // up to 10 s for ready, then watch
    if (!cycle && gDmsPort && !gDmsAsked && gIpcrtr && gIpcrtr->up && now - t0 > 20000) {
      gDmsAsked = true;
      static const uint8_t rev[7] = {0, 1, 0, 0x23, 0, 0, 0}, opmode[7] = {0, 2, 0, 0x2d, 0, 0, 0};
      qrtrSend(1, kModemNode, gDmsPort, kDmsClientPort, rev, sizeof rev);
      qrtrSend(1, kModemNode, gDmsPort, kDmsClientPort, opmode, sizeof opmode);
      IOLog("ZIZHAN_MPSS: dms requests sent to 0:%x (get revision, get operating mode)\n", gDmsPort);
      if (gUimPort) {
        static const uint8_t card[7] = {0, 3, 0, 0x2f, 0, 0, 0};
        qrtrSend(1, kModemNode, gUimPort, kUimClientPort, card, sizeof card);
        IOLog("ZIZHAN_MPSS: uim get card status sent to 0:%x\n", gUimPort);
      }
    }
    if (now >= nextReport) {
      nextReport += 10000;
      IOLog("ZIZHAN_MPSS: glink version=%u rx=%u tx=%u unknown_opens=%u '%s' ipcrtr_up=%u ctrl=%u data=%u pdm=%u rmtfs req=%u rd=%u/%llu wr=%u/%llu fail=%u\n", gGlink.version_agreed,
            gGlink.rx_messages, gGlink.tx_messages, gGlink.unknown_opens, gGlink.unknown_names, (gIpcrtr && gIpcrtr->up) ? 1u : 0u,
            gCtlLogged, gDataSeen, gPdmRequests, zizhan_rmtfs::gReqs, zizhan_rmtfs::gReads, zizhan_rmtfs::gReadBytes,
            zizhan_rmtfs::gWrites, zizhan_rmtfs::gWriteBytes, zizhan_rmtfs::gFailures);
      uint32_t d = 0, tx = 0, rx = 0;
      const bool hd = smemItem(478, &d) != nullptr, ht = smemItem(479, &tx) != nullptr, hr = smemItem(480, &rx) != nullptr;
      IOLog("ZIZHAN_MPSS: t=%llums bits=%x glink_desc=%u/%u fifo_tx=%u/%u fifo_rx=%u/%u\n", now - t0, lastBits, hd ? 1u : 0u, d,
            ht ? 1u : 0u, tx, hr ? 1u : 0u, rx);
    }
    if (slaveBits(&bits) && bits != lastBits) {
      IOLog("ZIZHAN_MPSS: t=%llums slave-kernel=%x ready=%u fatal=%u handover=%u\n", now - t0, bits, (bits & kReady) ? 1u : 0u,
            (bits & kFatal) ? 1u : 0u, (bits & kHandover) ? 1u : 0u);
      lastBits = bits;
      if (bits & kFatal) { readCrash(); break; }
      if (bits & kReady) readySeen = true;
      if (!cycle && (bits & kReady) && !gGlinkOn) glinkStart();
    }
    if (!cycle) glinkService();
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
    // Every run so far got 0xffcfffb8 from the first PAS_SHUTDOWN after a clean stop-ack. Linux qcom_pas_stop retries
    // shutdown every 100 ms (50 times) where the secure world is still tearing the image down (decrypt_shutdown).
    ScmResult r = scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    const int64_t firstErr = r.x0;
    unsigned tries = 1;
    for (; r.x0 != 0 && tries < 50; ++tries) {
      IOSleep(100);
      r = scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    }
    IOLog("ZIZHAN_MPSS: pas_shutdown x0=%lld x1=%llu tries=%u first=%llx\n", r.x0, r.x1, tries, uint64_t(firstErr));
    if (stopEntry >= 0) qcom_smp2p_update(&gP2p, stopEntry, 1, 0);
    IOLog("ZIZHAN_MPSS: cycle %u/%u end ready=%u stop-ack=%u shutdown_ok=%u\n", cycle + 1, cycles, readySeen ? 1u : 0u,
          (bits & kStopAck) ? 1u : 0u, r.x0 == 0 ? 1u : 0u);
    if (stage == 27 && readySeen && r.x0 == 0 && cycle + 1 < cycles) {
      if (bounce) { IOFreeData(bounce, 256 * 1024); bounce = nullptr; }
      unmapWin(region);
      ++cycle;
      readySeen = false;
      pasStarted = false;
      bits = 0;
      lastBits = 0xffffffffu;
      rc = 0;
      IOSleep(2000);
      goto again;
    }
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
