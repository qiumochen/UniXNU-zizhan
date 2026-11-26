/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* FOLD-01 outer panel step 1 (zizhan_outer_probe=1, default off): read-only inventory of what the boot loader left for
 * the L18 secondary panel. DISP_CC 0xaf00000 (Linux dispcc-sm8450): MDSS GDSC 0x9000, byte0/pclk0 0x8028/0x8004,
 * byte1/byte1_intf/esc1/pclk1 0x8030/0x8034/0x803c/0x8008. DSI0 0xae94000 (inner, running), DSI1 0xae96000 / PHY1
 * 0xae96400. TLMM: inner reset GPIO0 / ESD 116, outer reset GPIO125, outer VDDD enable GPIO93, outer ESD GPIO60.
 * DSI1 registers are only read when its byte clock reports running; nothing is written. */
#pragma once
#include <IOKit/IOLib.h>
#include <IOKit/IODeviceMemory.h>
#include <IOKit/IOMemoryDescriptor.h>
#include <kern/thread.h>
#include <pexpert/pexpert.h>
#include "ZizhanDailyProfile.h"
#include "ZizhanOuterTouchPlan.h"
#include "../PongPower/ZizhanSlpiSensors.h"
#include <kern/clock.h>
#include "../PongPower/GeniI2c.h"

extern "C" struct vnode *rootvnode;
extern "C" void pong_touch_post(uint32_t type, uint32_t finger, uint32_t x, uint32_t y);
extern "C" unsigned pong_volume_key_sample(uint32_t code, int down, uint32_t dt_ms);
extern "C" int zizhan_slpi_proximity_near(void);
extern "C" int32_t zizhan_slpi_ambient_lux_milli(void);
extern "C" void zizhan_slpi_set_prox_sim(int near);
extern "C" volatile int32_t native_battery_stats[16];
extern "C" int zizhan_display_power_state(void) __attribute__((weak));
extern "C" int zizhan_inner_panel_cover_mute(int muted) __attribute__((weak));
extern "C" void pong_scanout_set_cover_redirect(int active) __attribute__((weak));
extern "C" uint32_t pong_scanout_cover_snapshot(const uint8_t **va_out, uint64_t *phys_out,
                                                uint32_t *w_out, uint32_t *h_out, uint32_t *stride_out);

namespace zizhan_outer_probe {
struct Ro {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool open(uint64_t base, uint64_t bytes) {
    auto m = IODeviceMemory::withRange(base, bytes);
    map = m ? m->map(kIOMapAnywhere | kIOMapInhibitCache | kIOMapReadOnly) : nullptr;
    if (m) m->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  uint32_t r(uint32_t at) const { const uint32_t v = *reinterpret_cast<const volatile uint32_t *>(va + at); __asm__ volatile("dmb oshld" ::: "memory"); return v; }
  ~Ro() { if (map) map->release(); }
};


/* zizhan_outer_probe=2: closed-loop outer picture test without looking at the panel. The loader leaves CTL1 -> INTF_2 (DSI1)
 * fetching VIG2 from 0xb8fc9900 (1080x2520, stride 4320, inside the no-map splash region 0xb8000000+0x2b00000 that dbuf mode 1
 * does not use). INTF_2 = MDP 0xae01000 + 0x36000; its MISR (CTRL 0x180, SIGNATURE 0x184, Linux dpu_hw_util) signs every output
 * frame: constant while the picture is constant. Fill the buffer with colour bars and the signature must move to a new constant. */
struct Rw {
  IOMemoryMap *map = nullptr;
  volatile uint8_t *va = nullptr;
  bool open(uint64_t base, uint64_t bytes, IOOptionBits cache) {
    auto m = IODeviceMemory::withRange(base, bytes);
    map = m ? m->map(kIOMapAnywhere | cache) : nullptr;
    if (m) m->release();
    va = map ? reinterpret_cast<volatile uint8_t *>(map->getVirtualAddress()) : nullptr;
    return va != nullptr;
  }
  uint32_t r(uint32_t at) const { const uint32_t v = *reinterpret_cast<const volatile uint32_t *>(va + at); __asm__ volatile("dmb oshld" ::: "memory"); return v; }
  void w(uint32_t at, uint32_t v) { __asm__ volatile("dmb oshst" ::: "memory"); *reinterpret_cast<volatile uint32_t *>(va + at) = v; __asm__ volatile("dsb sy" ::: "memory"); }
  ~Rw() { if (map) map->release(); }
};
static unsigned misr_samples(Rw &intf, uint32_t *out, unsigned n, unsigned gap_ms) {
  unsigned got = 0;
  for (unsigned i = 0; i < n; ++i) {
    IOSleep(gap_ms);
    const uint32_t ctrl = intf.r(0x180);
    out[i] = (ctrl & (1u << 9)) ? intf.r(0x184) : 0;
    if (ctrl & (1u << 9)) ++got;
  }
  return got;
}
static void picture_test() {
  enum : uint64_t { Fb = 0xb8fc9900ull, W = 1080, H = 2520, Stride = 4320 };
  Rw mdss, intf, fb;
  if (!mdss.open(0xae00000, 0x84000, kIOMapInhibitCache) || !intf.open(0xae37000, 0x1000, kIOMapInhibitCache)) {
    IOLog("ZIZHAN_OUTER_PICTURE: FAIL map\n");
    return;
  }
  const uint32_t vig2 = mdss.r(0x9000 + 0x14), ctl1_intf = mdss.r(0x17000 + 0xf4), ctl1_fetch = mdss.r(0x17000 + 0xfc);
  IOLog("ZIZHAN_OUTER_PICTURE: vig2 addr=%x ctl1 intf=%x fetch=%x intf2 te_en=%x status=%x frame=%x\n", vig2, ctl1_intf, ctl1_fetch,
        intf.r(0x0), intf.r(0x26c), intf.r(0xac));
  if (vig2 != uint32_t(Fb)) { IOLog("ZIZHAN_OUTER_PICTURE: SKIP vig2-not-outer-buffer writes=0\n"); return; }
  intf.w(0x180, 1u << 10);
  intf.w(0x180, 0x1u | (1u << 8) | (1u << 31) | (1u << 24));
  uint32_t before[6] = {}, after[10] = {};
  const unsigned nb = misr_samples(intf, before, 6, 100);
  // offset of the buffer within its page
  const uint64_t page = Fb & ~0xfffull, skew = Fb - page;
  if (!fb.open(page, ((skew + H * Stride) + 0xfff) & ~0xfffull, kIOMapWriteCombineCache)) { IOLog("ZIZHAN_OUTER_PICTURE: FAIL fb-map\n"); return; }
  static const uint32_t bars[8] = {0xffffffffu, 0xffffff00u, 0xff00ffffu, 0xff00ff00u, 0xffff00ffu, 0xffff0000u, 0xff0000ffu, 0xff000000u};
  for (uint64_t y = 0; y < H; ++y) {
    volatile uint32_t *row = reinterpret_cast<volatile uint32_t *>(fb.va + skew + y * Stride);
    const uint32_t c = bars[(y * 8) / H];
    for (uint64_t x = 0; x < W; ++x) row[x] = (x < 64 || x >= W - 64) ? 0xff808080u : c;   // grey margins, colour bars down the panel
  }
  __asm__ volatile("dsb sy" ::: "memory");
  const unsigned na = misr_samples(intf, after, 10, 100);
  IOLog("ZIZHAN_OUTER_PICTURE: misr before(%u)=%x %x %x %x %x %x\n", nb, before[0], before[1], before[2], before[3], before[4], before[5]);
  IOLog("ZIZHAN_OUTER_PICTURE: misr after(%u)=%x %x %x %x %x %x %x %x %x %x\n", na, after[0], after[1], after[2], after[3], after[4],
        after[5], after[6], after[7], after[8], after[9]);
  const bool changed = nb && na && before[nb - 1] != after[9] && after[8] == after[9];
  IOLog("ZIZHAN_OUTER_PICTURE: DONE pattern=colour-bars signature_changed=%u stable_after=%u\n", changed ? 1u : 0u,
        (na && after[8] == after[9]) ? 1u : 0u);
}

/* zizhan_outer_probe=3: read-only outer touch inventory. ST FTS st,spi-sec on SE19 SPI (0x890000, wrap2 S4: vote 0x162010
 * bit 8, CBCR 0x12e4dc; pads GPIO72-75 qup19), reset GPIO52, IRQ GPIO119; supplies WL2866D avdd2/dvdd2 on SE1 I2C
 * (0x984000, wrap0 S1: vote 0x162008 bit 11, CBCR 0x127140; pads GPIO4/5 qup1) at 0x28. WL2866D (datasheet V1.6): reg 0x0E
 * bit0 DVDD1, bit1 DVDD2, bit2 AVDD1, bit3 AVDD2 enables. Only reads; the I2C is used only when the boot loader left it set up. */
static uint32_t se_rd(void *c, uint32_t reg) { return static_cast<Rw *>(c)->r(reg); }
static void se_wr(void *c, uint32_t reg, uint32_t v) { static_cast<Rw *>(c)->w(reg, v); }
static void outer_spi();
static void touch_inventory(bool alive_test, bool spi_test) {
#if !(XNU_PONG_WIFI || XNU_PONG_POWER)
  /* Reduced kernels (pong_ipados18_ci_defconfig) do not build PongPower, so there is no GENI I2C for the WL2866D. */
  (void)alive_test; (void)spi_test;
  IOLog("ZIZHAN_OUTER_TOUCH: SKIP kernel-without-pong-power\n");
#else
  Ro vote, w0, w2;
  if (!vote.open(0x162000, 0x1000) || !w0.open(0x127000, 0x1000) || !w2.open(0x12e000, 0x1000)) { IOLog("ZIZHAN_OUTER_TOUCH: FAIL map\n"); return; }
  const uint32_t s1 = w0.r(0x140), s4 = w2.r(0x4dc);
  IOLog("ZIZHAN_OUTER_TOUCH: gcc vote8=%x vote10=%x wrap0_s1=%x wrap2_s4=%x\n", vote.r(0x8), vote.r(0x10), s1, s4);
  static const uint16_t pins[] = {4, 5, 52, 119, 72, 73, 74, 75};
  uint32_t cfg4 = 0, cfg5 = 0;
  for (uint16_t pin : pins) {
    Ro p;
    if (!p.open(0xf100000ull + uint64_t(pin) * 0x1000, 0x1000)) continue;
    const uint32_t cfg = p.r(0), io = p.r(4);
    if (pin == 4) cfg4 = cfg;
    if (pin == 5) cfg5 = cfg;
    IOLog("ZIZHAN_OUTER_TOUCH: pin=%u cfg=%x func=%u oe=%u pull=%u in=%u out=%u\n", pin, cfg, (cfg >> 2) & 15, (cfg >> 9) & 1, cfg & 3, io & 1,
          (io >> 1) & 1);
  }
  if ((s1 & 0x80000000u) || ((cfg4 >> 2) & 15) != 1 || ((cfg5 >> 2) & 15) != 1) {
    IOLog("ZIZHAN_OUTER_TOUCH: DONE i2c=not-set-up wl2866d_read=0 writes=0\n");
    return;
  }
  Rw se;
  if (!se.open(0x984000, 0x4000, kIOMapInhibitCache)) { IOLog("ZIZHAN_OUTER_TOUCH: FAIL se-map\n"); return; }
  // The loader leaves M_CLK_CFG (div 7 from 19.2 MHz) but no SCL counters: set Linux geni-i2c 100 kHz timing (10/11/26).
  if ((se.r(0x48) & 1u) && !se.r(0x278)) { se.w(0x278, (10u << 20) | (11u << 10) | 26u); IOLog("ZIZHAN_OUTER_TOUCH: se1 scl counters set 100 kHz (clk_cfg=%x)\n", se.r(0x48)); }
  GeniI2c g;
  const GeniIo io = {se_rd, se_wr, &se};
  uint32_t config = 0;
  if (geni_i2c_prepare_poll(&g, &io, 0x28, &config)) { IOLog("ZIZHAN_OUTER_TOUCH: DONE i2c-prepare-refused config=%x writes=0\n", config); return; }
  uint8_t regs[16] = {};
  unsigned ok = 0;
  for (unsigned r = 0; r < 16; ++r) {
    if (geni_i2c_start_read(&g, uint8_t(r))) break;
    int st = GeniPending;
    for (unsigned i = 0; i < 2000 && st == GeniPending; ++i) { st = geni_i2c_poll(&g); if (st == GeniPending) IODelay(50); }
    for (unsigned k = 0; k < 3 && st == GeniPending; ++k) { st = geni_i2c_timeout(&g); IODelay(1000); }
    if (st != GeniDone) { IOLog("ZIZHAN_OUTER_TOUCH: wl2866d reg=%x failed error=%u status=%x\n", r, g.error, g.error_status); break; }
    regs[r] = g.data;
    ++ok;
  }
  IOLog("ZIZHAN_OUTER_TOUCH: wl2866d reads=%u regs00-07=%02x %02x %02x %02x %02x %02x %02x %02x regs08-0f=%02x %02x %02x %02x %02x %02x %02x %02x\n", ok,
        regs[0], regs[1], regs[2], regs[3], regs[4], regs[5], regs[6], regs[7], regs[8], regs[9], regs[10], regs[11], regs[12], regs[13], regs[14], regs[15]);
  IOLog("ZIZHAN_OUTER_TOUCH: enables dvdd1=%u dvdd2(touch vdd)=%u avdd1=%u avdd2(touch avdd)=%u\n", regs[14] & 1, (regs[14] >> 1) & 1,
        (regs[14] >> 2) & 1, (regs[14] >> 3) & 1);
  if (!alive_test || ok != 16) { IOLog("ZIZHAN_OUTER_TOUCH: DONE inventory writes=0\n"); return; }
  /* mode 4: is the outer FTS alive? Touch rails at the inner touch's voltages (VDD 1.8 V = DVDD2 code 0xc8, AVDD 3.0 V = AVDD2
   * code 0x90; WL2866D: DVDD = 0.6 V + 6 mV*code, AVDD = 1.2 V + 12.5 mV*code), the panel's DVDD1/AVDD1 bits untouched; then
   * reset GPIO52 released and IRQ GPIO119 (pull-up input) watched: an ST FTS pulls it low with its boot/ready event. */
  auto wr8 = [&](uint8_t reg, uint8_t v) -> bool {
    const uint8_t b[2] = {reg, v};
    if (geni_i2c_start_write_bytes(&g, b, 2)) return false;
    int st = GeniPending;
    for (unsigned i = 0; i < 2000 && st == GeniPending; ++i) { st = geni_i2c_poll(&g); if (st == GeniPending) IODelay(50); }
    for (unsigned k = 0; k < 3 && st == GeniPending; ++k) { st = geni_i2c_timeout(&g); IODelay(1000); }
    return st == GeniDone;
  };
  auto rd8 = [&](uint8_t reg, uint8_t *v) -> bool {
    if (geni_i2c_start_read(&g, reg)) return false;
    int st = GeniPending;
    for (unsigned i = 0; i < 2000 && st == GeniPending; ++i) { st = geni_i2c_poll(&g); if (st == GeniPending) IODelay(50); }
    for (unsigned k = 0; k < 3 && st == GeniPending; ++k) { st = geni_i2c_timeout(&g); IODelay(1000); }
    if (st != GeniDone) return false;
    *v = g.data; return true;
  };
  Rw rst, irq;
  if (!rst.open(0xf100000ull + 52ull * 0x1000, 0x1000, kIOMapInhibitCache) || !irq.open(0xf100000ull + 119ull * 0x1000, 0x1000, kIOMapInhibitCache)) {
    IOLog("ZIZHAN_OUTER_TOUCH: FAIL pin-map\n"); return;
  }
  const uint32_t s_rst = rst.r(0), s_irq = irq.r(0), s_rst_io = rst.r(4);
  const uint8_t en0 = regs[14], dv0 = regs[4], av0 = regs[6];
  rst.w(0, (s_rst & ~0x3ffu) | (1u << 9)); rst.w(4, 0);                 // reset low (output)
  irq.w(0, (s_irq & ~0x3ffu) | 3u);                                       // IRQ: GPIO input, pull-up
  uint8_t back[3] = {};
  bool ok_w = wr8(0x04, 0xc8) && wr8(0x06, 0x90) && rd8(0x04, &back[0]) && rd8(0x06, &back[1]) && back[0] == 0xc8 && back[1] == 0x90;
  if (ok_w) ok_w = wr8(0x0e, uint8_t((en0 | 0x0a) & 0x0f)) && rd8(0x0e, &back[2]) && (back[2] & 0x0f) == ((en0 | 0x0a) & 0x0f);
  IOLog("ZIZHAN_OUTER_TOUCH: rails ok=%u dvdd2=%x avdd2=%x enable=%x (panel bits kept %x)\n", ok_w ? 1u : 0u, back[0], back[1], back[2], en0 & 5u);
  unsigned lows = 0, first_low_ms = 0, highs = 0;
  if (ok_w) {
    IOSleep(10);
    rst.w(4, 2);                                                          // reset released
    for (unsigned ms = 0; ms < 1000; ++ms) {
      const unsigned v = irq.r(4) & 1u;
      if (!v) { if (!lows) first_low_ms = ms; ++lows; } else ++highs;
      IOSleep(1);
    }
  }
  IOLog("ZIZHAN_OUTER_TOUCH: irq119 samples low=%u high=%u first_low_ms=%u -> %s\n", lows, highs, first_low_ms,
        lows ? "controller alive (IRQ asserted)" : "no IRQ");
  if (spi_test && ok_w) outer_spi();
  // restore: reset low, rails back to how the loader left them, pins as before
  rst.w(4, 0);
  IOSleep(2);
  const bool r1 = wr8(0x0e, en0), r2 = wr8(0x04, dv0), r3 = wr8(0x06, av0);
  rst.w(0, s_rst); rst.w(4, s_rst_io); irq.w(0, s_irq);
  IOLog("ZIZHAN_OUTER_TOUCH: DONE alive-test restored=%u\n", (r1 && r2 && r3) ? 1u : 0u);
#endif
}

/* mode 5: the outer FTS over SE19 SPI, as bsd/dev/pong_fts.c does it for the inner one (st fst2 driver 6.0.3):
 * GCC wrap2 votes 0x162010 bits 0-3 + S4 bit 8; S4 branch 0x12e4dc, RCG CMD 0x12e4e4 / CFG +4 / DFSR +0x14 / PERF +0x1c/+0x5c/+0x9c;
 * pads GPIO72-75 qup19; GENI SPI mode 0, 8-bit, 1 MHz; configure_spi4 writes, chip id (0xfb 0x20000000), one event (0xb1 0x0060). */
static void outer_spi() {
  Rw vote, w2, se, pads[4];
  if (!vote.open(0x162000, 0x1000, kIOMapInhibitCache) || !w2.open(0x12e000, 0x1000, kIOMapInhibitCache)) { IOLog("ZIZHAN_OUTER_SPI: FAIL map\n"); return; }
  uint32_t saved[4];
  for (unsigned i = 0; i < 4; ++i) {
    if (!pads[i].open(0xf100000ull + uint64_t(72 + i) * 0x1000, 0x1000, kIOMapInhibitCache)) { IOLog("ZIZHAN_OUTER_SPI: FAIL pad-map\n"); return; }
    saved[i] = pads[i].r(0);
  }
  vote.w(0x10, vote.r(0x10) | 0xfu);
  uint32_t src_hz = 19200000, sel = 0;
  if (w2.r(0x4f8) & 1u) {
    static const uint32_t parent[8] = {19200000, 600000000, 0, 0, 0, 0, 300000000, 0};
    src_hz = 0;
    for (uint32_t l = 0; l < 8; ++l) {
      const uint32_t cfg = w2.r(0x500 + 4 * l), m = w2.r(0x540 + 4 * l), nn = w2.r(0x580 + 4 * l);
      uint64_t hz = parent[(cfg >> 8) & 7];
      const uint32_t div = cfg & 0x1f, mode = (cfg >> 12) & 3;
      if (div) hz = hz * 2 / (div + 1);
      if (mode) { const uint32_t n = (~nn & 0xffff) + (m & 0xffff); if (n) hz = hz * (m & 0xffff) / n; }
      if (hz >= 4000000 && (!src_hz || hz < src_hz)) { src_hz = uint32_t(hz); sel = l; }
    }
  } else {
    w2.w(0x4e8, (w2.r(0x4e8) & ~0x371fu) | 1u);
    w2.w(0x4e4, w2.r(0x4e4) | 1u);
    for (unsigned i = 0; i < 500 && (w2.r(0x4e4) & 1u); ++i) IODelay(10);
  }
  vote.w(0x10, vote.r(0x10) | (1u << 8));
  bool on = false;
  auto off = [](uint32_t cbcr) { return (cbcr & 2u) ? 0u : (cbcr & 0x80000000u); };
  for (unsigned i = 0; i < 1000 && !on; ++i) { on = !(off(w2.r(0x004)) | off(w2.r(0x008)) | off(w2.r(0x4dc))); if (!on) IODelay(10); }
  IOLog("ZIZHAN_OUTER_SPI: clocks on=%u src_hz=%u sel=%u s4=%x\n", on ? 1u : 0u, src_hz, sel, w2.r(0x4dc));
  if (!on || !src_hz) return;
  for (unsigned i = 0; i < 4; ++i) pads[i].w(0, (saved[i] & ~0x3ffu) | (1u << 2) | (2u << 6));   // qup19, 6 mA, no pull
  if (!se.open(0x890000, 0x4000, kIOMapInhibitCache)) { IOLog("ZIZHAN_OUTER_SPI: FAIL se-map\n"); goto pads_back; }
  {
    const uint32_t fw = se.r(0x68), depth = (se.r(0xe24) >> 16) & 0x3f;
    IOLog("ZIZHAN_OUTER_SPI: se19 fw=%x protocol=%u fifo=%u if_disable=%x\n", fw, (fw >> 8) & 255, depth, se.r(0x64));
    if (((fw >> 8) & 255) != 1 || (se.r(0x64) & 1u) || depth < 4) goto pads_back;
    se.w(0xe18, 0); se.w(0x618, ~0u); se.w(0x648, ~0u); se.w(0xc44, ~0u); se.w(0xd44, ~0u);
    se.w(0x28, se.r(0x28) | 0x7f); se.w(0xe30, se.r(0xe30) | 0xf); se.w(0x24, 0x7f); se.w(0x20, 1);
    se.w(0x258, se.r(0x258) & ~1u); se.w(0x810, depth - 3); se.w(0x814, depth - 2); se.w(0x80c, 0);
    se.w(0xe1c, se.r(0xe1c) | 0xf); se.w(0x614, se.r(0x614) | 0x7fc0007fu);
    se.w(0x22c, 0); se.w(0x250, 0); se.w(0x224, 0); se.w(0x230, 0); se.w(0x24c, 0);
    se.w(0x25c, se.r(0x25c) & ~2u); se.w(0x278, 0);
    se.w(0x260, 0x0007f8fe); se.w(0x264, 0x000ffefe); se.w(0x284, 0x0007f8fe); se.w(0x288, 0x000ffefe);
    se.w(0x254, 0); se.w(0x268, 8 - 4);
    const uint32_t div = (src_hz + 1000000 - 1) / 1000000;
    se.w(0x7c, sel & 7); se.w(0x48, (div ? div : 1) << 4 | 1);
    auto xfer = [&](const uint8_t *tx, uint8_t *rx, uint32_t len) -> bool {
      uint32_t sent = 0, got = 0;
      for (unsigned i = 0; (se.r(0x40) & 1u) && i < 2000; ++i) IODelay(100);
      se.w(0x618, ~0u); se.w(0x26c, len); se.w(0x270, len); se.w(0x600, 3u << 27);
      for (unsigned spin = 0; spin < 20000; ++spin) {
        while (sent < len && (se.r(0x800) & 0x0fffffffu) + 1 < depth) {
          uint32_t w = 0;
          for (unsigned j = 0; j < 4 && sent < len; ++j, ++sent) w |= uint32_t(tx ? tx[sent] : 0) << (8 * j);
          se.w(0x700, w);
        }
        const uint32_t st = se.r(0x610);
        const uint32_t rs = se.r(0x804), words = rs & 0x1ffffff;
        uint32_t bytes = words * 4;
        if ((rs & 0x80000000u) && ((rs >> 28) & 7)) bytes -= 4 - ((rs >> 28) & 7);
        for (uint32_t i = 0; i < words && words <= depth; ++i) {
          const uint32_t v = se.r(0x780);
          for (unsigned j = 0; j < 4 && bytes; ++j, --bytes) { if (got < len) { if (rx) rx[got] = uint8_t(v >> (8 * j)); ++got; } }
        }
        if (st & ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 24) | (1u << 25) | (1u << 28) | (1u << 29))) break;
        if (st & 1u) { se.w(0x618, st); return got == len; }
        IODelay(10);
      }
      se.w(0x604, 1u << 2); IODelay(1000); se.w(0x618, ~0u);
      return false;
    };
    uint8_t tx[16] = {}, rx[16] = {};
    auto hw_w8 = [&](uint32_t a, uint8_t v) { const uint8_t b[6] = {0xfa, uint8_t(a >> 24), uint8_t(a >> 16), uint8_t(a >> 8), uint8_t(a), v}; return xfer(b, nullptr, 6); };
    const bool cfg = hw_w8(0x20000034u, 0x02) && hw_w8(0x2000003eu, 0x07) && hw_w8(0x2000002du, 0x02);
    tx[0] = 0xfb; tx[1] = 0x20; tx[2] = 0; tx[3] = 0; tx[4] = 0;
    const bool idok = xfer(tx, rx, 4 + 1 + 1 + 2);
    IOLog("ZIZHAN_OUTER_SPI: configure_spi4=%u chip_id_xfer=%u id=%02x%02x (inner FST2 is 3652)\n", cfg ? 1u : 0u, idok ? 1u : 0u, rx[6], rx[7]);
    uint8_t ev[16] = {};
    memset(tx, 0, sizeof tx); tx[0] = 0xb1; tx[1] = 0x00; tx[2] = 0x60;
    const bool evok = xfer(tx, ev, 3 + 1 + 8);
    IOLog("ZIZHAN_OUTER_SPI: event_xfer=%u event=%02x %02x %02x %02x %02x %02x %02x %02x (0x03 = controller ready)\n", evok ? 1u : 0u,
          ev[4], ev[5], ev[6], ev[7], ev[8], ev[9], ev[10], ev[11]);
    /* FTM5 SALIXP (chip id 0x3640) over SPI, per the ST fts_spi driver in Xiaomi's sm8475 kernel source: the FIFO read
     * command is 0x87 on SPI (0x86 is the I2C opcode), then one dummy byte, then an 8-byte event. Drain while IRQ GPIO119 is low:
     * 0x03 = controller ready, 0x43 = status update, 0xf3 = error. Then start active scanning (A0 00 01, the driver's resume
     * setting) and drain again. The IRQ line going high after the reads is the closed-loop check; no finger is needed. */
    {
      Rw irqp;
      const bool irq_ok = irqp.open(0xf100000ull + 119ull * 0x1000, 0x1000, kIOMapInhibitCache);
      auto irq_level = [&]() -> unsigned { return irq_ok ? (irqp.r(4) & 1u) : 2u; };
      auto drain = [&](const char *phase) {
        unsigned n = 0;
        for (; n < 16; ++n) {
          uint8_t t2[10] = {0x87}, r2[10] = {};
          const bool ok = xfer(t2, r2, 1 + 1 + 8);
          const unsigned level = irq_level();
          IOLog("ZIZHAN_OUTER_SPI: ftm5 %s event %u xfer=%u data=%02x %02x %02x %02x %02x %02x %02x %02x irq_after=%u\n", phase, n,
                ok ? 1u : 0u, r2[2], r2[3], r2[4], r2[5], r2[6], r2[7], r2[8], r2[9], level);
          if (!ok || level == 1 || r2[2] == 0x00) { ++n; break; }
        }
        return n;
      };
      const unsigned before = irq_level();
      const unsigned n1 = drain("boot");
      const unsigned mid = irq_level();
      const uint8_t scan[3] = {0xa0, 0x00, 0x01};
      const bool scan_ok = xfer(scan, nullptr, 3);
      IODelay(50000);
      const unsigned after_scan = irq_level();
      const unsigned n2 = drain("scan");
      IOLog("ZIZHAN_OUTER_SPI: ftm5 summary irq_before=%u boot_reads=%u irq_after_boot=%u scan_cmd=%u irq_after_scan=%u scan_reads=%u irq_end=%u\n",
            before, n1, mid, scan_ok ? 1u : 0u, after_scan, n2, irq_level());
    }
  }
pads_back:
  for (unsigned i = 0; i < 4; ++i) pads[i].w(0, saved[i]);
  IOLog("ZIZHAN_OUTER_SPI: DONE pads_restored=1 (clock votes kept)\n");
}

/* zizhan_outer_fold=1 (daily default): closed draws MM-DD + battery % + HH:MM (UTC+8) on 0xb8fc9900 (1080x2520, stride 4320),
 * open clears to black. zizhan_outer_fold=2: live interactive 2:1 iPadOS desktop (2160x1916 -> 1080x958 centered at y=781..1738)
 * with top status bar while the inner 8.02" OLED is cover-muted. INTF_2 MISR signs every output frame as closed-loop proof. */
namespace fold {
/* Kept for the life of the system (no function-local statics: the kernel has no guard variables). */
static IOMemoryMap *fb_map, *intf_map;
static volatile uint8_t *fb_va, *intf_va;
static bool ready, misr_on;
static uint32_t mode_val;
static uint8_t mode_parsed;
static uint32_t allow_doubletap = 1;
static uint32_t allow_pocket_guard = 1;
static uint32_t allow_outer_dim = 1;
static volatile uint32_t lid_closed, hud_dirty, active_view, desktop_busy;
static volatile uint64_t wake_req_ms;

static uint32_t cover_scale_q8() {
  if (!allow_outer_dim) {
    return 256u;
  }
  return zizhan_slpi_sensors::outer_cover_scale_q8(zizhan_slpi_ambient_lux_milli());
}

static uint32_t current_mode() {
  if (!mode_parsed) {
    uint32_t v = 0;
    if (!zizhan_daily::arg("zizhan_outer_fold", &v, zizhan_daily::kOuterFold)) {
      v = 0;
    }
    mode_val = v;
    mode_parsed = 1;
  }
  return mode_val;
}
static volatile uint8_t *map_once(uint64_t base, uint64_t bytes, IOOptionBits cache, IOMemoryMap **out) {
  auto m = IODeviceMemory::withRange(base, bytes);
  *out = m ? m->map(kIOMapAnywhere | cache) : nullptr;
  if (m) { m->release(); }
  return *out ? reinterpret_cast<volatile uint8_t *>((*out)->getVirtualAddress()) : nullptr;
}
static uint32_t ir(uint32_t at) { const uint32_t v = *reinterpret_cast<volatile uint32_t *>(intf_va + at); __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static void iw(uint32_t at, uint32_t v) { *reinterpret_cast<volatile uint32_t *>(intf_va + at) = v; __asm__ volatile("dsb sy" ::: "memory"); }
static bool init() {
  if (ready) { return true; }
  const uint64_t base = 0xb8fc9900ull, page = base & ~0xfffull, skew = base - page;
  {
    Rw mdss;
    if (!mdss.open(0xae00000, 0x84000, kIOMapInhibitCache)) { return false; }
    if (mdss.r(0x9000 + 0x14) != uint32_t(base)) { IOLog("ZIZHAN_OUTER_FOLD: SKIP vig2 not on the outer buffer\n"); return false; }
  }
  fb_va = map_once(page, ((skew + 2520ull * 4320) + 0xfff) & ~0xfffull, kIOMapWriteCombineCache, &fb_map);
  intf_va = map_once(0xae37000, 0x1000, kIOMapInhibitCache, &intf_map);
  ready = fb_va && intf_va;
  return ready;
}
static inline volatile uint32_t *row(unsigned y) { return reinterpret_cast<volatile uint32_t *>(fb_va + (0xb8fc9900ull & 0xfffull) + uint64_t(y) * 4320); }
static void rect(unsigned x0, unsigned y0, unsigned w, unsigned h, uint32_t c) {
  const uint32_t sc = (c == 0xff000000u) ? 256u : cover_scale_q8();
  const uint32_t dc = (sc < 256u) ? zizhan_outer_touch::scale_bgra_q8(c, sc) : c;
  for (unsigned y = y0; y < y0 + h && y < 2520; ++y) {
    volatile uint32_t *r = row(y);
    for (unsigned x = x0; x < x0 + w && x < 1080; ++x) { r[x] = dc; }
  }
}
/* seven segments a..g of a W x H digit at (x, y), stroke t */
static void digit(unsigned d, unsigned x, unsigned y, unsigned W, unsigned H, unsigned t, uint32_t c) {
  static const uint8_t seg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  const uint8_t m = seg[d % 10];
  const unsigned h2 = H / 2;
  if (m & 0x01) { rect(x, y, W, t, c); }
  if (m & 0x02) { rect(x + W - t, y, t, h2, c); }
  if (m & 0x04) { rect(x + W - t, y + h2, t, H - h2, c); }
  if (m & 0x08) { rect(x, y + H - t, W, t, c); }
  if (m & 0x10) { rect(x, y + h2, t, H - h2, c); }
  if (m & 0x20) { rect(x, y, t, h2, c); }
  if (m & 0x40) { rect(x, y + h2 - t / 2, W, t, c); }
}
static void ensure_misr() {
  if (!misr_on) {
    iw(0x180, 1u << 10);
    iw(0x180, 0x1u | (1u << 8) | (1u << 31) | (1u << 24));
    misr_on = true;
  }
}
static uint32_t misr_peek() {
  ensure_misr();
  return (ir(0x180) & (1u << 9)) ? ir(0x184) : 0;
}
static uint32_t misr() {
  ensure_misr();
  IOSleep(60);
  return (ir(0x180) & (1u << 9)) ? ir(0x184) : 0;
}
static void draw_battery_pill(unsigned x, unsigned y, int pct, bool charging) {
  const unsigned bw = 96, bh = 44, bt = 5;
  rect(x, y, bw, bt, 0xffd0d4dcu);
  rect(x, y + bh - bt, bw, bt, 0xffd0d4dcu);
  rect(x, y, bt, bh, 0xffd0d4dcu);
  rect(x + bw - bt, y, bt, bh, 0xffd0d4dcu);
  rect(x + bw, y + bh / 4, 7, bh / 2, 0xffd0d4dcu);
  if (pct >= 0) {
    const unsigned clamped = pct > 100 ? 100u : unsigned(pct);
    const unsigned inner_max = bw - 2 * (bt + 4);
    const unsigned fill = (inner_max * clamped + 50u) / 100u;
    const uint32_t fc = charging ? 0xff34c759u : (clamped < 20u ? 0xffff9f0au : 0xffffffffu);
    if (fill > 0) {
      rect(x + bt + 4, y + bt + 4, fill, bh - 2 * (bt + 4), fc);
    }
    const unsigned dw = 24, dh = 44, dt = 6, dg = 7;
    unsigned dx = x + bw + 20;
    if (clamped >= 100u) {
      digit(1, dx, y, dw, dh, dt, fc);
      dx += dw + dg;
      digit(0, dx, y, dw, dh, dt, fc);
      dx += dw + dg;
      digit(0, dx, y, dw, dh, dt, fc);
    } else {
      if (clamped >= 10u) {
        digit(clamped / 10u, dx, y, dw, dh, dt, fc);
        dx += dw + dg;
      }
      digit(clamped % 10u, dx, y, dw, dh, dt, fc);
    }
  }
}
static void draw_status_strip(unsigned y, bool include_time, zizhan_outer_touch::CalendarLocal *cal_out,
                              int *pct_out, bool *chg_out) {
  clock_sec_t sec = 0;
  clock_usec_t usec = 0;
  clock_get_calendar_microtime(&sec, &usec);
  const auto cal = zizhan_outer_touch::calendar_utc8(uint64_t(sec));
  const bool batt_ok = native_battery_stats[0] == 4;
  const int pct = batt_ok ? int(native_battery_stats[1]) : -1;
  const bool chg = batt_ok && ((native_battery_stats[5] & 4) != 0);
  if (cal_out) { *cal_out = cal; }
  if (pct_out) { *pct_out = pct; }
  if (chg_out) { *chg_out = chg; }
  rect(0, y > 8 ? (y - 8) : 0, 1080, 64, 0xff000000u);
  const unsigned dw = 26, dh = 46, dt = 6, dg = 8;
  unsigned cx = 64;
  digit(cal.month / 10u, cx, y, dw, dh, dt, 0xffb8c0ccu); cx += dw + dg;
  digit(cal.month % 10u, cx, y, dw, dh, dt, 0xffb8c0ccu); cx += dw + dg;
  rect(cx, y + dh / 2 - dt / 2, 16, dt, 0xffb8c0ccu); cx += 16 + dg;
  digit(cal.day / 10u, cx, y, dw, dh, dt, 0xffb8c0ccu); cx += dw + dg;
  digit(cal.day % 10u, cx, y, dw, dh, dt, 0xffb8c0ccu); cx += dw + 28;
  if (include_time) {
    digit(cal.hour / 10u, cx, y, dw, dh, dt, 0xffffffffu); cx += dw + dg;
    digit(cal.hour % 10u, cx, y, dw, dh, dt, 0xffffffffu); cx += dw + dg;
    rect(cx, y + dh / 3 - dt / 2, dt, dt, 0xffffffffu);
    rect(cx, y + (2 * dh) / 3 - dt / 2, dt, dt, 0xffffffffu);
    cx += dt + dg;
    digit(cal.minute / 10u, cx, y, dw, dh, dt, 0xffffffffu); cx += dw + dg;
    digit(cal.minute % 10u, cx, y, dw, dh, dt, 0xffffffffu);
  }
  draw_battery_pill(800, y + 1, pct, chg);
}
static void draw_pill_box(unsigned px, unsigned py, unsigned pw, unsigned ph) {
  const unsigned pt = 3;
  rect(px, py, pw, ph, 0xff1c1c1eu);
  rect(px, py, pw, pt, 0xff636366u);
  rect(px, py + ph - pt, pw, pt, 0xff636366u);
  rect(px, py, pt, ph, 0xff636366u);
  rect(px + pw - pt, py, pt, ph, 0xff636366u);
}
static void draw_lock_pill() {
  const unsigned py = 1900, ph = 56;
  /* 1. VOL- pill (56..256): minus bar */
  draw_pill_box(56, py, 200, ph);
  rect(138, py + 26, 36, 4, 0xffe5e5eau);
  /* 2. HOME pill (284..496): rounded-square home icon */
  draw_pill_box(284, py, 212, ph);
  rect(376, py + 14, 28, 3, 0xffe5e5eau);
  rect(376, py + 39, 28, 3, 0xffe5e5eau);
  rect(376, py + 14, 3, 28, 0xffe5e5eau);
  rect(401, py + 14, 3, 28, 0xffe5e5eau);
  /* 3. CLOCK / LOCK pill (524..756): clock dial + hands */
  draw_pill_box(524, py, 232, ph);
  const unsigned cx = 624, cy = 1912, cs = 32, ct = 3;
  rect(cx, cy, cs, ct, 0xffd0d4dcu);
  rect(cx, cy + cs - ct, cs, ct, 0xffd0d4dcu);
  rect(cx, cy, ct, cs, 0xffd0d4dcu);
  rect(cx + cs - ct, cy, ct, cs, 0xffd0d4dcu);
  rect(cx + cs / 2 - 1, cy + 7, 3, 10, 0xffffffffu);
  rect(cx + cs / 2 - 1, cy + 15, 9, 3, 0xffffffffu);
  /* 4. VOL+ pill (784..1024): plus cross */
  draw_pill_box(784, py, 240, ph);
  rect(886, py + 26, 36, 4, 0xffe5e5eau);
  rect(902, py + 10, 4, 36, 0xffe5e5eau);
}
static void draw(bool closed) {
  if (!init()) { return; }
  rect(0, 0, 1080, 2520, 0xff000000u);
  if (closed) {
    zizhan_outer_touch::CalendarLocal cal{};
    int pct = -1;
    bool chg = false;
    draw_status_strip(680, false, &cal, &pct, &chg);
    const unsigned W = 190, H = 360, t = 40, y = 980, gap = 40;
    digit(cal.hour / 10u, 60, y, W, H, t, 0xffffffffu);
    digit(cal.hour % 10u, 60 + W + gap, y, W, H, t, 0xffffffffu);
    rect(540 - t / 2, y + H / 3 - t / 2, t, t, 0xffffffffu);
    rect(540 - t / 2, y + 2 * H / 3 - t / 2, t, t, 0xffffffffu);
    digit(cal.minute / 10u, 1080 - 60 - 2 * W - gap, y, W, H, t, 0xffffffffu);
    digit(cal.minute % 10u, 1080 - 60 - W, y, W, H, t, 0xffffffffu);
    if (allow_doubletap) {
      rect(440, 1924, 200, 6, 0xff48484au);
    }
    __asm__ volatile("dsb sy" ::: "memory");
    IOLog("ZIZHAN_OUTER_FOLD: closed clock %02u-%02u %02u:%02u batt=%d chg=%u misr=%x\n",
          cal.month, cal.day, cal.hour, cal.minute, pct, chg ? 1u : 0u, misr());
  } else {
    __asm__ volatile("dsb sy" ::: "memory");
    IOLog("ZIZHAN_OUTER_FOLD: open black misr=%x\n", misr());
  }
}

static uint64_t mono_ms() {
  uint64_t ns = 0;
  absolutetime_to_nanoseconds(mach_absolute_time(), &ns);
  return ns / 1000000ull;
}

static void switch_to_desktop() {
  if (!__atomic_load_n(&lid_closed, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&active_view, __ATOMIC_ACQUIRE) == 2u) {
    return;
  }
  __atomic_store_n(&wake_req_ms, mono_ms(), __ATOMIC_RELEASE);
  if (pong_scanout_set_cover_redirect) {
    pong_scanout_set_cover_redirect(1);
  }
  if (zizhan_inner_panel_cover_mute) {
    (void)zizhan_inner_panel_cover_mute(1);
  }
  __atomic_store_n(&active_view, 2u, __ATOMIC_RELEASE);
  const int disp = zizhan_display_power_state ? zizhan_display_power_state() : -1;
  bool woke = false;
  if (disp == 0) {
    pong_touch_post(4u, 0u, 0, 0);
    IOSleep(80);
    pong_touch_post(5u, 0u, 0, 0);
    woke = true;
  }
  IOLog("ZIZHAN_OUTER_FOLD: toggle clock->desktop prev_display=%d woke=%u\n", disp, woke ? 1u : 0u);
}

static void switch_to_clock() {
  if (!__atomic_load_n(&lid_closed, __ATOMIC_ACQUIRE) ||
      __atomic_load_n(&active_view, __ATOMIC_ACQUIRE) != 2u) {
    return;
  }
  __atomic_store_n(&active_view, 1u, __ATOMIC_RELEASE);
  for (unsigned w = 0; w < 30 && __atomic_load_n(&desktop_busy, __ATOMIC_ACQUIRE); ++w) {
    IOSleep(10);
  }
  draw(true);
  if (pong_scanout_set_cover_redirect) {
    pong_scanout_set_cover_redirect(0);
  }
  const int disp = zizhan_display_power_state ? zizhan_display_power_state() : -1;
  bool slept = false;
  if (disp == 1) {
    pong_touch_post(4u, 0u, 0, 0);
    IOSleep(120);
    pong_touch_post(5u, 0u, 0, 0);
    slept = true;
    for (unsigned w = 0; w < 40; ++w) {
      if (zizhan_display_power_state && zizhan_display_power_state() == 0) {
        break;
      }
      IOSleep(50);
    }
  }
  if (zizhan_inner_panel_cover_mute) {
    (void)zizhan_inner_panel_cover_mute(0);
  }
  IOLog("ZIZHAN_OUTER_FOLD: toggle desktop->clock prev_display=%d slept=%u end_display=%d\n",
        disp, slept ? 1u : 0u, zizhan_display_power_state ? zizhan_display_power_state() : -1);
}

static void cover_desktop_worker(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_outer_desk");
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i) {
    IOSleep(100);
  }
  if (current_mode() != 0u && !__atomic_load_n(&lid_closed, __ATOMIC_ACQUIRE)) {
    draw(false);
  }
  bool was_active = false;
  uint32_t last_seq = 0, seq_first = 0, seq_last = 0, blits = 0, idle_skips = 0, idle_streak = 0;
  uint32_t first_misr = 0, last_misr = 0, prev_misr = 0, distinct_misrs = 0;
  uint64_t last_hud_ms = 0, last_misr_ms = 0;
  for (;;) {
    const bool closed = __atomic_load_n(&lid_closed, __ATOMIC_ACQUIRE) != 0;
    const bool desk_on = closed && (__atomic_load_n(&active_view, __ATOMIC_ACQUIRE) == 2u);
    if (!desk_on) {
      if (was_active) {
        was_active = false;
        uint32_t open_sig = 0;
        if (!closed && init()) {
          rect(0, 0, 1080, 2520, 0xff000000u);
          __asm__ volatile("dsb sy" ::: "memory");
          open_sig = misr();
        }
        IOLog("ZIZHAN_OUTER_DESKTOP: SUMMARY blits=%u idle_skips=%u seq_first=%u seq_last=%u first_misr=%x last_misr=%x distinct_misrs=%u open_misr=%x\n",
              blits, idle_skips, seq_first, seq_last, first_misr, last_misr, distinct_misrs, open_sig);
        __atomic_store_n(&desktop_busy, 0u, __ATOMIC_RELEASE);
      }
      IOSleep(50);
      continue;
    }
    __atomic_store_n(&desktop_busy, 1u, __ATOMIC_RELEASE);
    if (!init()) {
      IOSleep(200);
      continue;
    }
    const uint64_t now = mono_ms();
    if (!was_active) {
      was_active = true;
      last_seq = 0; seq_first = 0; seq_last = 0; blits = 0; idle_skips = 0; idle_streak = 0;
      first_misr = 0; last_misr = 0; prev_misr = 0; distinct_misrs = 0;
      rect(0, 0, 1080, 2520, 0xff000000u);
      rect(0, 777, 1080, 2, 0xff2c2c2eu);
      rect(0, 1741, 1080, 2, 0xff2c2c2eu);
      draw_status_strip(704, true, nullptr, nullptr, nullptr);
      if (allow_doubletap) {
        draw_lock_pill();
      }
      __asm__ volatile("dsb sy" ::: "memory");
      ensure_misr();
      last_hud_ms = now;
      last_misr_ms = 0;
      IOLog("ZIZHAN_OUTER_DESKTOP: ACTIVE viewport=1080x958@y781 pill=%u\n", allow_doubletap ? 1u : 0u);
    } else if (__atomic_exchange_n(&hud_dirty, 0u, __ATOMIC_ACQ_REL) || now - last_hud_ms >= 15000ull) {
      draw_status_strip(704, true, nullptr, nullptr, nullptr);
      if (allow_doubletap) {
        draw_lock_pill();
      }
      __asm__ volatile("dsb sy" ::: "memory");
      last_hud_ms = now;
    }
    const uint8_t *src_va = nullptr;
    uint64_t src_phys = 0;
    uint32_t src_w = 0, src_h = 0, src_stride = 0;
    const uint32_t seq = pong_scanout_cover_snapshot(&src_va, &src_phys, &src_w, &src_h, &src_stride);
    if (seq != 0 && src_va) {
      if (seq != last_seq || blits == 0) {
        const uint32_t sc_q8 = cover_scale_q8();
        const unsigned rows = zizhan_outer_touch::blit_2to1_bgra(
            src_va, src_stride, src_w, src_h,
            fb_va + (0xb8fc9900ull & 0xfffull), 4320u, zizhan_outer_touch::kDesktopY0, sc_q8);
        if (rows > 0) {
          __asm__ volatile("dsb sy" ::: "memory");
          last_seq = seq;
          if (!seq_first) { seq_first = seq; }
          seq_last = seq;
          ++blits;
          idle_streak = 0;
          if (blits == 1) {
            const uint64_t req0 = __atomic_exchange_n(&wake_req_ms, 0ull, __ATOMIC_ACQ_REL);
            const uint64_t t_now = mono_ms();
            if (req0 && t_now >= req0) {
              IOLog("ZIZHAN_OUTER_DESKTOP: first_frame wake_to_blit_ms=%llu scale_q8=%u\n",
                    (unsigned long long)(t_now - req0), sc_q8);
            }
            IOSleep(16);
          }
          if (blits <= 3 || now - last_misr_ms >= 350ull) {
            const uint32_t sig = misr_peek();
            if (sig != 0) {
              if (!first_misr) { first_misr = sig; }
              last_misr = sig;
              if (sig != prev_misr) {
                ++distinct_misrs;
                prev_misr = sig;
              }
            }
            last_misr_ms = now;
          }
        }
      } else {
        ++idle_skips;
        if (idle_streak < 16u) {
          ++idle_streak;
        }
      }
    }
    IOSleep(idle_streak >= 8u ? 50 : 16);
  }
}
}  // namespace fold

extern "C" uint32_t zizhan_outer_fold_mode(void) {
  return fold::current_mode();
}

extern "C" int zizhan_outer_lid_closed(void) {
  return __atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE) ? 1 : 0;
}

extern "C" void zizhan_outer_fold_show(int closed) {
  const uint32_t m = fold::current_mode();
  if (m == 0u) {
    __atomic_store_n(&fold::lid_closed, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&fold::active_view, 0u, __ATOMIC_RELEASE);
    return;
  }
  if (closed) {
    const uint32_t was_closed = __atomic_exchange_n(&fold::lid_closed, 1u, __ATOMIC_ACQ_REL);
    if (!was_closed) {
      const uint32_t view = (m == 2u) ? 2u : 1u;
      __atomic_store_n(&fold::active_view, view, __ATOMIC_RELEASE);
      if (view == 1u) {
        fold::draw(true);
      }
    } else if (__atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE) == 1u) {
      fold::draw(true);
    }
  } else {
    __atomic_store_n(&fold::lid_closed, 0u, __ATOMIC_RELEASE);
    const uint32_t prev = __atomic_exchange_n(&fold::active_view, 0u, __ATOMIC_ACQ_REL);
    if (prev != 0u) {
      fold::draw(false);
    }
  }
}

#if XNU_PONG_WIFI || XNU_PONG_POWER
static uint32_t outer_touch_synth;
static void outer_touch_worker(void *, wait_result_t) {
  thread_set_thread_name(current_thread(), "zizhan_outer_touch");
  for (unsigned i = 0; i < 1200 && !__atomic_load_n(&rootvnode, __ATOMIC_ACQUIRE); ++i) {
    IOSleep(100);
  }
  for (;;) {
    if (!__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) {
      IOSleep(50);
      continue;
    }
    Rw vote, w0, w2, se1, se19, rst, irq, pads[4];
    if (!vote.open(0x162000, 0x1000, kIOMapInhibitCache) ||
        !w0.open(0x127000, 0x1000, kIOMapInhibitCache) ||
        !w2.open(0x12e000, 0x1000, kIOMapInhibitCache) ||
        !se1.open(0x984000, 0x4000, kIOMapInhibitCache) ||
        !rst.open(0xf100000ull + 52ull * 0x1000, 0x1000, kIOMapInhibitCache) ||
        !irq.open(0xf100000ull + 119ull * 0x1000, 0x1000, kIOMapInhibitCache)) {
      IOLog("ZIZHAN_OUTER_TOUCH_LIVE: FAIL map\n");
      while (__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) { IOSleep(200); }
      continue;
    }
    bool pad_ok = true;
    uint32_t saved_pads[4] = {};
    for (unsigned i = 0; i < 4; ++i) {
      if (!pads[i].open(0xf100000ull + uint64_t(72 + i) * 0x1000, 0x1000, kIOMapInhibitCache)) {
        pad_ok = false;
        break;
      }
      saved_pads[i] = pads[i].r(0);
    }
    if (!pad_ok || (w0.r(0x140) & 0x80000000u)) {
      IOLog("ZIZHAN_OUTER_TOUCH_LIVE: SKIP se1-or-pads-unavailable\n");
      while (__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) { IOSleep(200); }
      continue;
    }
    if ((se1.r(0x48) & 1u) && !se1.r(0x278)) {
      se1.w(0x278, (10u << 20) | (11u << 10) | 26u);
    }
    GeniI2c i2c;
    const GeniIo i2c_io = {se_rd, se_wr, &se1};
    uint32_t i2c_cfg = 0;
    if (geni_i2c_prepare_poll(&i2c, &i2c_io, 0x28, &i2c_cfg)) {
      IOLog("ZIZHAN_OUTER_TOUCH_LIVE: SKIP wl2866d-i2c-busy cfg=%x\n", i2c_cfg);
      while (__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) { IOSleep(200); }
      continue;
    }
    auto wr8 = [&](uint8_t reg, uint8_t v) -> bool {
      const uint8_t b[2] = {reg, v};
      if (geni_i2c_start_write_bytes(&i2c, b, 2)) { return false; }
      int st = GeniPending;
      for (unsigned i = 0; i < 2000 && st == GeniPending; ++i) { st = geni_i2c_poll(&i2c); if (st == GeniPending) { IODelay(50); } }
      for (unsigned k = 0; k < 3 && st == GeniPending; ++k) { st = geni_i2c_timeout(&i2c); IODelay(1000); }
      return st == GeniDone;
    };
    auto rd8 = [&](uint8_t reg, uint8_t *v) -> bool {
      if (geni_i2c_start_read(&i2c, reg)) { return false; }
      int st = GeniPending;
      for (unsigned i = 0; i < 2000 && st == GeniPending; ++i) { st = geni_i2c_poll(&i2c); if (st == GeniPending) { IODelay(50); } }
      for (unsigned k = 0; k < 3 && st == GeniPending; ++k) { st = geni_i2c_timeout(&i2c); IODelay(1000); }
      if (st != GeniDone) { return false; }
      *v = i2c.data;
      return true;
    };
    uint8_t en0 = 0, dv0 = 0, av0 = 0;
    if (!rd8(0x0e, &en0) || !rd8(0x04, &dv0) || !rd8(0x06, &av0)) {
      IOLog("ZIZHAN_OUTER_TOUCH_LIVE: FAIL wl2866d-read\n");
      while (__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) { IOSleep(200); }
      continue;
    }
    const uint32_t s_rst = rst.r(0), s_rst_io = rst.r(4), s_irq = irq.r(0);
    const uint32_t vote10_0 = vote.r(0x10);
    rst.w(0, (s_rst & ~0x3ffu) | (1u << 9));
    rst.w(4, 0);
    irq.w(0, (s_irq & ~0x3ffu) | 3u);
    const bool rails_ok = wr8(0x04, 0xc8) && wr8(0x06, 0x90) && wr8(0x0e, uint8_t((en0 | 0x0a) & 0x0f));
    if (!rails_ok) {
      rst.w(0, s_rst); rst.w(4, s_rst_io); irq.w(0, s_irq);
      IOLog("ZIZHAN_OUTER_TOUCH_LIVE: FAIL wl2866d-rails\n");
      while (__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) { IOSleep(200); }
      continue;
    }
    IOSleep(10);
    rst.w(4, 2);
    for (unsigned ms = 0; ms < 250 && (irq.r(4) & 1u); ++ms) {
      IOSleep(1);
    }
    vote.w(0x10, vote.r(0x10) | 0xfu);
    uint32_t src_hz = 19200000, sel = 0;
    if (w2.r(0x4f8) & 1u) {
      static const uint32_t parent[8] = {19200000, 600000000, 0, 0, 0, 0, 300000000, 0};
      src_hz = 0;
      for (uint32_t l = 0; l < 8; ++l) {
        const uint32_t cfg = w2.r(0x500 + 4 * l), m = w2.r(0x540 + 4 * l), nn = w2.r(0x580 + 4 * l);
        uint64_t hz = parent[(cfg >> 8) & 7];
        const uint32_t div = cfg & 0x1f, md = (cfg >> 12) & 3;
        if (div) { hz = hz * 2 / (div + 1); }
        if (md) { const uint32_t n = (~nn & 0xffff) + (m & 0xffff); if (n) { hz = hz * (m & 0xffff) / n; } }
        if (hz >= 4000000 && (!src_hz || hz < src_hz)) { src_hz = uint32_t(hz); sel = l; }
      }
    } else {
      w2.w(0x4e8, (w2.r(0x4e8) & ~0x371fu) | 1u);
      w2.w(0x4e4, w2.r(0x4e4) | 1u);
      for (unsigned i = 0; i < 500 && (w2.r(0x4e4) & 1u); ++i) { IODelay(10); }
    }
    vote.w(0x10, vote.r(0x10) | (1u << 8));
    bool clk_on = false;
    auto cbcr_off = [](uint32_t cbcr) { return (cbcr & 2u) ? 0u : (cbcr & 0x80000000u); };
    for (unsigned i = 0; i < 1000 && !clk_on; ++i) {
      clk_on = !(cbcr_off(w2.r(0x004)) | cbcr_off(w2.r(0x008)) | cbcr_off(w2.r(0x4dc)));
      if (!clk_on) { IODelay(10); }
    }
    for (unsigned i = 0; i < 4; ++i) {
      pads[i].w(0, (saved_pads[i] & ~0x3ffu) | (1u << 2) | (2u << 6));
    }
    bool spi_ready = clk_on && src_hz != 0 && se19.open(0x890000, 0x4000, kIOMapInhibitCache);
    uint32_t depth = 0;
    if (spi_ready) {
      const uint32_t fw = se19.r(0x68);
      depth = (se19.r(0xe24) >> 16) & 0x3f;
      if (((fw >> 8) & 255u) != 1u || (se19.r(0x64) & 1u) || depth < 4u) {
        spi_ready = false;
      } else {
        se19.w(0xe18, 0); se19.w(0x618, ~0u); se19.w(0x648, ~0u); se19.w(0xc44, ~0u); se19.w(0xd44, ~0u);
        se19.w(0x28, se19.r(0x28) | 0x7f); se19.w(0xe30, se19.r(0xe30) | 0xf); se19.w(0x24, 0x7f); se19.w(0x20, 1);
        se19.w(0x258, se19.r(0x258) & ~1u); se19.w(0x810, depth - 3); se19.w(0x814, depth - 2); se19.w(0x80c, 0);
        se19.w(0xe1c, se19.r(0xe1c) | 0xf); se19.w(0x614, se19.r(0x614) | 0x7fc0007fu);
        se19.w(0x22c, 0); se19.w(0x250, 0); se19.w(0x224, 0); se19.w(0x230, 0); se19.w(0x24c, 0);
        se19.w(0x25c, se19.r(0x25c) & ~2u); se19.w(0x278, 0);
        se19.w(0x260, 0x0007f8fe); se19.w(0x264, 0x000ffefe); se19.w(0x284, 0x0007f8fe); se19.w(0x288, 0x000ffefe);
        se19.w(0x254, 0); se19.w(0x268, 8 - 4);
        const uint32_t div = (src_hz + 1000000 - 1) / 1000000;
        se19.w(0x7c, sel & 7); se19.w(0x48, ((div ? div : 1) << 4) | 1u);
      }
    }
    auto xfer = [&](const uint8_t *tx, uint8_t *rx, uint32_t len) -> bool {
      uint32_t sent = 0, got = 0;
      for (unsigned i = 0; (se19.r(0x40) & 1u) && i < 2000; ++i) { IODelay(100); }
      se19.w(0x618, ~0u); se19.w(0x26c, len); se19.w(0x270, len); se19.w(0x600, 3u << 27);
      for (unsigned spin = 0; spin < 20000; ++spin) {
        while (sent < len && (se19.r(0x800) & 0x0fffffffu) + 1 < depth) {
          uint32_t w = 0;
          for (unsigned j = 0; j < 4 && sent < len; ++j, ++sent) { w |= uint32_t(tx ? tx[sent] : 0) << (8 * j); }
          se19.w(0x700, w);
        }
        const uint32_t st = se19.r(0x610);
        const uint32_t rs = se19.r(0x804), words = rs & 0x1ffffff;
        uint32_t bytes = words * 4;
        if ((rs & 0x80000000u) && ((rs >> 28) & 7)) { bytes -= 4 - ((rs >> 28) & 7); }
        for (uint32_t i = 0; i < words && words <= depth; ++i) {
          const uint32_t v = se19.r(0x780);
          for (unsigned j = 0; j < 4 && bytes; ++j, --bytes) {
            if (got < len) {
              if (rx) { rx[got] = uint8_t(v >> (8 * j)); }
              ++got;
            }
          }
        }
        if (st & ((1u << 1) | (1u << 2) | (1u << 3) | (1u << 24) | (1u << 25) | (1u << 28) | (1u << 29))) { break; }
        if (st & 1u) { se19.w(0x618, st); return got == len; }
        IODelay(10);
      }
      se19.w(0x604, 1u << 2); IODelay(1000); se19.w(0x618, ~0u);
      return false;
    };
    zizhan_outer_touch::Tracker tracker{};
    uint16_t chip_id = 0, scr_x = 0, scr_y = 0;
    uint8_t sys_hdr[2] = {};
    if (spi_ready) {
      auto hw_w8 = [&](uint32_t a, uint8_t v) {
        const uint8_t b[6] = {0xfa, uint8_t(a >> 24), uint8_t(a >> 16), uint8_t(a >> 8), uint8_t(a), v};
        return xfer(b, nullptr, 6);
      };
      (void)hw_w8(0x20000034u, 0x02);
      (void)hw_w8(0x2000003eu, 0x07);
      (void)hw_w8(0x2000002du, 0x02);
      uint8_t id_tx[8] = {0xfb, 0x20, 0, 0, 0}, id_rx[8] = {};
      if (xfer(id_tx, id_rx, 8)) {
        chip_id = uint16_t((uint16_t(id_rx[6]) << 8) | id_rx[7]);
      }
      for (unsigned d = 0; d < 16 && !(irq.r(4) & 1u); ++d) {
        uint8_t t2[10] = {0x87}, r2[10] = {};
        if (!xfer(t2, r2, 10) || r2[2] == 0x00) { break; }
      }
      uint8_t stx[32] = {0xa7, 0x00, 0x00}, srx[32] = {};
      if (xfer(stx, srx, 32)) {
        sys_hdr[0] = srx[4];
        sys_hdr[1] = srx[5];
        if (sys_hdr[0] == 0xa5 && sys_hdr[1] == 0x01) {
          scr_x = uint16_t(srx[4 + 0x14]) | (uint16_t(srx[4 + 0x15]) << 8);
          scr_y = uint16_t(srx[4 + 0x16]) | (uint16_t(srx[4 + 0x17]) << 8);
          zizhan_outer_touch::configure_sysinfo(tracker, scr_x, scr_y);
        }
      }
      const uint8_t scan[3] = {0xa0, 0x00, 0x01};
      (void)xfer(scan, nullptr, 3);
      IOSleep(50);
      for (unsigned d = 0; d < 16 && !(irq.r(4) & 1u); ++d) {
        uint8_t t2[10] = {0x87}, r2[10] = {};
        if (!xfer(t2, r2, 10) || r2[2] == 0x00) { break; }
      }
    }
    auto cur_map_mode = [&]() {
      return (__atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE) == 2u)
          ? zizhan_outer_touch::MapMode::DesktopViewport
          : zizhan_outer_touch::MapMode::ClockWakeOnly;
    };
    const bool toggle_ok = fold::allow_doubletap != 0u;
    auto pocket_now = [&]() -> bool {
      return fold::allow_pocket_guard != 0u && zizhan_slpi_proximity_near() != 0;
    };
    unsigned home_swipes = 0, vol_clicks = 0;
    IOLog("ZIZHAN_OUTER_TOUCH_LIVE: UP ok=%u id=%04x sysinfo=%02x%02x/%ux%u super_res=%u max=%ux%u irq=%u map_mode=%u toggle=%u pocket_guard=%u\n",
          (spi_ready && chip_id == 0x3640u) ? 1u : 0u, chip_id, sys_hdr[0], sys_hdr[1], scr_x, scr_y,
          tracker.super_res ? 1u : 0u, tracker.max_x, tracker.max_y, irq.r(4) & 1u, unsigned(cur_map_mode()),
          toggle_ok ? 1u : 0u, fold::allow_pocket_guard ? 1u : 0u);
    auto dispatch_step = [&](const zizhan_outer_touch::StepResult &sr) {
      if (sr.pocket_blocked) {
        IOLog("ZIZHAN_OUTER_TOUCH_LIVE: pocket-guard blocked double-tap blocks=%u\n", tracker.pocket_blocks);
        return;
      }
      if (sr.toggle_desktop) {
        fold::switch_to_desktop();
        return;
      }
      if (sr.bar_action == zizhan_outer_touch::BarAction::VolDown) {
        (void)pong_volume_key_sample(2u, 1, 0u);
        (void)pong_volume_key_sample(2u, 0, 0u);
        ++vol_clicks;
        return;
      }
      if (sr.bar_action == zizhan_outer_touch::BarAction::VolUp) {
        (void)pong_volume_key_sample(1u, 1, 0u);
        (void)pong_volume_key_sample(1u, 0, 0u);
        ++vol_clicks;
        return;
      }
      if (sr.bar_action == zizhan_outer_touch::BarAction::Home) {
        /* Edge-tagged Home-bar swipe-up on dedicated finger 9 (y >= 62790 triggers DIGITIZER_FROM_EDGE_TIP). */
        pong_touch_post(1u, 9u, 32768u, 65200u);
        IOSleep(18);
        pong_touch_post(2u, 9u, 32768u, 55000u);
        IOSleep(18);
        pong_touch_post(2u, 9u, 32768u, 42000u);
        IOSleep(18);
        pong_touch_post(2u, 9u, 32768u, 28000u);
        IOSleep(12);
        pong_touch_post(3u, 9u, 32768u, 28000u);
        ++home_swipes;
        return;
      }
      if (sr.toggle_clock) {
        fold::switch_to_clock();
        return;
      }
      if (sr.refresh_hud) {
        __atomic_store_n(&fold::hud_dirty, 1u, __ATOMIC_RELEASE);
        if (__atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE) == 1u) {
          fold::draw(true);
        }
      }
      for (uint8_t k = 0; k < sr.count; ++k) {
        const auto &op = sr.ops[k];
        pong_touch_post(op.type, op.finger, op.x, op.y);
      }
      if (sr.rearm_scan && spi_ready) {
        const uint8_t scan[3] = {0xa0, 0x00, 0x01};
        (void)xfer(scan, nullptr, 3);
      }
    };
    auto encode_pt = [&](uint8_t opcode, uint32_t x, uint32_t y, uint8_t out[8]) {
      memset(out, 0, 8);
      out[0] = opcode;
      out[1] = 0x01;
      out[6] = (opcode == 0x33u) ? 0x00u : 0x20u;
      if (tracker.super_res) {
        out[2] = uint8_t(x); out[3] = uint8_t(x >> 8);
        out[4] = uint8_t(y); out[5] = uint8_t(y >> 8);
      } else {
        out[2] = uint8_t(x);
        out[3] = uint8_t(((x >> 8) & 0xfu) | ((y & 0xfu) << 4));
        out[4] = uint8_t(y >> 4);
      }
    };
    const uint64_t up_ms = fold::mono_ms();
    bool synth_done = false;
    unsigned synth_stage = 0;
    while (__atomic_load_n(&fold::lid_closed, __ATOMIC_ACQUIRE)) {
      if (spi_ready && !(irq.r(4) & 1u)) {
        for (unsigned n = 0; n < 16 && !(irq.r(4) & 1u); ++n) {
          uint8_t t2[10] = {0x87}, r2[10] = {};
          if (!xfer(t2, r2, 10) || r2[2] == 0x00) { break; }
          const uint64_t ev_ms = fold::mono_ms();
          const auto sr = zizhan_outer_touch::step(tracker, &r2[2], cur_map_mode(), ev_ms, toggle_ok, pocket_now());
          dispatch_step(sr);
        }
      }
      const uint64_t elapsed_ms = fold::mono_ms() - up_ms;
      if (outer_touch_synth == 1u && !synth_done && elapsed_ms >= 2000ull) {
        synth_done = true;
        const uint32_t sx = tracker.max_x / 5u;
        const uint32_t sy = uint32_t((uint64_t(zizhan_outer_touch::kDesktopY0 + 180u) * tracker.max_y) / zizhan_outer_touch::kPanelH);
        const uint32_t sy2 = sy + (tracker.super_res ? (tracker.max_y / 40u) : 20u);
        uint8_t e1[8], e2[8], e3[8];
        encode_pt(0x13u, sx, sy, e1);
        encode_pt(0x23u, sx, sy2, e2);
        encode_pt(0x33u, sx, sy2, e3);
        dispatch_step(zizhan_outer_touch::step(tracker, e1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
        IOSleep(40);
        dispatch_step(zizhan_outer_touch::step(tracker, e2, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
        IOSleep(40);
        dispatch_step(zizhan_outer_touch::step(tracker, e3, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
        IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth injected mode=%u events=%u posted=%u hud_taps=%u toggles=%u\n",
              unsigned(cur_map_mode()), tracker.events, tracker.posted, tracker.hud_taps, tracker.mode_toggles);
      } else if (outer_touch_synth == 2u) {
        if (synth_stage == 0u && elapsed_ms >= 4000ull) {
          synth_stage = 1u;
          const uint32_t cx = tracker.max_x / 2u, cy = tracker.max_y / 2u;
          uint8_t d1[8], u1[8];
          encode_pt(0x13u, cx, cy, d1);
          encode_pt(0x33u, cx, cy, u1);
          dispatch_step(zizhan_outer_touch::step(tracker, d1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(60);
          dispatch_step(zizhan_outer_touch::step(tracker, u1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(150);
          dispatch_step(zizhan_outer_touch::step(tracker, d1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(60);
          dispatch_step(zizhan_outer_touch::step(tracker, u1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth2 stage=1 double-tap toggles=%u active_view=%u\n",
                tracker.mode_toggles, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        } else if (synth_stage == 1u && elapsed_ms >= 12000ull) {
          synth_stage = 2u;
          const uint32_t sx = tracker.max_x / 5u;
          const uint32_t sy = uint32_t((uint64_t(zizhan_outer_touch::kDesktopY0 + 180u) * tracker.max_y) / zizhan_outer_touch::kPanelH);
          const uint32_t sy2 = sy + (tracker.super_res ? (tracker.max_y / 40u) : 20u);
          uint8_t e1[8], e2[8], e3[8];
          encode_pt(0x13u, sx, sy, e1);
          encode_pt(0x23u, sx, sy2, e2);
          encode_pt(0x33u, sx, sy2, e3);
          dispatch_step(zizhan_outer_touch::step(tracker, e1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(40);
          dispatch_step(zizhan_outer_touch::step(tracker, e2, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(40);
          dispatch_step(zizhan_outer_touch::step(tracker, e3, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth2 stage=2 desktop-drag posted=%u active_view=%u\n",
                tracker.posted, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        } else if (synth_stage == 2u && elapsed_ms >= 19000ull) {
          synth_stage = 3u;
          const uint32_t px = tracker.max_x / 2u;
          const uint32_t py = uint32_t((1930ull * tracker.max_y) / zizhan_outer_touch::kPanelH);
          uint8_t d1[8], u1[8];
          encode_pt(0x13u, px, py, d1);
          encode_pt(0x33u, px, py, u1);
          dispatch_step(zizhan_outer_touch::step(tracker, d1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(60);
          dispatch_step(zizhan_outer_touch::step(tracker, u1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth2 stage=3 lock-pill toggles=%u active_view=%u\n",
                tracker.mode_toggles, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        }
      } else if (outer_touch_synth == 3u) {
        /* Full pocket-guard + 4-pill quick bar self-test:
         * stage 1 (3 s): simulate pocket near=1 -> double-tap blocked (stays active_view=1).
         * stage 2 (7 s): clear pocket near=0 -> double-tap wakes Desktop (active_view=2).
         * stage 3 (14 s): tap VOL-, tap HOME, tap VOL+ on bottom quick bar.
         * stage 4 (22 s): tap CLOCK pill -> returns to Clock & standby (active_view=1). */
        auto tap_at = [&](uint32_t panel_x, uint32_t panel_y) {
          const uint32_t rx = uint32_t((uint64_t(panel_x) * tracker.max_x) / zizhan_outer_touch::kPanelW);
          const uint32_t ry = uint32_t((uint64_t(panel_y) * tracker.max_y) / zizhan_outer_touch::kPanelH);
          uint8_t d1[8], u1[8];
          encode_pt(0x13u, rx, ry, d1);
          encode_pt(0x33u, rx, ry, u1);
          dispatch_step(zizhan_outer_touch::step(tracker, d1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(55);
          dispatch_step(zizhan_outer_touch::step(tracker, u1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
        };
        if (synth_stage == 0u && elapsed_ms >= 3000ull) {
          synth_stage = 1u;
          zizhan_slpi_set_prox_sim(1);
          tap_at(540u, 1260u);
          IOSleep(140);
          tap_at(540u, 1260u);
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth3 stage=1 pocket-near blocks=%u active_view=%u\n",
                tracker.pocket_blocks, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        } else if (synth_stage == 1u && elapsed_ms >= 7000ull) {
          synth_stage = 2u;
          zizhan_slpi_set_prox_sim(-1);
          tap_at(540u, 1260u);
          IOSleep(140);
          tap_at(540u, 1260u);
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth3 stage=2 pocket-clear toggles=%u active_view=%u\n",
                tracker.mode_toggles, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        } else if (synth_stage == 2u && elapsed_ms >= 14000ull) {
          synth_stage = 3u;
          tap_at(156u, 1930u);   /* VOL- */
          IOSleep(250);
          tap_at(390u, 1930u);   /* HOME */
          IOSleep(250);
          tap_at(900u, 1930u);   /* VOL+ */
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth3 stage=3 quick-bar bar_actions=%u vol_clicks=%u home_swipes=%u\n",
                tracker.bar_actions, vol_clicks, home_swipes);
        } else if (synth_stage == 3u && elapsed_ms >= 22000ull) {
          synth_stage = 4u;
          tap_at(640u, 1930u);   /* CLOCK / LOCK */
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth3 stage=4 clock-pill toggles=%u active_view=%u\n",
                tracker.mode_toggles, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        }
      } else if (outer_touch_synth == 4u) {
        /* Cover Clock swipe-up wake + lower-chin swipe-up Home/Unlock self-test:
         * stage 1 (3 s): pocket near=1 -> swipe-up blocked (stays active_view=1).
         * stage 2 (7 s): pocket near=0 -> swipe-up wakes Desktop (active_view=2, swipe_wakes=1).
         * stage 3 (14 s): lower-chin swipe-up (y=2200 -> 1650) triggers Home/Unlock swipe (swipe_homes=1).
         * stage 4 (21 s): tap CLOCK pill -> returns to Clock standby (active_view=1). */
        auto swipe_panel = [&](uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1) {
          const uint32_t rx0 = uint32_t((uint64_t(x0) * tracker.max_x) / zizhan_outer_touch::kPanelW);
          const uint32_t ry0 = uint32_t((uint64_t(y0) * tracker.max_y) / zizhan_outer_touch::kPanelH);
          const uint32_t rxm = (rx0 + uint32_t((uint64_t(x1) * tracker.max_x) / zizhan_outer_touch::kPanelW)) / 2u;
          const uint32_t rym = (ry0 + uint32_t((uint64_t(y1) * tracker.max_y) / zizhan_outer_touch::kPanelH)) / 2u;
          const uint32_t rx1 = uint32_t((uint64_t(x1) * tracker.max_x) / zizhan_outer_touch::kPanelW);
          const uint32_t ry1 = uint32_t((uint64_t(y1) * tracker.max_y) / zizhan_outer_touch::kPanelH);
          uint8_t d1[8], m1[8], m2[8], u1[8];
          encode_pt(0x13u, rx0, ry0, d1);
          encode_pt(0x23u, rxm, rym, m1);
          encode_pt(0x23u, rx1, ry1, m2);
          encode_pt(0x33u, rx1, ry1, u1);
          dispatch_step(zizhan_outer_touch::step(tracker, d1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(35);
          dispatch_step(zizhan_outer_touch::step(tracker, m1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(35);
          dispatch_step(zizhan_outer_touch::step(tracker, m2, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(35);
          dispatch_step(zizhan_outer_touch::step(tracker, u1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
        };
        if (synth_stage == 0u && elapsed_ms >= 3000ull) {
          synth_stage = 1u;
          zizhan_slpi_set_prox_sim(1);
          swipe_panel(540u, 2100u, 540u, 1500u);
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth4 stage=1 pocket-swipe blocks=%u active_view=%u\n",
                tracker.pocket_blocks, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        } else if (synth_stage == 1u && elapsed_ms >= 7000ull) {
          synth_stage = 2u;
          zizhan_slpi_set_prox_sim(-1);
          swipe_panel(540u, 2100u, 540u, 1500u);
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth4 stage=2 swipe-wake toggles=%u swipe_wakes=%u active_view=%u\n",
                tracker.mode_toggles, tracker.swipe_wakes, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        } else if (synth_stage == 2u && elapsed_ms >= 14000ull) {
          synth_stage = 3u;
          swipe_panel(540u, 2250u, 540u, 1600u);
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth4 stage=3 chin-home swipe_homes=%u home_swipes=%u\n",
                tracker.swipe_homes, home_swipes);
        } else if (synth_stage == 3u && elapsed_ms >= 21000ull) {
          synth_stage = 4u;
          const uint32_t rx = uint32_t((640ull * tracker.max_x) / zizhan_outer_touch::kPanelW);
          const uint32_t ry = uint32_t((1930ull * tracker.max_y) / zizhan_outer_touch::kPanelH);
          uint8_t d1[8], u1[8];
          encode_pt(0x13u, rx, ry, d1);
          encode_pt(0x33u, rx, ry, u1);
          dispatch_step(zizhan_outer_touch::step(tracker, d1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOSleep(55);
          dispatch_step(zizhan_outer_touch::step(tracker, u1, cur_map_mode(), fold::mono_ms(), toggle_ok, pocket_now()));
          IOLog("ZIZHAN_OUTER_TOUCH_LIVE: synth4 stage=4 clock-pill toggles=%u active_view=%u\n",
                tracker.mode_toggles, __atomic_load_n(&fold::active_view, __ATOMIC_ACQUIRE));
        }
      }
      IOSleep(8);
    }
    zizhan_outer_touch::StepResult rel{};
    zizhan_outer_touch::release_all(tracker, rel);
    for (uint8_t k = 0; k < rel.count; ++k) {
      pong_touch_post(rel.ops[k].type, rel.ops[k].finger, rel.ops[k].x, rel.ops[k].y);
    }
    rst.w(4, 0);
    IOSleep(2);
    const bool r1 = wr8(0x0e, en0), r2 = wr8(0x04, dv0), r3 = wr8(0x06, av0);
    for (unsigned i = 0; i < 4; ++i) { pads[i].w(0, saved_pads[i]); }
    if (!(vote10_0 & (1u << 8))) {
      vote.w(0x10, vote.r(0x10) & ~(1u << 8));
    }
    const unsigned end_irq = irq.r(4) & 1u;
    rst.w(0, s_rst); rst.w(4, s_rst_io); irq.w(0, s_irq);
    IOLog("ZIZHAN_OUTER_TOUCH_LIVE: DOWN events=%u posted=%u hud_taps=%u toggles=%u swipes=%u/%u blocks=%u bar=%u vol=%u home=%u restored=%u irq_end=%u\n",
          tracker.events, tracker.posted, tracker.hud_taps, tracker.mode_toggles,
          tracker.swipe_wakes, tracker.swipe_homes,
          tracker.pocket_blocks, tracker.bar_actions, vol_clicks, home_swipes, (r1 && r2 && r3) ? 1u : 0u, end_irq);
  }
}
#endif

static uint32_t probe_mode;
static void worker(void *, wait_result_t) {
  IOSleep(40000);
  if (probe_mode >= 2) { if (probe_mode == 2) picture_test(); else touch_inventory(probe_mode >= 4, probe_mode == 5); thread_terminate(current_thread()); thread_block(THREAD_CONTINUE_NULL); return; }
  {
    Ro cc, dsi0;
    if (!cc.open(0xaf00000, 0x10000) || !dsi0.open(0xae94000, 0x1000)) {
      IOLog("ZIZHAN_OUTER_PROBE: FAIL map\n");
    } else {
      const uint32_t byte1 = cc.r(0x8030);
      IOLog("ZIZHAN_OUTER_PROBE: dispcc mdss_gdsc=%x byte0=%x pclk0=%x byte1=%x byte1_intf=%x esc1=%x pclk1=%x\n", cc.r(0x9000),
            cc.r(0x8028), cc.r(0x8004), byte1, cc.r(0x8034), cc.r(0x803c), cc.r(0x8008));
      IOLog("ZIZHAN_OUTER_PROBE: dsi0 hw_version=%x ctrl=%x\n", dsi0.r(0x0), dsi0.r(0x4));
      static const uint16_t pins[] = {0, 116, 125, 93, 60};
      for (uint16_t pin : pins) {
        Ro p;
        if (!p.open(0xf100000ull + uint64_t(pin) * 0x1000, 0x1000)) continue;
        const uint32_t cfg = p.r(0), io = p.r(4);
        IOLog("ZIZHAN_OUTER_PROBE: pin=%u cfg=%x func=%u oe=%u pull=%u in=%u out=%u\n", pin, cfg, (cfg >> 2) & 15, (cfg >> 9) & 1,
              cfg & 3, io & 1, (io >> 1) & 1);
      }
      for (unsigned te_pin = 86; te_pin <= 87; ++te_pin) {   // inner / outer TE (mdp_vsync)
        Ro p;
        if (!p.open(0xf100000ull + uint64_t(te_pin) * 0x1000, 0x1000)) continue;
        unsigned edges = 0, last = p.r(4) & 1u;
        for (unsigned i = 0; i < 5000; ++i) { const unsigned v = p.r(4) & 1u; if (v != last) ++edges; last = v; IODelay(20); }
        IOLog("ZIZHAN_OUTER_PROBE: te gpio%u cfg=%x edges=%u over ~100ms\n", te_pin, p.r(0), edges);
      }
      if (byte1 & 0x80000000u) {
        IOLog("ZIZHAN_OUTER_PROBE: DONE dsi1=clock-off dsi1_read=0 writes=0\n");
      } else {
        Ro dsi1;
        if (dsi1.open(0xae96000, 0x1000))
          IOLog("ZIZHAN_OUTER_PROBE: dsi1 hw_version=%x ctrl=%x phy_rev=%x\n", dsi1.r(0x0), dsi1.r(0x4), dsi1.r(0x400));
        IOLog("ZIZHAN_OUTER_PROBE: DONE dsi1=clock-on dsi1_read=1 writes=0\n");
      }
    }
  }
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start() {
  uint32_t mode = 0;
  const bool have_probe = PE_parse_boot_argn("zizhan_outer_probe", &mode, sizeof mode) && mode != 0;
  uint32_t dtap = 1, pguard = 1, odim = 1;
  if (PE_parse_boot_argn("zizhan_outer_doubletap", &dtap, sizeof dtap)) {
    fold::allow_doubletap = dtap ? 1u : 0u;
  }
  if (PE_parse_boot_argn("zizhan_outer_pocket", &pguard, sizeof pguard)) {
    fold::allow_pocket_guard = pguard ? 1u : 0u;
  }
  if (PE_parse_boot_argn("zizhan_outer_dim", &odim, sizeof odim)) {
    fold::allow_outer_dim = odim ? 1u : 0u;
  }
  const uint32_t fmode = fold::current_mode();
  if (fmode == 1u || fmode == 2u) {
    thread_t td = THREAD_NULL;
    if (kernel_thread_start(fold::cover_desktop_worker, nullptr, &td) == KERN_SUCCESS) {
      thread_deallocate(td);
    }
    IOLog("ZIZHAN_OUTER_DESKTOP: READY mode=%u doubletap=%u\n", fmode, fold::allow_doubletap);
  }
#if XNU_PONG_WIFI || XNU_PONG_POWER
  uint32_t touch_on = 0;
  if ((!have_probe || mode < 4u) &&
      zizhan_daily::arg("zizhan_outer_touch", &touch_on, zizhan_daily::kOuterTouch) && touch_on == 1u) {
    (void)PE_parse_boot_argn("zizhan_outer_touch_synth", &outer_touch_synth, sizeof outer_touch_synth);
    thread_t tt = THREAD_NULL;
    if (kernel_thread_start(outer_touch_worker, nullptr, &tt) == KERN_SUCCESS) {
      thread_deallocate(tt);
    }
    IOLog("ZIZHAN_OUTER_TOUCH_LIVE: READY lid-gated=1 synth=%u\n", outer_touch_synth);
  }
#endif
  if (!have_probe) { return; }
  if (mode < 1 || mode > 5) { IOLog("ZIZHAN_OUTER_PROBE: SKIP unsupported-mode=%u\n", mode); return; }
  probe_mode = mode;
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) == KERN_SUCCESS) { thread_deallocate(t); }
  IOLog("ZIZHAN_OUTER_PROBE: RUNNING mode=%u readonly=%u\n", mode, (mode == 1 || mode == 3) ? 1u : 0u);
}
} // namespace zizhan_outer_probe
