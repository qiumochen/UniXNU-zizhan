/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CAM-01 prerequisite: CAM_CC steps for one sensor MCLK (host tested, not wired into the kernel yet).
 * Linux camcc-sm8450.c, CAM_CC at 0xade0000 (shares MMCX with the running display):
 *   titan_top_gdsc GDSCR 0x131dc (SW_COLLAPSE bit 0, PWR_ON bit 31; CFG_GDSCR +4 POWER_UP_COMPLETE bit 16),
 *   cam_cc_mclk3_clk_src RCG 0x15054 (CMD, CFG +4, M +8, N +0xc, D +0x10), 19.2 MHz = BI_TCXO (src 0), pre_div 1,
 *   cam_cc_mclk3_clk branch CBCR 0x1506c (CLK_ENABLE bit 0, CLK_OFF bit 31).
 * Order: GDSC on -> RCG 19.2 MHz -> branch on. Off: branch -> GDSC. Every wait is bounded by the caller's poll count. */
#ifndef ZIZHAN_CAM_CC_H
#define ZIZHAN_CAM_CC_H
#include <stdint.h>

enum { ZizhanCamCcOk = 0, ZizhanCamCcArgument, ZizhanCamCcTimeout, ZizhanCamCcState };
enum { ZizhanCamCcBase = 0x0ade0000u, ZizhanCamCcTitanGdscr = 0x131dcu, ZizhanCamCcMclk3Rcg = 0x15054u, ZizhanCamCcMclk3Cbcr = 0x1506cu };

typedef struct {
  void *context;
  uint32_t (*read)(void *context, uint32_t offset);
  void (*write)(void *context, uint32_t offset, uint32_t value);
  void (*pause_us)(void *context, unsigned us);
  unsigned polls;   /* bound for every wait, e.g. 1000 x 10 us */
} ZizhanCamCcOps;

static inline int zizhan_camcc_gdsc(const ZizhanCamCcOps *o, uint32_t gdscr, int on) {
  if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return ZizhanCamCcArgument;
  const uint32_t v = o->read(o->context, gdscr);
  o->write(o->context, gdscr, on ? (v & ~1u) : (v | 1u));
  for (unsigned i = 0; i < o->polls; ++i) {
    const int pwr_on = (o->read(o->context, gdscr) & 0x80000000u) != 0;
    if (pwr_on == !!on) return ZizhanCamCcOk;
    o->pause_us(o->context, 10);
  }
  return ZizhanCamCcTimeout;
}

/* 19.2 MHz from the XO: CFG src 0, hid div field 1 (divide by 1), mode 0, M/N unused. */
static inline int zizhan_camcc_rcg_xo(const ZizhanCamCcOps *o, uint32_t rcg) {
  if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return ZizhanCamCcArgument;
  const uint32_t cfg = o->read(o->context, rcg + 4);
  o->write(o->context, rcg + 4, (cfg & ~0x1037ffu) | 1u);
  o->write(o->context, rcg, o->read(o->context, rcg) | 1u);   /* CMD_UPDATE */
  for (unsigned i = 0; i < o->polls; ++i) {
    if (!(o->read(o->context, rcg) & 1u)) return ZizhanCamCcOk;
    o->pause_us(o->context, 10);
  }
  return ZizhanCamCcTimeout;
}

static inline int zizhan_camcc_branch(const ZizhanCamCcOps *o, uint32_t cbcr, int on) {
  if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return ZizhanCamCcArgument;
  const uint32_t v = o->read(o->context, cbcr);
  o->write(o->context, cbcr, on ? (v | 1u) : (v & ~1u));
  for (unsigned i = 0; i < o->polls; ++i) {
    const int off = (o->read(o->context, cbcr) & 0x80000000u) != 0;
    if (off == !on) return ZizhanCamCcOk;
    o->pause_us(o->context, 10);
  }
  return ZizhanCamCcTimeout;
}

/* Any sensor MCLK at 19.2 MHz (camcc-sm8450: mclk1 0x1501c/0x15034, mclk2 0x15038/0x15050, mclk3 0x15054/0x1506c,
 * mclk5 0x1508c/0x150a4); on any failure the steps already taken are undone in reverse. */
static inline int zizhan_camcc_mclk_on(const ZizhanCamCcOps *o, uint32_t rcg, uint32_t cbcr, unsigned *stage) {
  int rc = zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 1);
  if (stage) *stage = 1;
  if (rc) { (void)zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0); return rc; }
  rc = zizhan_camcc_rcg_xo(o, rcg);
  if (stage) *stage = 2;
  if (rc) { (void)zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0); return rc; }
  rc = zizhan_camcc_branch(o, cbcr, 1);
  if (stage) *stage = 3;
  if (rc) { (void)zizhan_camcc_branch(o, cbcr, 0); (void)zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0); return rc; }
  return ZizhanCamCcOk;
}
static inline int zizhan_camcc_mclk_off(const ZizhanCamCcOps *o, uint32_t cbcr) {
  const int a = zizhan_camcc_branch(o, cbcr, 0);
  const int b = zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0);
  return a ? a : b;
}
/* Bring MCLK3 to 19.2 MHz; on any failure the steps already taken are undone in reverse. */
static inline int zizhan_camcc_mclk3_on(const ZizhanCamCcOps *o, unsigned *stage) {
  int rc = zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 1);
  if (stage) *stage = 1;
  if (rc) { (void)zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0); return rc; }
  rc = zizhan_camcc_rcg_xo(o, ZizhanCamCcMclk3Rcg);
  if (stage) *stage = 2;
  if (rc) { (void)zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0); return rc; }
  rc = zizhan_camcc_branch(o, ZizhanCamCcMclk3Cbcr, 1);
  if (stage) *stage = 3;
  if (rc) { (void)zizhan_camcc_branch(o, ZizhanCamCcMclk3Cbcr, 0); (void)zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0); return rc; }
  return ZizhanCamCcOk;
}
static inline int zizhan_camcc_mclk3_off(const ZizhanCamCcOps *o) {
  const int a = zizhan_camcc_branch(o, ZizhanCamCcMclk3Cbcr, 0);
  const int b = zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 0);
  return a ? a : b;
}
#endif
