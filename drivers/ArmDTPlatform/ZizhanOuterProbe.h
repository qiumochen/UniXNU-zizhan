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

static uint32_t probe_mode;
static void worker(void *, wait_result_t) {
  IOSleep(40000);
  if (probe_mode == 2) { picture_test(); thread_terminate(current_thread()); thread_block(THREAD_CONTINUE_NULL); return; }
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
  if (mode != 1 && mode != 2) { IOLog("ZIZHAN_OUTER_PROBE: SKIP unsupported-mode=%u\n", mode); return; }
  probe_mode = mode;
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
  IOLog("ZIZHAN_OUTER_PROBE: RUNNING mode=%u readonly=%u\n", mode, mode == 1 ? 1u : 0u);
}
} // namespace zizhan_outer_probe
