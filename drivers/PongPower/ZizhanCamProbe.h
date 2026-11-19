/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CAM-01 kernel probe, default off. zizhan_cam_probe=1: read-only inventory (GCC wrap0 S5, TLMM 11/53/103/128/206/207,
 * CAM_CC titan GDSC / MCLK3 / CCI1). zizhan_cam_probe=2 (ATTENDED ONLY, first power-up of the front IMX596):
 * PM8008J over QUP wrap0 S5 I2C (GPIO206/207, 0x0d), L5 1.8 V -> GPIO53 vana -> L1 1.056 V, MCLK3 19.2 MHz on GPIO103,
 * reset GPIO128 release, CCI1 master 0 read of 0x0016 at 7-bit 0x1a then 0x10, then everything off in reverse.
 * Logic lives in ZizhanPm8008.h / ZizhanCamCc.h / ZizhanCci.h (host tested). Not run unattended. */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "GeniI2c.h"
#include "ZizhanPm8008.h"
#include "ZizhanCci.h"

namespace zizhan_cam_probe {
struct Win {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool open(uint64_t base, uint64_t bytes, bool write) {
    auto m = IODeviceMemory::withRange(base, bytes);
    map = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache | (write ? 0 : kIOMapReadOnly)) : nullptr;
    if (m) m->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  uint32_t r(uint32_t at) const { const uint32_t v = *reinterpret_cast<const volatile uint32_t *>(va + at); __asm__ volatile("dmb oshld" ::: "memory"); return v; }
  void w(uint32_t at, uint32_t v) { __asm__ volatile("dmb oshst" ::: "memory"); *reinterpret_cast<volatile uint32_t *>(va + at) = v; __asm__ volatile("dsb sy" ::: "memory"); }
  ~Win() { if (map) map->release(); }
};
static uint32_t win_read(void *c, uint32_t off) { return static_cast<Win *>(c)->r(off); }
static void win_write(void *c, uint32_t off, uint32_t v) { static_cast<Win *>(c)->w(off, v); }
static void pause_us(void *, unsigned us) { IODelay(us); }

static const uint16_t kPins[] = {11, 53, 103, 128, 206, 207};

static void inventory() {
  Win vote, wrap, camcc;
  if (!vote.open(0x162000, 0x1000, false) || !wrap.open(0x127000, 0x1000, false) || !camcc.open(0xade0000, 0x20000, false)) {
    IOLog("ZIZHAN_CAM_PROBE: FAIL map\n");
    return;
  }
  IOLog("ZIZHAN_CAM_PROBE: gcc vote8=%x s5=%x m_ahb=%x s_ahb=%x rcg cmd=%x cfg=%x dfsr=%x\n", vote.r(0x8), wrap.r(0x610),
        wrap.r(0x004), wrap.r(0x008), wrap.r(0x618), wrap.r(0x61c), wrap.r(0x62c));
  for (uint16_t pin : kPins) {
    Win p;
    if (!p.open(0xf100000ull + uint64_t(pin) * 0x1000, 0x1000, false)) continue;
    const uint32_t cfg = p.r(0), io = p.r(4);
    IOLog("ZIZHAN_CAM_PROBE: pin=%u cfg=%x func=%u oe=%u pull=%u egpio=%u/%u in=%u out=%u\n", pin, cfg, (cfg >> 2) & 15,
          (cfg >> 9) & 1, cfg & 3, (cfg >> 11) & 1, (cfg >> 12) & 1, io & 1, (io >> 1) & 1);
  }
  IOLog("ZIZHAN_CAM_PROBE: camcc titan_gdscr=%x mclk3_rcg=%x/%x mclk3_cbcr=%x cci1_rcg=%x cci1_cbcr=%x cpas_ahb=%x\n",
        camcc.r(0x131dc), camcc.r(0x15054), camcc.r(0x15058), camcc.r(0x1506c), camcc.r(0x13148), camcc.r(0x13160), camcc.r(0x13164));
  IOLog("ZIZHAN_CAM_PROBE: DONE inventory writes=0\n");
}

/* --- attended mode 2 --- */
struct I2cCtx { GeniI2c g; Win *se; };
static int i2c_wait(GeniI2c *g) {
  for (unsigned i = 0; i < 2000; ++i) {
    const int st = geni_i2c_poll(g);
    if (st == GeniDone) return 0;
    if (st == GeniFailed) return -1;
    IODelay(50);
  }
  for (unsigned k = 0; k < 3; ++k) { const int st = geni_i2c_timeout(g); if (st != GeniPending) return -1; IODelay(1000); }
  return -1;
}
static int pm_read(void *c, uint16_t reg, uint8_t *v) {
  auto *x = static_cast<I2cCtx *>(c);
  const uint8_t a[2] = {uint8_t(reg >> 8), uint8_t(reg)};
  if (geni_i2c_start_read_bytes(&x->g, a, 2, 1) || i2c_wait(&x->g)) return 1;
  *v = uint8_t(x->g.rx);
  return 0;
}
static int pm_write(void *c, uint16_t reg, uint8_t v) {
  auto *x = static_cast<I2cCtx *>(c);
  const uint8_t a[3] = {uint8_t(reg >> 8), uint8_t(reg), v};
  return (geni_i2c_start_write_bytes(&x->g, a, 3) || i2c_wait(&x->g)) ? 1 : 0;
}
static uint32_t se_read(void *c, uint32_t reg) { return static_cast<Win *>(c)->r(reg); }
static void se_write(void *c, uint32_t reg, uint32_t v) { static_cast<Win *>(c)->w(reg, v); }

static void gpio_out(Win &p, int high) { p.w(0, (p.r(0) & ~0x3ffu) | (1u << 9)); p.w(4, high ? 2u : 0u); }

static void attended() {
  Win vote, wrap, se, camcc, cci, pin[6];
  uint32_t saved[6] = {};
  if (!vote.open(0x162000, 0x1000, true) || !wrap.open(0x127000, 0x1000, true) || !se.open(0x994000, 0x4000, true) ||
      !camcc.open(0xade0000, 0x20000, true) || !cci.open(0xac16000, 0x1000, true)) { IOLog("ZIZHAN_CAM_PROBE: FAIL map\n"); return; }
  for (unsigned i = 0; i < 6; ++i) {
    if (!pin[i].open(0xf100000ull + uint64_t(kPins[i]) * 0x1000, 0x1000, true)) { IOLog("ZIZHAN_CAM_PROBE: FAIL pin-map\n"); return; }
    saved[i] = pin[i].r(0);
  }
  // I2C pins 206/207: eGPIO must be enabled for APPS (bit 12) when present (bit 11), as the SE15 rule.
  for (unsigned i = 4; i < 6; ++i)
    if ((saved[i] & (1u << 11)) && !(saved[i] & (1u << 12))) { IOLog("ZIZHAN_CAM_PROBE: SKIP i2c-pin-not-apps pin=%u cfg=%x\n", kPins[i], saved[i]); return; }
  // wrap0 clocks (add-only votes), S5 RCG at XO when DFS is off.
  vote.w(0x8, vote.r(0x8) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 15));
  if (!(wrap.r(0x62c) & 1u)) { wrap.w(0x61c, (wrap.r(0x61c) & ~0x1037ffu) | 1u); wrap.w(0x618, wrap.r(0x618) | 1u); }
  bool on = false;
  for (unsigned i = 0; i < 1000 && !on; ++i) {
    const uint32_t m = wrap.r(0x004), s = wrap.r(0x008);
    on = !(wrap.r(0x610) & 0x80000000u) && (!(m & 0x80000000u) || (m & 2u)) && (!(s & 0x80000000u) || (s & 2u));
    if (!on) IODelay(100);
  }
  if (!on) { IOLog("ZIZHAN_CAM_PROBE: FAIL wrap0-s5-clock\n"); return; }
  for (unsigned i = 4; i < 6; ++i) pin[i].w(0, (saved[i] & ~0x3ffu) | (1u << 2) | 3u);   // qup5, pull-up
  // 100 kHz from 19.2 MHz (Linux geni i2c: div 7, t_high 10, t_low 11, t_cycle 26) if the boot loader left it unset.
  if (!(se.r(0x48) & 1u) || !se.r(0x278)) { se.w(0x48, (7u << 4) | 1u); se.w(0x278, (10u << 20) | (11u << 10) | 26u); }
  I2cCtx x{}; x.se = &se;
  const GeniIo io = {se_read, se_write, &se};
  uint32_t config = 0;
  if (geni_i2c_prepare_poll(&x.g, &io, 0x0d, &config)) { IOLog("ZIZHAN_CAM_PROBE: FAIL i2c-prepare config=%x\n", config); goto pins; }
  {
    gpio_out(pin[0], 1);   // PM8008J enable (active state output-high)
    gpio_out(pin[3], 0);   // sensor reset low
    IOSleep(5);
    const ZizhanPm8008Ops pm = {&x, pm_read, pm_write};
    unsigned mv = 0; int en = 0;
    int rc = zizhan_pm8008_read_ldo(&pm, 1, &mv, &en);
    IOLog("ZIZHAN_CAM_PROBE: pm8008j l1 rc=%d mv=%u en=%d\n", rc, mv, en);
    if (rc) goto pmoff;
    {
      unsigned done = 0;
      rc = zizhan_pm8008_rails_on(&pm, &zizhan_pm8008_front_rails[0], 1, &done);   // vio L5 1.8 V
      if (!rc) { gpio_out(pin[1], 1); IOSleep(1); }                                  // vana (GPIO53 regulator)
      if (!rc) rc = zizhan_pm8008_rails_on(&pm, &zizhan_pm8008_front_rails[1], 1, &done);   // vdig L1 1.056 V
      IOLog("ZIZHAN_CAM_PROBE: rails rc=%d\n", rc);
      ZizhanCamCcOps cc = {&camcc, win_read, win_write, pause_us, 1000};
      unsigned stage = 0;
      int crc = rc ? -1 : zizhan_camcc_mclk3_on(&cc, &stage);
      if (!crc) { pin[2].w(0, (saved[2] & ~0x3ffu) | (1u << 2)); IOSleep(1); gpio_out(pin[3], 1); IOSleep(10); }
      int ccirc = -1;
      if (!crc) ccirc = zizhan_cci_clocks_on(&cc);
      if (!crc && !ccirc) {
        ZizhanCciOps co = {&cci, win_read, win_write, pause_us, 2000};
        ccirc = zizhan_cci_reset_init(&co);
        static const uint8_t addrs[] = {0x1a, 0x10};
        for (uint8_t a : addrs) {
          uint8_t id[2] = {0, 0};
          const int r = ccirc ? ccirc : zizhan_cci_read16(&co, a, 0x0016, id, 2);
          IOLog("ZIZHAN_CAM_PROBE: cci addr=%x rc=%d chip_id=%02x%02x\n", a, r, id[0], id[1]);
          if (!r) break;
        }
      }
      IOLog("ZIZHAN_CAM_PROBE: mclk rc=%d stage=%u cci_clocks=%d\n", crc, stage, ccirc);
      gpio_out(pin[3], 0);
      pin[2].w(0, saved[2]);
      if (!crc) (void)zizhan_camcc_mclk3_off(&cc);
      (void)zizhan_pm8008_rails_off(&pm, zizhan_pm8008_front_rails, 2);
      gpio_out(pin[1], 0);
    }
  pmoff:
    pin[0].w(0, saved[0]);
    pin[1].w(0, saved[1]);
    pin[3].w(0, saved[3]);
  }
pins:
  for (unsigned i = 4; i < 6; ++i) pin[i].w(0, saved[i]);
  IOLog("ZIZHAN_CAM_PROBE: DONE attended rails_off=1 pins_restored=1\n");
}

static void worker(void *arg, wait_result_t) {
  const uint32_t mode = uint32_t(uintptr_t(arg));
  IOSleep(40000);   // after the desktop and Wi-Fi
  if (mode == 1) inventory(); else attended();
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start() {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_cam_probe", &mode, sizeof mode) || !mode) return;
  uint32_t present = 0;
  if (mode == 2 && (!PE_parse_boot_argn("zizhan_user_present", &present, sizeof present) || present != 1)) {
    IOLog("ZIZHAN_CAM_PROBE: SKIP mode=2 needs zizhan_user_present=1\n");
    return;
  }
  if (mode > 2) { IOLog("ZIZHAN_CAM_PROBE: SKIP unsupported-mode=%u\n", mode); return; }
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, reinterpret_cast<void *>(uintptr_t(mode)), &t) == KERN_SUCCESS) thread_deallocate(t);
  IOLog("ZIZHAN_CAM_PROBE: RUNNING mode=%u\n", mode);
}
} // namespace zizhan_cam_probe
