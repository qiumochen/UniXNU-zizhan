/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* POWER-05 (docs/progress/2026-11-20-round3.md), both default off.
 *
 * zizhan_rpmh_dump=1: read-only dump of the APSS RSC DRV2 TCS command slots (SLEEP 0..2, ACTIVE 3..4, WAKE 5..6 on SM8475, see
 * PongRpmh.cpp) with each address named from the command database, once at start and at every standby transition. The SLEEP/WAKE
 * slots keep what the boot firmware programmed; the ACTIVE slots show only the latest request.
 *
 * zizhan_standby_cx=<RPMh level> (for example 256 = NOM): the Wi-Fi bring-up (PongWifiPlatform.cpp supplies()) keeps this DRV's
 * cx.lvl and mx.lvl votes at their highest level so that no inherited consumer is starved. While the display has been off for 3 s,
 * lower CX and then MX to the first level >= the request; when it comes back on, raise MX and then CX to the highest level again.
 * The GPU votes through its own GMU DRV, the ADSP votes lcx/lmx and the display mmcx, so none of them is changed here. Every
 * transition is logged with its result.
 *
 * zizhan_standby_bw=<bits> (experiment): while the display is off, bit0 lowers the boot firmware's MM0 and SN2 bus votes on this
 * DRV (read back from TCS0 at start) to the smallest non-zero vote and restores those exact values when the display comes back;
 * bit1 lowers MC0 and SH0 (DDR) the same way and votes them to the maximum when it comes back (their earlier value on this DRV
 * is not known). A non-zero vote keeps every NoC clocked; nothing is ever voted to zero. bit2 alternates within one display-off
 * period, 120 s each: unchanged (control), lowered, unchanged, ... so one boot carries its own control phases (zlog.py prints
 * system power per phase). */
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <kern/clock.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include <string.h>
#include "QcomRpmh.h"

extern "C" bool pong_rpmh_vote(uint32_t address, uint32_t value);
extern "C" int zizhan_display_power_state(void);

namespace {
constexpr uint64_t kCmdDb = 0x80860000, kCmdDbSize = 0x20000, kRsc = 0x17a20000, kRscSize = 0x10000;
constexpr unsigned kTcsBase = 0xd00, kTcsStride = 0x2a0, kCmdStride = 20, kTcsCount = 7;
uint8_t *gDb;
QcomRpmhResource gCx, gMx;
uint32_t gTarget, gDump, gBw;
struct Bus { const char *name; uint32_t addr, boot; bool have; };
Bus gBus[4] = {{"MM0", 0, 0, false}, {"SN2", 0, 0, false}, {"MC0", 0, 0, false}, {"SH0", 0, 0, false}};
constexpr uint32_t kBcmMin = (1u << 30) | (1u << 29) | (1u << 14) | 1u, kBcmMax = (1u << 30) | (1u << 29) | (0x3fffu << 14) | 0x3fffu;

uint16_t u16(const uint8_t *v) { return uint16_t(v[0] | (v[1] << 8)); }
uint32_t u32(const uint8_t *v) { return uint32_t(v[0]) | (uint32_t(v[1]) << 8) | (uint32_t(v[2]) << 16) | (uint32_t(v[3]) << 24); }

bool readDb() {
  gDb = static_cast<uint8_t *>(IOMallocData(kCmdDbSize));
  auto memory = gDb ? IODeviceMemory::withRange(kCmdDb, kCmdDbSize) : nullptr;
  auto map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
  if (memory) { memory->release(); }
  if (!map) { return false; }
  auto words = reinterpret_cast<volatile uint32_t *>(map->getVirtualAddress());
  for (size_t i = 0; i < kCmdDbSize / 4; ++i) {
    const uint32_t w = words[i];
    memcpy(gDb + 4 * i, &w, 4);
  }
  map->release();
  return true;   // qcom_cmd_db_find checks the header (magic at offset 4, after the version)
}

// The command database entry whose address is addr (the same header/entry layout PongWifiPlatform.cpp bcmVote parses).
void nameOf(uint32_t addr, char out[9]) {
  memcpy(out, "?", 2);
  for (unsigned slave = 0; slave < 8; ++slave) {
    const uint8_t *h = gDb + 8 + slave * 16;
    if (!u16(h)) { break; }
    const size_t heads = 144 + u16(h + 2), count = u16(h + 6);
    if (heads > kCmdDbSize || count > (kCmdDbSize - heads) / 24) { return; }
    for (size_t i = 0; i < count; ++i) {
      const uint8_t *e = gDb + heads + 24 * i;
      if (u32(e + 16) == addr) { memcpy(out, e, 8); out[8] = 0; return; }
    }
  }
}

void dump(const char *when) {
  auto memory = IODeviceMemory::withRange(kRsc, kRscSize);
  auto map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
  if (memory) { memory->release(); }
  if (!map) { IOLog("ZIZHAN_RPMH_DUMP: %s FAIL map\n", when); return; }
  auto v = reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());
  auto r = [&](unsigned o) { const uint32_t x = *reinterpret_cast<volatile uint32_t *>(v + o); __asm__ volatile("dmb oshld" ::: "memory"); return x; };
  const unsigned caps = r(0xc), ncpt = (caps >> 27) & 31;
  for (unsigned t = 0; t < kTcsCount; ++t) {
    const unsigned b = kTcsBase + t * kTcsStride, enable = r(b + 0x1c);
    for (unsigned i = 0; i < ncpt && i < 16; ++i) {
      const uint32_t addr = r(b + 0x34 + kCmdStride * i), data = r(b + 0x38 + kCmdStride * i);
      if (!addr && !data) { continue; }
      char name[9];
      nameOf(addr, name);
      IOLog("ZIZHAN_RPMH_DUMP: %s tcs %u cmd %u en %u addr %05x %s data %08x\n", when, t, i, (enable >> i) & 1, addr, name, data);
    }
  }
  map->release();
}

uint32_t addrOf(const char *want) {
  for (unsigned slave = 0; slave < 8; ++slave) {
    const uint8_t *h = gDb + 8 + slave * 16;
    if (!u16(h)) { break; }
    const size_t heads = 144 + u16(h + 2), count = u16(h + 6);
    if (heads > kCmdDbSize || count > (kCmdDbSize - heads) / 24) { return 0; }
    for (size_t i = 0; i < count; ++i) {
      const uint8_t *e = gDb + heads + 24 * i;
      if (!strncmp(reinterpret_cast<const char *>(e), want, 8)) { return u32(e + 16); }
    }
  }
  return 0;
}

// The last value each bus resource got in TCS0's retained batch (commands run in order, so a later one wins).
void learnBus() {
  auto memory = IODeviceMemory::withRange(kRsc, kRscSize);
  auto map = memory ? memory->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
  if (memory) { memory->release(); }
  if (!map) { return; }
  auto v = reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress());
  auto r = [&](unsigned o) { const uint32_t x = *reinterpret_cast<volatile uint32_t *>(v + o); __asm__ volatile("dmb oshld" ::: "memory"); return x; };
  for (auto &b : gBus) { b.addr = addrOf(b.name); }
  for (unsigned i = 0; i < 16; ++i) {
    const uint32_t addr = r(kTcsBase + 0x34 + kCmdStride * i), data = r(kTcsBase + 0x38 + kCmdStride * i);
    for (auto &b : gBus) {
      if (b.addr && addr == b.addr) { b.boot = data; b.have = true; }
    }
  }
  map->release();
  for (auto &b : gBus) { IOLog("ZIZHAN_STANDBY_BW: %s addr %05x boot %s %08x\n", b.name, b.addr, b.have ? "value" : "unknown", b.boot); }
}

void applyBus(bool low) {
  char res[160];
  unsigned k = 0;
  for (unsigned i = 0; i < 4; ++i) {
    Bus &b = gBus[i];
    const bool ddr = i >= 2;
    if (!(gBw & (ddr ? 2u : 1u)) || !b.addr) { continue; }
    if (!ddr && !b.have) { continue; }   // only restore what is known exactly
    const uint32_t value = low ? kBcmMin : (ddr ? kBcmMax : b.boot);
    const bool ok = pong_rpmh_vote(b.addr, value);
    k += snprintf(res + k, sizeof res - k, " %s=%08x:%d", b.name, value, ok ? 1 : 0);
    if (k >= sizeof res) { break; }
  }
  IOLog("ZIZHAN_STANDBY_BW: display %s ->%s\n", low ? "off" : "on", k ? res : " nothing");
}

uint64_t us() { uint64_t ns = 0; absolutetime_to_nanoseconds(mach_absolute_time(), &ns); return ns / 1000; }

void apply(bool low) {
  uint32_t cx = 0, mx = 0;
  const uint32_t level = low ? gTarget : 0xffffffffu;
  const bool found = !qcom_rpmh_level_index(&gCx, level, &cx) && !qcom_rpmh_level_index(&gMx, level, &mx);
  const uint64_t t0 = us();
  bool ok = found;
  if (ok && low) { ok = pong_rpmh_vote(gCx.address, cx) && pong_rpmh_vote(gMx.address, mx); }
  if (ok && !low) { ok = pong_rpmh_vote(gMx.address, mx) && pong_rpmh_vote(gCx.address, cx); }
  IOLog("ZIZHAN_STANDBY_RPMH: display %s -> cx idx %u level %u, mx idx %u level %u, ok=%d in %llu us\n", low ? "off" : "on",
        cx, unsigned(gCx.levels[cx]), mx, unsigned(gMx.levels[mx]), ok ? 1 : 0, (unsigned long long)(us() - t0));
  if (gDump) { dump(low ? "standby" : "resume"); }
}

void worker(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_standby");
  IOSleep(30000);   // the device log (a 2 MiB console ring) loses early boot; start and report once boot is over
  if (!readDb() || qcom_cmd_db_find(gDb, kCmdDbSize, "cx.lvl", &gCx) || qcom_cmd_db_find(gDb, kCmdDbSize, "mx.lvl", &gMx) ||
      !gCx.level_count || !gMx.level_count) {
    IOLog("ZIZHAN_STANDBY_RPMH: FAIL command database\n");
    return;
  }
  char cx[96] = "", mx[96] = "";
  for (unsigned i = 0, k = 0; i < gCx.level_count; ++i) { k += snprintf(cx + k, sizeof cx - k, " %u", unsigned(gCx.levels[i])); }
  for (unsigned i = 0, k = 0; i < gMx.level_count; ++i) { k += snprintf(mx + k, sizeof mx - k, " %u", unsigned(gMx.levels[i])); }
  IOLog("ZIZHAN_STANDBY_RPMH: RUNNING target=%u cx %05x levels%s; mx %05x levels%s\n", gTarget, gCx.address, cx, gMx.address, mx);
  if (gBw) { learnBus(); }
  if (gDump) { dump("boot"); }   // nothing in this kernel writes the SLEEP/WAKE slots, so they still hold the firmware's sets
  unsigned offFor = 0;
  bool low = false, busLow = false;
  unsigned phase = 0, window = 0;
  for (;;) {
    IOSleep(500);
    if (zizhan_display_power_state() == 0) {
      if (++offFor == 6 && !low && (gTarget || gBw)) {   // off for 3 s (CPU offline acts at 2 s)
        busLow = gBw && !(gBw & 4);
        phase = 0;
        if (gBw & 4) { IOLog("ZIZHAN_STANDBY_BW: window %u bus unchanged\n", ++window); }
        if (busLow) { applyBus(true); }
        if (gTarget) { apply(true); }
        low = true;
      } else if (low && (gBw & 4) && ++phase == 240) {   // 120 s per phase
        phase = 0;
        busLow = !busLow;
        IOLog("ZIZHAN_STANDBY_BW: window %u bus %s\n", ++window, busLow ? "lowered" : "unchanged");
        applyBus(busLow);
      }
    } else {
      offFor = 0;
      if (low) {
        if (gTarget) { apply(false); }
        if (busLow) { applyBus(false); }
        busLow = false;
        low = false;
      }
    }
  }
}
}  // namespace

extern "C" void zizhan_standby_rpmh_start(void) {
  PE_parse_boot_argn("zizhan_standby_cx", &gTarget, sizeof gTarget);
  PE_parse_boot_argn("zizhan_rpmh_dump", &gDump, sizeof gDump);
  PE_parse_boot_argn("zizhan_standby_bw", &gBw, sizeof gBw);
  if (!gTarget && !gDump && !gBw) { return; }
  thread_t th = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &th) == KERN_SUCCESS) { thread_deallocate(th); }
}
