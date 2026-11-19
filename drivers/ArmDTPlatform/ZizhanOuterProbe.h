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

static void worker(void *, wait_result_t) {
  IOSleep(40000);
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
  if (mode != 1) { IOLog("ZIZHAN_OUTER_PROBE: SKIP unsupported-mode=%u\n", mode); return; }
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, nullptr, &t) == KERN_SUCCESS) thread_deallocate(t);
  IOLog("ZIZHAN_OUTER_PROBE: RUNNING mode=1 readonly=1\n");
}
} // namespace zizhan_outer_probe
