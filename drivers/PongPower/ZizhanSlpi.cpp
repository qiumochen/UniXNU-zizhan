/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SENS-01 stage 1 (zizhan_slpi=1): boot the SM8475 sensor DSP (SLPI, "qcom,waipio-slpi-pas") once from Android's own firmware,
 * bring up GLINK + QRTR, answer its pd-mapper queries and log the QMI services it announces, then stop it. Same recipe as
 * ZizhanMpss.cpp (MODEM-01), with the live-DT inputs for SLPI: PAS 12, carveout slpi_region 0x88000000 + 0x1900000, SMP2P
 * items in 481 / out 430 with remote pid 3, IPCC client 4, crash reason SMEM item 424, rails lcx + lmx at NOM (384), AOSS
 * load_state "slpi". Firmware image/slpi.mdt + b00..b24 is read in place from modem_b on UFS LUN 4 (read-only). pd-mapper
 * domains follow Linux qcom_pd_mapper.c: msm/slpi/root_pd and msm/slpi/sensor_pd, instance 90. No sensor is configured and
 * no stream is requested. Needs pong_adsp=0 and zizhan_modem unset (shared AOSS mailbox, SMEM mutex, CVP metadata area). */
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
constexpr uint64_t kMpssBase = 0x88000000, kMpssSize = 0x1900000;   // slpi_region
/* Android gives the modem PAS device the linux,cma pool (alloc-ranges below 4 GiB) for its metadata; our contiguous
 * sub-4 GiB allocation fails, so the metadata goes into the unused, no-map CVP carveout instead (iPadOS never runs CVP). */
constexpr uint64_t kMetaBase = 0x9ee00000, kMetaSize = 0x10000;
constexpr uint32_t kPas = 12, kRemote = 3, kItemIn = 481, kItemOut = 430, kItemCrash = 424, kIpccMpss = 4;
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
  IOLog("ZIZHAN_SLPI: qrtr hello sent rc=%d\n", rc);
  gHelloSent = rc == QcomGlinkOk;
}
/* Stage 1c-b: a minimal pd-mapper. The modem asks the APPS SERVREG locator (QMI service 0x40, QRTR instance 0x101) which
 * protection domains provide a service; Linux answers from the firmware's *.jsn. modemr.jsn: domain msm/modem/root_pd,
 * instance 180, services tms/servreg and gps/gps_service. Anything else gets an empty list. */
constexpr uint32_t kPdmPort = 0x4000, kModemNode = 9, kDmsClientPort = 0x4002;
/* Stage 1d: two read-only QMI DMS requests (service 2): Get Revision (0x23) and Get Operating Mode (0x2d). Nothing that
 * changes state (no Set Operating Mode, no NAS/WDS), and no identifiers (no IMEI/serial request). */
uint32_t gDmsPort, gUimPort;
bool gDmsAsked;
constexpr uint32_t kUimClientPort = 0x4003;
constexpr bool kModemQueries = false;   // DMS/UIM are modem services

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
  if (rc != QcomGlinkOk) IOLog("ZIZHAN_SLPI: qrtr send type=%u rc=%d\n", type, rc);
}
void pdmAnnounce() {
  uint32_t c[5] = {4, 0x40, 0x101, kQrtrNodeApps, kPdmPort};   // NEW_SERVER service 0x40 version 1 instance 1
  qrtrSend(4, kModemNode, kQrtrPortCtrl, kQrtrPortCtrl, reinterpret_cast<const uint8_t *>(c), sizeof c);
  IOLog("ZIZHAN_SLPI: pd-mapper announced service=0x40 instance=0x101 port=0x%x\n", kPdmPort);
  if (zizhan_rmtfs::gReady) {
    uint32_t r[5] = {4, 14, 0x1, kQrtrNodeApps, zizhan_rmtfs::kPort};   // NEW_SERVER rmtfs (service 14, version 1, instance 0 -> 0x1)
    qrtrSend(4, kModemNode, kQrtrPortCtrl, kQrtrPortCtrl, reinterpret_cast<const uint8_t *>(r), sizeof r);
    IOLog("ZIZHAN_SLPI: rmtfs announced service=14 instance=0x1 port=0x%x assigned=%u\n", zizhan_rmtfs::kPort,
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
  const bool ours = msg == 0x21 && !strcmp(name, "tms/servreg");
  const uint16_t total = ours ? 2 : 0, rev = 1;
  if (msg == 0x21) {
    tlv(0x10, reinterpret_cast<const uint8_t *>(&total), 2);
    tlv(0x11, reinterpret_cast<const uint8_t *>(&rev), 2);
    uint8_t list[128];
    uint32_t L = 0;
    list[L++] = uint8_t(total);
    static const char *const doms[2] = {"msm/slpi/root_pd", "msm/slpi/sensor_pd"};
    for (unsigned k = 0; ours && k < 2; ++k) {
      const uint32_t dl = uint32_t(strlen(doms[k])), inst = 90, data = 0;
      list[L++] = uint8_t(dl);
      memcpy(list + L, doms[k], dl); L += dl;
      memcpy(list + L, &inst, 4); L += 4;
      list[L++] = 0;                       // service_data_valid
      memcpy(list + L, &data, 4); L += 4;
    }
    tlv(0x12, list, uint16_t(L));
  }
  r[0] = 2; r[1] = uint8_t(txn); r[2] = uint8_t(txn >> 8); r[3] = uint8_t(msg); r[4] = uint8_t(msg >> 8);
  r[5] = uint8_t(o - 7); r[6] = uint8_t((o - 7) >> 8);
  qrtrSend(1, srcNode, srcPort, kPdmPort, r, o);
  IOLog("ZIZHAN_SLPI: pd-mapper req msg=0x%x txn=%u name='%s' from %u:%x -> %u domain(s)\n", msg, txn, name, srcNode, srcPort, total);
}
/* SENS-01 stage 2 (zizhan_slpi=2): the Qualcomm sensor client service (QMI 400, "SSC"). One QMI message (0x20) carries a
 * protobuf SscClientRequest {uid, msg_id, config{processor=APSS, wakeup}, request{msg}}; replies arrive as indications 0x21/0x22
 * with an SscClientResponse {uid, response[] {msg_id, timestamp, msg}}. Message numbers and field layout as documented by the
 * open libssc/libqmi projects (protocol facts only, no code taken): SUID lookup 512 -> 768 on the fixed SUID 0xabab..ab,
 * continuous enable 513 {float sample_rate} -> measurements 1025 {repeated float, accuracy}, disable 10. Read-only use: look up
 * registry, accel and ambient_light, stream the accelerometer at 10 Hz and log it; nothing is written to the sensor registry. */
constexpr uint32_t kSnsService = 400, kSnsClientPort = 0x4004;
uint32_t gSlpiMode;
uint32_t gSnsNode = 0xffffffffu, gSnsPort, gSnsTxn = 0x100, gSnsStage, gAccelSamples, gSnsInd;
uint64_t gAccelLo, gAccelHi, gSnsNextMs;
bool gAccelFound, gAccelOn;
uint32_t gAccel[3];   // raw IEEE-754 bits: the kernel is built soft-float, so no float arithmetic here
/* value * 1000 as an integer from float bits, integer math only (|v| < 2^20 is plenty for m/s2 and lux) */
int32_t milli(uint32_t bits) {
  const int32_t e = int32_t((bits >> 23) & 0xff) - 127;
  if (e < -20) return 0;
  if (e > 20) return (bits >> 31) ? -2000000000 : 2000000000;
  const uint64_t m = (bits & 0x7fffff) | 0x800000;   // 1.23 fixed point
  const uint64_t scaled = m * 1000;
  const uint64_t v = e >= 23 ? scaled << (e - 23) : scaled >> (23 - e);
  return (bits >> 31) ? -int32_t(v) : int32_t(v);
}
constexpr uint64_t kSuid = 0xababababababababULL;

struct Pb { uint8_t b[400]; uint32_t n; bool bad; };
void pbByte(Pb &p, uint8_t v) { if (p.n < sizeof p.b) p.b[p.n++] = v; else p.bad = true; }
void pbVar(Pb &p, uint64_t v) { do { uint8_t c = v & 0x7f; v >>= 7; pbByte(p, uint8_t(c | (v ? 0x80 : 0))); } while (v); }
void pbKey(Pb &p, uint32_t f, uint32_t w) { pbVar(p, (uint64_t(f) << 3) | w); }
void pbF32(Pb &p, uint32_t f, uint32_t v) { pbKey(p, f, 5); for (int i = 0; i < 4; ++i) pbByte(p, uint8_t(v >> (8 * i))); }
void pbF64(Pb &p, uint32_t f, uint64_t v) { pbKey(p, f, 1); for (int i = 0; i < 8; ++i) pbByte(p, uint8_t(v >> (8 * i))); }
void pbBytes(Pb &p, uint32_t f, const uint8_t *d, uint32_t l) { pbKey(p, f, 2); pbVar(p, l); for (uint32_t i = 0; i < l; ++i) pbByte(p, d[i]); }
void pbVarField(Pb &p, uint32_t f, uint64_t v) { pbKey(p, f, 0); pbVar(p, v); }

/* Minimal reader: next field of a buffer. */
struct PbIn { const uint8_t *d; uint32_t n, at; };
bool pbNext(PbIn &in, uint32_t &field, uint32_t &wire, uint64_t &val, const uint8_t *&ptr, uint32_t &len) {
  auto var = [&](uint64_t &v) { v = 0; for (int sh = 0; sh < 64; sh += 7) { if (in.at >= in.n) return false; const uint8_t c = in.d[in.at++]; v |= uint64_t(c & 0x7f) << sh; if (!(c & 0x80)) return true; } return false; };
  if (in.at >= in.n) return false;
  uint64_t key;
  if (!var(key)) return false;
  field = uint32_t(key >> 3); wire = uint32_t(key & 7); ptr = nullptr; len = 0; val = 0;
  if (wire == 0) return var(val);
  if (wire == 1 || wire == 5) { const uint32_t k = wire == 1 ? 8 : 4; if (in.at + k > in.n) return false; for (uint32_t i = 0; i < k; ++i) val |= uint64_t(in.d[in.at + i]) << (8 * i); in.at += k; return true; }
  if (wire == 2) { uint64_t l; if (!var(l) || in.at + l > in.n) return false; ptr = in.d + in.at; len = uint32_t(l); in.at += uint32_t(l); return true; }
  return false;
}

void snsSend(uint64_t lo, uint64_t hi, uint32_t msgId, const uint8_t *msg, uint32_t msgLen, const char *what) {
  if (gSnsNode == 0xffffffffu) return;
  Pb uid = {}, cfg = {}, body = {}, req = {};
  pbF64(uid, 1, lo); pbF64(uid, 2, hi);
  pbVarField(cfg, 1, 1); pbVarField(cfg, 2, 0);              // processor APSS, suspend mode wakeup
  if (msgLen) pbBytes(body, 2, msg, msgLen);
  pbBytes(req, 1, uid.b, uid.n); pbF32(req, 2, msgId); pbBytes(req, 3, cfg.b, cfg.n); pbBytes(req, 4, body.b, body.n);
  if (req.bad) { IOLog("ZIZHAN_SLPI: sns request too large (%s)\n", what); return; }
  static uint8_t q[7 + 3 + 2 + sizeof req.b + 4];
  uint32_t o = 7;
  q[o++] = 0x01; const uint16_t tl = uint16_t(2 + req.n); q[o++] = uint8_t(tl); q[o++] = uint8_t(tl >> 8);
  q[o++] = uint8_t(req.n); q[o++] = uint8_t(req.n >> 8); memcpy(q + o, req.b, req.n); o += req.n;
  q[o++] = 0x10; q[o++] = 1; q[o++] = 0; q[o++] = 1;              // report type: large
  const uint16_t txn = uint16_t(gSnsTxn++);
  q[0] = 0; q[1] = uint8_t(txn); q[2] = uint8_t(txn >> 8); q[3] = 0x20; q[4] = 0; q[5] = uint8_t(o - 7); q[6] = uint8_t((o - 7) >> 8);
  qrtrSend(1, gSnsNode, gSnsPort, kSnsClientPort, q, o);
  IOLog("ZIZHAN_SLPI: sns send %s msg_id=%u txn=%u bytes=%u\n", what, msgId, txn, req.n);
}
void snsSuid(const char *type) {
  Pb m = {};
  pbBytes(m, 1, reinterpret_cast<const uint8_t *>(type), uint32_t(strlen(type)));
  pbVarField(m, 2, 0); pbVarField(m, 3, 1);                   // no updates, default sensor only
  snsSend(kSuid, kSuid, 512, m.b, m.n, type);
}
void snsAccelEnable(bool on) {
  if (!on) { snsSend(gAccelLo, gAccelHi, 10, nullptr, 0, "accel-off"); gAccelOn = false; return; }
  Pb m = {};
  pbF32(m, 1, 0x41200000u);   // 10.0f sample rate
  snsSend(gAccelLo, gAccelHi, 513, m.b, m.n, "accel-on");
  gAccelOn = true;
}
void snsEvent(const uint8_t *d, uint32_t n) {
  PbIn in = {d, n, 0};
  uint32_t f, w, len; uint64_t v; const uint8_t *ptr;
  uint64_t lo = 0, hi = 0;
  while (pbNext(in, f, w, v, ptr, len)) {
    if (f == 1 && w == 2) {
      PbIn u = {ptr, len, 0}; uint32_t f2, w2, l2; uint64_t v2; const uint8_t *p2;
      while (pbNext(u, f2, w2, v2, p2, l2)) { if (f2 == 1) lo = v2; if (f2 == 2) hi = v2; }
    } else if (f == 2 && w == 2) {
      PbIn r = {ptr, len, 0}; uint32_t f2, w2, l2; uint64_t v2; const uint8_t *p2;
      uint32_t msgId = 0; const uint8_t *msg = nullptr; uint32_t msgLen = 0;
      while (pbNext(r, f2, w2, v2, p2, l2)) { if (f2 == 1) msgId = uint32_t(v2); if (f2 == 3 && w2 == 2) { msg = p2; msgLen = l2; } }
      if (msgId == 768 && msg) {                       // SUID event: data_type + uids
        PbIn s2 = {msg, msgLen, 0}; uint32_t f3, w3, l3; uint64_t v3; const uint8_t *p3;
        char type[32] = ""; unsigned count = 0; uint64_t flo = 0, fhi = 0;
        while (pbNext(s2, f3, w3, v3, p3, l3)) {
          if (f3 == 1 && w3 == 2) { const uint32_t k = l3 < sizeof type - 1 ? l3 : sizeof type - 1; memcpy(type, p3, k); type[k] = 0; }
          if (f3 == 2 && w3 == 2) {
            PbIn u = {p3, l3, 0}; uint32_t f4, w4, l4; uint64_t v4; const uint8_t *p4; uint64_t a = 0, b = 0;
            while (pbNext(u, f4, w4, v4, p4, l4)) { if (f4 == 1) a = v4; if (f4 == 2) b = v4; }
            if (!count) { flo = a; fhi = b; }
            ++count;
          }
        }
        IOLog("ZIZHAN_SLPI: sns suid '%s' sensors=%u first=%016llx%016llx\n", type, count, fhi, flo);
        if (!strcmp(type, "accel") && count && !gAccelFound) { gAccelFound = true; gAccelLo = flo; gAccelHi = fhi; }
      } else if (msgId == 1025 && msg && lo == gAccelLo && hi == gAccelHi) {
        PbIn s2 = {msg, msgLen, 0}; uint32_t f3, w3, l3; uint64_t v3; const uint8_t *p3;
        unsigned k = 0; int32_t accuracy = -1;
        while (pbNext(s2, f3, w3, v3, p3, l3)) {
          if (f3 == 1 && w3 == 5 && k < 3) gAccel[k++] = uint32_t(v3);
          if (f3 == 1 && w3 == 2) { for (uint32_t i = 0; i + 4 <= l3 && k < 3; i += 4) memcpy(&gAccel[k++], p3 + i, 4); }
          if (f3 == 2 && w3 == 0) accuracy = int32_t(v3);
        }
        if (gAccelSamples++ < 20)
          IOLog("ZIZHAN_SLPI: accel #%u x=%d y=%d z=%d mm/s2 accuracy=%d\n", gAccelSamples, milli(gAccel[0]), milli(gAccel[1]), milli(gAccel[2]), accuracy);
      } else if (gSnsInd < 40) {
        uint32_t code = 0xffffffffu;
        if (msgId == 130 && msg) { PbIn e = {msg, msgLen, 0}; uint32_t f3, w3, l3; uint64_t v3; const uint8_t *p3; while (pbNext(e, f3, w3, v3, p3, l3)) { if (f3 == 1 && w3 == 0) code = uint32_t(v3); } }
        IOLog("ZIZHAN_SLPI: sns event uid=%016llx%016llx msg_id=%u bytes=%u error=%d\n", hi, lo, msgId, msgLen, int(code));
      }
    }
  }
}
void snsData(const uint8_t *q, uint32_t n) {
  if (n < 7) return;
  const uint8_t type = q[0];
  const uint16_t msg = uint16_t(q[3] | (q[4] << 8)), len = uint16_t(q[5] | (q[6] << 8));
  if (7u + len > n) return;
  for (uint32_t at = 7; at + 3 <= 7u + len;) {
    const uint8_t t = q[at];
    const uint16_t l = uint16_t(q[at + 1] | (q[at + 2] << 8));
    if (at + 3 + l > 7u + len) break;
    const uint8_t *v = q + at + 3;
    if (type == 2 && t == 0x02 && l >= 4) {
      uint16_t result, error; memcpy(&result, v, 2); memcpy(&error, v + 2, 2);
      if (result || gSnsInd < 40) IOLog("ZIZHAN_SLPI: sns resp msg=0x%x result=%u error=%u\n", msg, result, error);
    }
    if (type == 4 && (msg == 0x21 || msg == 0x22) && t == 0x02 && l >= 2) {
      uint16_t pl; memcpy(&pl, v, 2);
      if (2u + pl <= l) snsEvent(v + 2, pl);
      ++gSnsInd;
    }
    at += 3 + l;
  }
}
void ipcrtrState(void *, QcomGlinkChannel *, int up) {
  IOLog("ZIZHAN_SLPI: glink IPCRTR %s\n", up ? "up" : "down");
}
void ipcrtrReceive(void *, QcomGlinkChannel *, const uint8_t *d, uint32_t n) {
  if (n < 32) { IOLog("ZIZHAN_SLPI: qrtr short packet %u\n", n); return; }
  uint32_t h[8];
  memcpy(h, d, sizeof h);
  if (h[0] != 1) { IOLog("ZIZHAN_SLPI: qrtr version %u len=%u\n", h[0], n); return; }
  const uint32_t type = h[1];
  if (type == 1) {   // DATA (QMI)
    if (h[7] == kPdmPort && h[5] <= n - 32) { pdmHandle(h[2], h[3], d + 32, h[5]); return; }
    if (h[7] == kSnsClientPort && h[5] <= n - 32) { snsData(d + 32, h[5]); return; }
    if (h[7] == kUimClientPort && h[5] >= 7 && h[5] <= n - 32) {   // UIM Get Card Status: result + first bytes only
      const uint8_t *q = d + 32;
      uint16_t result = 0xffff, error = 0xffff;
      if (h[5] >= 14 && q[7] == 0x02) { memcpy(&result, q + 10, 2); memcpy(&error, q + 12, 2); }
      char hex[3 * 24 + 1] = "";
      unsigned k = 0;
      for (unsigned i = 14; i < h[5] && i < 14 + 24; ++i) k += snprintf(hex + k, sizeof hex - k, "%02x ", q[i]);
      IOLog("ZIZHAN_SLPI: uim card status result=%u error=%u tlv-bytes=[%s]\n", result, error, hex);
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
      IOLog("ZIZHAN_SLPI: dms resp msg=0x%x result=%u error=%u revision='%s' operating_mode=%u\n", msg, result, error, text, mode);
      return;
    }
    if (h[7] == zizhan_rmtfs::kPort && h[5] <= n - 32) {
      static uint8_t reply[128];
      const uint32_t len = zizhan_rmtfs::handle(d + 32, h[5], reply);
      if (len) qrtrSend(1, h[2], h[3], zizhan_rmtfs::kPort, reply, len);
      return;
    }
    if (gDataSeen++ < 8) IOLog("ZIZHAN_SLPI: qrtr data %u:%x -> %u:%x size=%u\n", h[2], h[3], h[6], h[7], h[5]);
    return;
  }
  uint32_t c[5] = {};
  memcpy(c, d + 32, n - 32 < sizeof c ? n - 32 : sizeof c);
  if (type == 4 && c[0] == 4 && c[1] == kSnsService) { gSnsNode = c[3]; gSnsPort = c[4]; gSnsNextMs = 0; }
  if (type == 4 && c[0] == 4 && c[1] == 2 && c[3] == kModemNode) gDmsPort = c[4];
  if (type == 4 && c[0] == 4 && c[1] == 11 && c[3] == kModemNode) gUimPort = c[4];
  if (gCtlLogged++ < 60)
    IOLog("ZIZHAN_SLPI: qrtr ctrl type=%u from %u:%x cmd=%u service=%u instance=%x node=%u port=%x\n", type, h[2], h[3], c[0], c[1],
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
  if ((rc != QcomSmemOk && rc != QcomSmemExists) || !desc || !txf) { IOLog("ZIZHAN_SLPI: glink smem rc=%d\n", rc); return; }
  volatile uint32_t *dw = reinterpret_cast<volatile uint32_t *>(desc);
  const QcomGlinkFifo tx = {&dw[1], &dw[0], txf, tsz}, rx = {&dw[3], &dw[2], nullptr, 0};
  qcom_glink_init(&gGlink, tx, rx, kickGlink, nullptr);
  static const QcomGlinkClient client = {ipcrtrState, ipcrtrReceive};
  gIpcrtr = qcom_glink_channel(&gGlink, "IPCRTR", 0x4400, 4, gIpcrtrStore, &client, nullptr, 1);
  gGlinkOn = gIpcrtr != nullptr;
  IOLog("ZIZHAN_SLPI: glink start desc=%u tx=%u channel=%u\n", dsz, tsz, gGlinkOn ? 1u : 0u);
}
void glinkService() {
  if (!gGlinkOn) return;
  if (!gGlink.rx.data) {
    uint32_t size = 0;
    if (volatile uint8_t *rx = smemItem(480, &size)) qcom_glink_set_rx(&gGlink, rx, size);
  }
  const int r = qcom_glink_poll(&gGlink);
  if (r < 0 && !gGlink.broken) IOLog("ZIZHAN_SLPI: glink poll %d\n", r);
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
    snprintf(path, sizeof path, "image/slpi.b%02u", piece);
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
      qcom_smem_find(&gSmem, &global, kItemCrash, &at, &size) || !size) { IOLog("ZIZHAN_SLPI: crash-reason none\n"); return; }
  char text[120];
  size_t n = 0;
  for (; n < sizeof text - 1 && n < size; ++n) { const char c = char(gSmemW.va[at + n]); if (!c) break; text[n] = (c >= 32 && c < 127) ? c : '.'; }
  text[n] = 0;
  IOLog("ZIZHAN_SLPI: crash-reason '%s'\n", text);
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
  const char *cmd = on ? "{class: image, res: load_state, name: slpi, val: on}" : "{class: image, res: load_state, name: slpi, val: off}";
  if (gQmp.state != QcomQmpOpen) return false;
  if (qcom_qmp_send(&gQmp, cmd) != QcomQmpSending) return false;
  for (unsigned i = 0; i < 1000; ++i) { if (qcom_qmp_step(&gQmp) == QcomQmpOpen) return true; IOSleep(1); }
  qcom_qmp_abandon(&gQmp);
  return false;
}

void worker(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_slpi");
  for (unsigned i = 0; i < 1200 && !pong_ufs_ready(); ++i) IOSleep(100);
  // modem-6: loading 108 MB while the first desktop frame releases the boot console (a one-time full-panel clear with
  // preemption off) pushed that clear past the 6 ms preemption limit and panicked. Start only after the first frame.
  for (unsigned i = 0; i < 1200 && !(&pong_zc_flipped_tag && __atomic_load_n(&pong_zc_flipped_tag, __ATOMIC_ACQUIRE)); ++i) IOSleep(100);
  IOLog("ZIZHAN_SLPI: desktop frame seen=%u, starting in 10 s\n", (&pong_zc_flipped_tag && pong_zc_flipped_tag) ? 1u : 0u);
  IOSleep(10000);
  uint64_t t0 = nowMs(), nextReport = 0;
  uint32_t watchS = 60;
  (void)PE_parse_boot_argn("zizhan_slpi_watch_s", &watchS, sizeof watchS);
  if (watchS < 30 || watchS > 1800) watchS = 60;
  // zizhan_modem_cycles=2|3: after a clean stop, boot the modem again in the same session (restart/cleanup check). Later
  // cycles only wait for ready and stop again: GLINK/QRTR stay on the first cycle's state, so no QMI there.
  // modem-18: the second load reset the whole SoC (no log survived), so leave this at 1 outside a deliberate test.
  uint32_t cycles = 1, cycle = 0;
  (void)PE_parse_boot_argn("zizhan_slpi_cycles", &cycles, sizeof cycles);
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
    STEP(5, !(rc = zizhan_fat::open_path(&gVol, "image/slpi.mdt", &mdt)) && mdt.size <= sizeof gHeaders);
    STEP(6, !(rc = zizhan_fat::file_read(&gVol, &mdt, 0, gHeaders, mdt.size)));
    STEP(7, !(rc = qcom_mdt_split(&gSplit, gHeaders, mdt.size)));
  }
  STEP(8, !(rc = qcom_mdt_parse(&gMdt, gSplit.size, joinedRead, nullptr)));
  IOLog("ZIZHAN_SLPI: firmware joined=%llu segments=%u span=%llu relocatable=%u\n", gSplit.size, gMdt.count, qcom_mdt_span(&gMdt), gMdt.relocatable ? 1u : 0u);
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
  STEP(17, !qcom_cmd_db_find(gDb, sizeof gDb, "lcx.lvl", &cx) && !qcom_cmd_db_find(gDb, sizeof gDb, "lmx.lvl", &mss) &&
           !qcom_rpmh_level_index(&cx, 384, &cxIdx) && !qcom_rpmh_level_index(&mss, 384, &mssIdx));
  STEP(18, pong_rpmh_vote(cx.address, cxIdx) && pong_rpmh_vote(mss.address, mssIdx));
  IOLog("ZIZHAN_SLPI: rails cx=%u mss=%u (NOM) load_state=on smp2p_out=ok\n", cxIdx, mssIdx);
again:
  IOLog("ZIZHAN_SLPI: cycle %u/%u begin\n", cycle + 1, cycles);
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
      IOLog("ZIZHAN_SLPI: pas_supported_mask=%llx error_mask=%llx\n", yes, err);
    }
    const ScmResult pre = scm(QCOM_SCM_PAS_SHUTDOWN, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    IOLog("ZIZHAN_SLPI: is_supported x0=%llx x1=%llx pre_shutdown x0=%llx x1=%llx meta_phys=%llx\n", uint64_t(sup.x0), sup.x1,
          uint64_t(pre.x0), pre.x1, phys);
    const ScmResult r = scm(QCOM_SCM_PAS_INIT_IMAGE, qcom_scm_args(2, QCOM_SCM_VAL, QCOM_SCM_RW, 0), kPas, phys, 0);
    IOLog("ZIZHAN_SLPI: init_image x0=%llx x1=%llx meta=%llu\n", uint64_t(r.x0), r.x1, bytes);
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
    IOLog("ZIZHAN_SLPI: segments loaded=%llu rc=%d ms=%llu\n", loaded, rc, nowMs() - loadStart);
    STEP(25, rc == 0);
  }
  {
    const ScmResult r = scm(QCOM_SCM_PAS_AUTH_AND_RESET, qcom_scm_args(1, 0, 0, 0), kPas, 0, 0);
    IOLog("ZIZHAN_SLPI: auth_and_reset x0=%lld x1=%llu\n", r.x0, r.x1);
    STEP(26, scmOk(r));
  }
  t0 = nowMs();
  nextReport = t0 + 10000;
  for (uint64_t now = t0; now - t0 < uint64_t(cycle ? 30 : watchS) * 1000; now = nowMs()) {   // up to 10 s for ready, then watch
    if (kModemQueries && !cycle && gDmsPort && !gDmsAsked && gIpcrtr && gIpcrtr->up && now - t0 > 20000) {
      gDmsAsked = true;
      static const uint8_t rev[7] = {0, 1, 0, 0x23, 0, 0, 0}, opmode[7] = {0, 2, 0, 0x2d, 0, 0, 0};
      qrtrSend(1, kModemNode, gDmsPort, kDmsClientPort, rev, sizeof rev);
      qrtrSend(1, kModemNode, gDmsPort, kDmsClientPort, opmode, sizeof opmode);
      IOLog("ZIZHAN_SLPI: dms requests sent to 0:%x (get revision, get operating mode)\n", gDmsPort);
      if (gUimPort) {
        static const uint8_t card[7] = {0, 3, 0, 0x2f, 0, 0, 0};
        qrtrSend(1, kModemNode, gUimPort, kUimClientPort, card, sizeof card);
        IOLog("ZIZHAN_SLPI: uim get card status sent to 0:%x\n", gUimPort);
      }
    }
    if (now >= nextReport) {
      nextReport += 10000;
      IOLog("ZIZHAN_SLPI: glink version=%u rx=%u tx=%u unknown_opens=%u '%s' ipcrtr_up=%u ctrl=%u data=%u pdm=%u rmtfs req=%u rd=%u/%llu wr=%u/%llu fail=%u\n", gGlink.version_agreed,
            gGlink.rx_messages, gGlink.tx_messages, gGlink.unknown_opens, gGlink.unknown_names, (gIpcrtr && gIpcrtr->up) ? 1u : 0u,
            gCtlLogged, gDataSeen, gPdmRequests, zizhan_rmtfs::gReqs, zizhan_rmtfs::gReads, zizhan_rmtfs::gReadBytes,
            zizhan_rmtfs::gWrites, zizhan_rmtfs::gWriteBytes, zizhan_rmtfs::gFailures);
      uint32_t d = 0, tx = 0, rx = 0;
      const bool hd = smemItem(478, &d) != nullptr, ht = smemItem(479, &tx) != nullptr, hr = smemItem(480, &rx) != nullptr;
      IOLog("ZIZHAN_SLPI: t=%llums bits=%x glink_desc=%u/%u fifo_tx=%u/%u fifo_rx=%u/%u\n", now - t0, lastBits, hd ? 1u : 0u, d,
            ht ? 1u : 0u, tx, hr ? 1u : 0u, rx);
    }
    if (slaveBits(&bits) && bits != lastBits) {
      IOLog("ZIZHAN_SLPI: t=%llums slave-kernel=%x ready=%u fatal=%u handover=%u\n", now - t0, bits, (bits & kReady) ? 1u : 0u,
            (bits & kFatal) ? 1u : 0u, (bits & kHandover) ? 1u : 0u);
      lastBits = bits;
      if (bits & kFatal) { readCrash(); break; }
      if (bits & kReady) readySeen = true;
      if (!cycle && (bits & kReady) && !gGlinkOn) glinkStart();
    }
    if (!cycle) glinkService();
    if (gSlpiMode == 2 && !cycle && gSnsNode != 0xffffffffu && gIpcrtr && gIpcrtr->up && now >= gSnsNextMs) {
      switch (gSnsStage) {   // one step per slot: registry, accel, ambient_light lookups, then the accelerometer stream
      case 0: gSnsNextMs = now + 3000; break;
      case 1: snsSuid("registry"); gSnsNextMs = now + 1500; break;
      case 2: snsSuid("accel"); gSnsNextMs = now + 1500; break;
      case 3: snsSuid("ambient_light"); gSnsNextMs = now + 2000; break;
      case 4:
        if (gAccelFound) { snsAccelEnable(true); gSnsNextMs = now + 10000; break; }
        IOLog("ZIZHAN_SLPI: sns no accel sensor yet (t=%llus), retry in 15 s\n", (now - t0) / 1000);
        gSnsStage = 0xffffffffu; gSnsNextMs = now + 15000;   // ++ below wraps it to 0
        break;
      default:
        gSnsNextMs = now + 10000;
        if (gAccelOn) IOLog("ZIZHAN_SLPI: accel samples=%u last x=%d y=%d z=%d mm/s2\n", gAccelSamples, milli(gAccel[0]), milli(gAccel[1]), milli(gAccel[2]));
      }
      if (gSnsStage < 5 || gSnsStage == 0xffffffffu) ++gSnsStage;
    }
    if (now - t0 > 10000 && !(lastBits != 0xffffffffu && (lastBits & kReady))) break;
    IOSleep(10);
  }
  stage = 27;
out:
  IOLog("ZIZHAN_SLPI: stage=%d rc=%d bits=%x pas_started=%u\n", stage, rc, lastBits == 0xffffffffu ? 0u : lastBits, pasStarted ? 1u : 0u);
  if (gAccelOn) {   // stop the stream before the DSP goes away
    snsAccelEnable(false);
    for (uint64_t s0 = nowMs(); nowMs() - s0 < 500;) { glinkService(); IOSleep(10); }
  }
  if (pasStarted) {
    if (stopEntry >= 0) {
      qcom_smp2p_update(&gP2p, stopEntry, 1, 1);
      ring(kIpccMpss, QCOM_IPCC_SIGNAL_SMP2P);
      const uint64_t s0 = nowMs();
      while (nowMs() - s0 < 5000 && !(slaveBits(&bits) && (bits & kStopAck))) IOSleep(10);
      IOLog("ZIZHAN_SLPI: stop-ack=%u after %llums\n", (bits & kStopAck) ? 1u : 0u, nowMs() - s0);
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
    IOLog("ZIZHAN_SLPI: pas_shutdown x0=%lld x1=%llu tries=%u first=%llx\n", r.x0, r.x1, tries, uint64_t(firstErr));
    if (stopEntry >= 0) qcom_smp2p_update(&gP2p, stopEntry, 1, 0);
    IOLog("ZIZHAN_SLPI: cycle %u/%u end ready=%u stop-ack=%u shutdown_ok=%u\n", cycle + 1, cycles, readySeen ? 1u : 0u,
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
  IOLog("ZIZHAN_SLPI: DONE radio=0 qmi=0 (rails stay voted until reboot)\n");
#undef STEP
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}
}  // namespace

extern "C" void zizhan_slpi_start(void) {
  uint32_t mode = 0, adsp = 0, modem = 0;
  if (!PE_parse_boot_argn("zizhan_slpi", &mode, sizeof mode) || mode < 1 || mode > 2) return;
  gSlpiMode = mode;
  if (PE_parse_boot_argn("pong_adsp", &adsp, sizeof adsp) && adsp) { IOLog("ZIZHAN_SLPI: SKIP needs pong_adsp=0 (AOSS/SMEM owner)\n"); return; }
  if (PE_parse_boot_argn("zizhan_modem", &modem, sizeof modem) && modem == 2) { IOLog("ZIZHAN_SLPI: SKIP zizhan_modem=2 owns the same resources\n"); return; }
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) != KERN_SUCCESS) { IOLog("ZIZHAN_SLPI: FAIL worker\n"); return; }
  thread_deallocate(t);
  IOLog("ZIZHAN_SLPI: RUNNING pas=%u region=%llx+%llx\n", kPas, kMpssBase, kMpssSize);
}
