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
  Win vote, wrap, camcc, cam_ahb;
  if (!vote.open(0x162000, 0x1000, false) || !wrap.open(0x127000, 0x1000, false) || !cam_ahb.open(0x136000, 0x1000, false)) {
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
  // CAM_CC is only touched when GCC_CAMERA_AHB_CLK (0x136004, Linux keeps it always on) runs.
  const uint32_t ahb = cam_ahb.r(0x004), xo = cam_ahb.r(0x020);
  IOLog("ZIZHAN_CAM_PROBE: gcc camera_ahb=%x camera_xo=%x\n", ahb, xo);
  if ((ahb & 0x80000000u) || !camcc.open(0xade0000, 0x20000, false)) { IOLog("ZIZHAN_CAM_PROBE: DONE inventory camcc_read=0 writes=0\n"); return; }
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

/* One camera module, from the zizhan live DT (regulator names W/F/U/T = wide/front/ultra/tele). vdig_ldo 0 means the
 * digital rail is a GPIO regulator (vdig_gpio). All share vio = PM8008J L5 1.8 V. */
struct Sensor {
  const char *name;
  unsigned vana_gpio, vdig_ldo, vdig_mv, vdig_gpio;
  uint32_t mclk_rcg, mclk_cbcr; unsigned mclk_pin, reset_pin;
  uint64_t cci_base; uint32_t cci_rcg, cci_cbcr; unsigned master, pad0, pad1;
  uint8_t addrs[3]; uint16_t id_reg; unsigned id_len;
  unsigned extra_gpio;   /* second analog rail as a GPIO regulator (main: CAMERA_W_AVDD1 GPIO70 1.8 V), 0 = none */
};
static const Sensor kSensors[] = {
  // mode 2: CCI1 / cam-sensor3 = IMX663 tele (cam-1: 0x1a -> 0x0663)
  {"tele", 53, 1, 1056, 0, 0x15054, 0x1506c, 103, 128, 0xac16000, 0x13148, 0x13160, 0, 114, 115, {0x1a, 0x10, 0}, 0x0016, 2, 0},
  // mode 3: CCI0 master 1 / cam-sensor1 = front IMX596: vdig PM8008J L2 1.152 V, vana GPIO97 2.9 V, MCLK5 GPIO105, reset GPIO127
  {"front", 97, 2, 1152, 0, 0x1508c, 0x150a4, 105, 127, 0xac15000, 0x1312c, 0x13144, 1, 112, 113, {0x10, 0x1a, 0x37}, 0x0016, 2, 0},
  // mode 4: CCI0 master 1 / cam-sensor2 = ultra OV13B10: vdig GPIO92 1.2 V, vana GPIO6 2.8 V, MCLK1 GPIO101, reset GPIO24
  {"ultra", 6, 0, 0, 92, 0x1501c, 0x15034, 101, 24, 0xac15000, 0x1312c, 0x13144, 1, 112, 113, {0x36, 0x10, 0}, 0x300a, 3, 0},
  // mode 5: CCI0 master 0 / cam-sensor0 = main IMX766: vdig GPIO68 1.104 V, vana GPIO42 2.8 V, avdd1 GPIO70 1.8 V
  // (vana1 = pm8350_s12, input of the DVDD regulators, already on), MCLK2/GPIO102, reset GPIO126; AF/OIS rails not needed for the id
  {"main", 42, 0, 0, 68, 0x15038, 0x15050, 102, 126, 0xac15000, 0x1312c, 0x13144, 0, 110, 111, {0x1a, 0x10, 0}, 0x0016, 2, 70},
};

static void attended(const Sensor &sn) {
  Win vote, wrap, se, camcc, cci, pm_en, vana, vdig, mclk, rst, pad[2], i2c[2], extra;
  if (!vote.open(0x162000, 0x1000, true) || !wrap.open(0x127000, 0x1000, true) || !se.open(0x994000, 0x4000, true) ||
      !camcc.open(0xade0000, 0x20000, true) || !cci.open(sn.cci_base, 0x1000, true)) { IOLog("ZIZHAN_CAM_PROBE: FAIL map\n"); return; }
  {
    Win cam_ahb;
    if (!cam_ahb.open(0x136000, 0x1000, true)) { IOLog("ZIZHAN_CAM_PROBE: FAIL map\n"); return; }
    if (cam_ahb.r(0x004) & 0x80000000u) { cam_ahb.w(0x004, cam_ahb.r(0x004) | 1u); IODelay(100); }
    if (cam_ahb.r(0x004) & 0x80000000u) { IOLog("ZIZHAN_CAM_PROBE: FAIL camera-ahb-off\n"); return; }
  }
  auto pin_open = [](Win &w, unsigned n) { return w.open(0xf100000ull + uint64_t(n) * 0x1000, 0x1000, true); };
  if (!pin_open(pm_en, 11) || !pin_open(vana, sn.vana_gpio) || (sn.vdig_gpio && !pin_open(vdig, sn.vdig_gpio)) ||
      !pin_open(mclk, sn.mclk_pin) || !pin_open(rst, sn.reset_pin) || !pin_open(pad[0], sn.pad0) || !pin_open(pad[1], sn.pad1) ||
      !pin_open(i2c[0], 206) || !pin_open(i2c[1], 207) || (sn.extra_gpio && !pin_open(extra, sn.extra_gpio))) { IOLog("ZIZHAN_CAM_PROBE: FAIL pin-map\n"); return; }
  const uint32_t s_pm = pm_en.r(0), s_vana = vana.r(0), s_vdig = sn.vdig_gpio ? vdig.r(0) : 0, s_mclk = mclk.r(0), s_rst = rst.r(0),
                 s_pad0 = pad[0].r(0), s_pad1 = pad[1].r(0), s_i2c0 = i2c[0].r(0), s_i2c1 = i2c[1].r(0),
                 s_extra = sn.extra_gpio ? extra.r(0) : 0;
  IOLog("ZIZHAN_CAM_PROBE: %s start vana%u=%x vdig%u=%x mclk%u=%x reset%u=%x pads%u/%u=%x/%x\n", sn.name, sn.vana_gpio, s_vana,
        sn.vdig_gpio, s_vdig, sn.mclk_pin, s_mclk, sn.reset_pin, s_rst, sn.pad0, sn.pad1, s_pad0, s_pad1);
  const uint32_t i2c_cfg[2] = {s_i2c0, s_i2c1};
  for (uint32_t v : i2c_cfg)
    if ((v & (1u << 11)) && !(v & (1u << 12))) { IOLog("ZIZHAN_CAM_PROBE: SKIP i2c-pin-not-apps cfg=%x\n", v); return; }
  vote.w(0x8, vote.r(0x8) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 15));
  if (!(wrap.r(0x62c) & 1u)) { wrap.w(0x61c, (wrap.r(0x61c) & ~0x1037ffu) | 1u); wrap.w(0x618, wrap.r(0x618) | 1u); }
  bool on = false;
  for (unsigned i = 0; i < 1000 && !on; ++i) {
    const uint32_t m = wrap.r(0x004), s = wrap.r(0x008);
    on = !(wrap.r(0x610) & 0x80000000u) && (!(m & 0x80000000u) || (m & 2u)) && (!(s & 0x80000000u) || (s & 2u));
    if (!on) IODelay(100);
  }
  if (!on) { IOLog("ZIZHAN_CAM_PROBE: FAIL wrap0-s5-clock\n"); return; }
  for (unsigned i = 0; i < 2; ++i) i2c[i].w(0, (i2c[i].r(0) & ~0x3ffu) | (1u << 2) | 3u);
  if (!(se.r(0x48) & 1u) || !se.r(0x278)) { se.w(0x48, (7u << 4) | 1u); se.w(0x278, (10u << 20) | (11u << 10) | 26u); }
  I2cCtx x{}; x.se = &se;
  const GeniIo io = {se_read, se_write, &se};
  uint32_t config = 0;
  if (geni_i2c_prepare_poll(&x.g, &io, 0x0d, &config)) {
    IOLog("ZIZHAN_CAM_PROBE: FAIL i2c-prepare config=%x\n", config);
  } else {
    gpio_out(pm_en, 1);
    gpio_out(rst, 0);
    IOSleep(5);
    const ZizhanPm8008Ops pm = {&x, pm_read, pm_write};
    const ZizhanPm8008Rail vio = {5, 1800}, dig = {sn.vdig_ldo, sn.vdig_mv};
    unsigned mv = 0; int en = 0, rc = 0, crc = -1, ccirc = -1;
    if (sn.vdig_ldo) {
      rc = zizhan_pm8008_read_ldo(&pm, sn.vdig_ldo, &mv, &en);
      IOLog("ZIZHAN_CAM_PROBE: %s pm8008j l%u rc=%d mv=%u en=%d\n", sn.name, sn.vdig_ldo, rc, mv, en);
    }
    unsigned done = 0, stage = 0;
    if (!rc) rc = zizhan_pm8008_rails_on(&pm, &vio, 1, &done);                 // vio
    if (!rc) { gpio_out(vana, 1); if (sn.extra_gpio) gpio_out(extra, 1); IOSleep(1); }   // vana (+ second analog rail)
    if (!rc) {                                                                 // vdig
      if (sn.vdig_ldo) rc = zizhan_pm8008_rails_on(&pm, &dig, 1, &done);
      else { gpio_out(vdig, 1); IOSleep(1); }
    }
    IOLog("ZIZHAN_CAM_PROBE: %s rails rc=%d\n", sn.name, rc);
    ZizhanCamCcOps cc = {&camcc, win_read, win_write, pause_us, 1000};
    if (!rc) crc = zizhan_camcc_mclk_on(&cc, sn.mclk_rcg, sn.mclk_cbcr, &stage);
    if (!crc) { mclk.w(0, (s_mclk & ~0x3ffu) | (1u << 2)); IOSleep(1); gpio_out(rst, 1); IOSleep(10); }
    if (!crc) ccirc = zizhan_cci_clocks_on_for(&cc, sn.cci_rcg, sn.cci_cbcr);
    if (!crc && !ccirc) {
      for (unsigned i = 0; i < 2; ++i) pad[i].w(0, ((i ? s_pad1 : s_pad0) & ~0x3ffu) | (1u << 2) | 3u);
      ZizhanCciOps co = {&cci, win_read, win_write, pause_us, 2000};
      ccirc = zizhan_cci_reset_init_m(&co, sn.master);
      for (uint8_t a : sn.addrs) {
        if (!a) continue;
        uint8_t id[3] = {0, 0, 0};
        int r = ccirc ? ccirc : zizhan_cci_read16_m(&co, sn.master, a, sn.id_reg, id, sn.id_len);
        if (r && !ccirc) (void)zizhan_cci_reset_init_m(&co, sn.master);   // a NACK leaves the queue halted: reset before the next address
        IOLog("ZIZHAN_CAM_PROBE: %s cci m%u addr=%x reg=%x rc=%d id=%02x%02x%02x\n", sn.name, sn.master, a, sn.id_reg, r, id[0], id[1], id[2]);
        if (!r) break;
      }
      pad[0].w(0, s_pad0); pad[1].w(0, s_pad1);
    }
    IOLog("ZIZHAN_CAM_PROBE: %s mclk rc=%d stage=%u cci_clocks=%d\n", sn.name, crc, stage, ccirc);
    gpio_out(rst, 0);
    mclk.w(0, s_mclk);
    if (!crc) {
      const uint32_t cci_branches[] = {sn.cci_cbcr, 0x131acu, 0x13164u};
      for (uint32_t br : cci_branches) (void)zizhan_camcc_branch(&cc, br, 0);
      camcc.w(0x131f0u, camcc.r(0x131f0u) & ~1u);
      (void)zizhan_camcc_mclk_off(&cc, sn.mclk_cbcr);
    }
    if (sn.vdig_ldo) (void)zizhan_pm8008_enable(&pm, sn.vdig_ldo, 0); else gpio_out(vdig, 0);
    if (sn.extra_gpio) gpio_out(extra, 0);
    gpio_out(vana, 0);
    (void)zizhan_pm8008_enable(&pm, 5, 0);
  }
  pm_en.w(0, s_pm); vana.w(0, s_vana); if (sn.vdig_gpio) vdig.w(0, s_vdig); mclk.w(0, s_mclk); rst.w(0, s_rst);
  i2c[0].w(0, s_i2c0); i2c[1].w(0, s_i2c1); if (sn.extra_gpio) extra.w(0, s_extra);
  IOLog("ZIZHAN_CAM_PROBE: DONE %s rails_off=1 pins_restored=1\n", sn.name);
}

static void worker(void *arg, wait_result_t) {
  const uint32_t mode = uint32_t(uintptr_t(arg));
  IOSleep(40000);   // after the desktop and Wi-Fi
  if (mode == 1) inventory(); else attended(kSensors[mode - 2]);
  thread_terminate(current_thread());
  thread_block(THREAD_CONTINUE_NULL);
}

static void start() {
  uint32_t mode = 0;
  if (!PE_parse_boot_argn("zizhan_cam_probe", &mode, sizeof mode) || !mode) return;
  uint32_t present = 0;
  if (mode >= 2 && (!PE_parse_boot_argn("zizhan_user_present", &present, sizeof present) || present != 1)) {
    IOLog("ZIZHAN_CAM_PROBE: SKIP mode=2 needs zizhan_user_present=1\n");
    return;
  }
  if (mode > 5) { IOLog("ZIZHAN_CAM_PROBE: SKIP unsupported-mode=%u\n", mode); return; }
  thread_t t = THREAD_NULL;
  if (kernel_thread_start(worker, reinterpret_cast<void *>(uintptr_t(mode)), &t) == KERN_SUCCESS) thread_deallocate(t);
  IOLog("ZIZHAN_CAM_PROBE: RUNNING mode=%u\n", mode);
}
} // namespace zizhan_cam_probe
