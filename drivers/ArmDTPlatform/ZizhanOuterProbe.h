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
#include <kern/clock.h>
#include "../PongPower/GeniI2c.h"

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

/* zizhan_outer_fold=1 (FOLD-01 step 2): the outer panel follows the lid. The loader keeps CTL1/INTF_2 scanning VIG2 from
 * 0xb8fc9900 (1080x2520, stride 4320, no-map splash tail); closed draws a big HH:MM (UTC+8) in white seven-segment digits on
 * black and refreshes it every 30 s, open clears to black. The INTF_2 MISR after each draw is logged as the closed-loop check. */
namespace fold {
/* Kept for the life of the system (no function-local statics: the kernel has no guard variables). */
static IOMemoryMap *fb_map, *intf_map;
static volatile uint8_t *fb_va, *intf_va;
static bool ready, misr_on;
static volatile uint8_t *map_once(uint64_t base, uint64_t bytes, IOOptionBits cache, IOMemoryMap **out) {
  auto m = IODeviceMemory::withRange(base, bytes);
  *out = m ? m->map(kIOMapAnywhere | cache) : nullptr;
  if (m) m->release();
  return *out ? reinterpret_cast<volatile uint8_t *>((*out)->getVirtualAddress()) : nullptr;
}
static uint32_t ir(uint32_t at) { const uint32_t v = *reinterpret_cast<volatile uint32_t *>(intf_va + at); __asm__ volatile("dmb oshld" ::: "memory"); return v; }
static void iw(uint32_t at, uint32_t v) { *reinterpret_cast<volatile uint32_t *>(intf_va + at) = v; __asm__ volatile("dsb sy" ::: "memory"); }
static bool init() {
  if (ready) return true;
  const uint64_t base = 0xb8fc9900ull, page = base & ~0xfffull, skew = base - page;
  {
    Rw mdss;
    if (!mdss.open(0xae00000, 0x84000, kIOMapInhibitCache)) return false;
    if (mdss.r(0x9000 + 0x14) != uint32_t(base)) { IOLog("ZIZHAN_OUTER_FOLD: SKIP vig2 not on the outer buffer\n"); return false; }
  }
  fb_va = map_once(page, ((skew + 2520ull * 4320) + 0xfff) & ~0xfffull, kIOMapWriteCombineCache, &fb_map);
  intf_va = map_once(0xae37000, 0x1000, kIOMapInhibitCache, &intf_map);
  ready = fb_va && intf_va;
  return ready;
}
static inline volatile uint32_t *row(unsigned y) { return reinterpret_cast<volatile uint32_t *>(fb_va + (0xb8fc9900ull & 0xfffull) + uint64_t(y) * 4320); }
static void rect(unsigned x0, unsigned y0, unsigned w, unsigned h, uint32_t c) {
  for (unsigned y = y0; y < y0 + h && y < 2520; ++y) { volatile uint32_t *r = row(y); for (unsigned x = x0; x < x0 + w && x < 1080; ++x) r[x] = c; }
}
/* seven segments a..g of a W x H digit at (x, y), stroke t */
static void digit(unsigned d, unsigned x, unsigned y, unsigned W, unsigned H, unsigned t, uint32_t c) {
  static const uint8_t seg[10] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f};
  const uint8_t m = seg[d % 10]; const unsigned h2 = H / 2;
  if (m & 0x01) rect(x, y, W, t, c);                       // a
  if (m & 0x02) rect(x + W - t, y, t, h2, c);              // b
  if (m & 0x04) rect(x + W - t, y + h2, t, H - h2, c);     // c
  if (m & 0x08) rect(x, y + H - t, W, t, c);               // d
  if (m & 0x10) rect(x, y + h2, t, H - h2, c);             // e
  if (m & 0x20) rect(x, y, t, h2, c);                      // f
  if (m & 0x40) rect(x, y + h2 - t / 2, W, t, c);          // g
}
static uint32_t misr() {
  if (!misr_on) { iw(0x180, 1u << 10); iw(0x180, 0x1u | (1u << 8) | (1u << 31) | (1u << 24)); misr_on = true; }
  IOSleep(60);
  return (ir(0x180) & (1u << 9)) ? ir(0x184) : 0;
}
static void draw(bool closed) {
  if (!init()) return;
  rect(0, 0, 1080, 2520, 0xff000000u);
  if (closed) {
    clock_sec_t sec = 0; clock_usec_t usec = 0;
    clock_get_calendar_microtime(&sec, &usec);
    const unsigned local = unsigned((sec + 8 * 3600) % 86400), hh = local / 3600, mm = (local / 60) % 60;
    const unsigned W = 190, H = 360, t = 40, y = 980, gap = 40;
    digit(hh / 10, 60, y, W, H, t, 0xffffffffu);
    digit(hh % 10, 60 + W + gap, y, W, H, t, 0xffffffffu);
    rect(540 - t / 2, y + H / 3 - t / 2, t, t, 0xffffffffu);
    rect(540 - t / 2, y + 2 * H / 3 - t / 2, t, t, 0xffffffffu);
    digit(mm / 10, 1080 - 60 - 2 * W - gap, y, W, H, t, 0xffffffffu);
    digit(mm % 10, 1080 - 60 - W, y, W, H, t, 0xffffffffu);
    __asm__ volatile("dsb sy" ::: "memory");
    IOLog("ZIZHAN_OUTER_FOLD: closed clock %02u:%02u misr=%x\n", hh, mm, misr());
  } else {
    __asm__ volatile("dsb sy" ::: "memory");
    IOLog("ZIZHAN_OUTER_FOLD: open black misr=%x\n", misr());
  }
}
}  // namespace fold
extern "C" void zizhan_outer_fold_show(int closed) {
  uint32_t on = 0;
  if (!zizhan_daily::arg("zizhan_outer_fold", &on, zizhan_daily::kOuterFold) || on != 1) return;
  fold::draw(closed != 0);
}

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
  if (!PE_parse_boot_argn("zizhan_outer_probe", &mode, sizeof mode) || !mode) return;
  if (mode < 1 || mode > 5) { IOLog("ZIZHAN_OUTER_PROBE: SKIP unsupported-mode=%u\n", mode); return; }
  probe_mode = mode;
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
  IOLog("ZIZHAN_OUTER_PROBE: RUNNING mode=%u readonly=%u\n", mode, (mode == 1 || mode == 3) ? 1u : 0u);
}
} // namespace zizhan_outer_probe
