/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CAM-01 prerequisite: CCI (camera control I2C) register read, polled (host tested, not wired into the kernel yet).
 * Linux drivers/i2c/busses/i2c-qcom-cci.c (cci_v2: 2 masters, queues 64/16 words) and camcc-sm8450.c:
 * every clock the CCI needs can run from the 19.2 MHz XO (no camera PLL): slow AHB RCG 0x10034, CAMNOC AXI RCG
 * 0x13194, CCI1 RCG 0x13148; branches CPAS AHB 0x13164, CAMNOC AXI 0x131ac, core AHB 0x131f0 (halt-delay),
 * CCI1 0x13160; all under titan_top_gdsc. CCI1 sits at 0xac16000; the front IMX596 is on master 0.
 * A sensor register read is a write of the 16-bit register address (queue 0) then a read (queue 1); CCI has no
 * repeated start, Sony sensors accept the stop in between. */
#ifndef ZIZHAN_CCI_H
#define ZIZHAN_CCI_H
#include <stdint.h>
#include "ZizhanCamCc.h"

enum { ZizhanCciOk = 0, ZizhanCciArgument, ZizhanCciTimeout, ZizhanCciNack, ZizhanCciError, ZizhanCciLength, ZizhanCciClock };
enum { ZizhanCciBase1 = 0x0ac16000u };

/* CAM_CC part: GDSC, then the XO roots, then the branches (core AHB is halt-delay: enable only, no poll). */
static inline int zizhan_cci_clocks_on(const ZizhanCamCcOps *c) {
  static const uint32_t rcgs[] = {0x10034u, 0x13194u, 0x13148u};
  static const uint32_t branches[] = {0x13164u, 0x131acu, 0x13160u};
  if (zizhan_camcc_gdsc(c, ZizhanCamCcTitanGdscr, 1)) return ZizhanCciClock;
  for (unsigned i = 0; i < 3; ++i) if (zizhan_camcc_rcg_xo(c, rcgs[i])) return ZizhanCciClock;
  c->write(c->context, 0x131f0u, c->read(c->context, 0x131f0u) | 1u);
  for (unsigned i = 0; i < 3; ++i) if (zizhan_camcc_branch(c, branches[i], 1)) return ZizhanCciClock;
  return ZizhanCciOk;
}

typedef ZizhanCamCcOps ZizhanCciOps;   /* same read/write/pause/poll shape, offsets relative to the CCI base */

static inline int zizhan_cci_wait(const ZizhanCciOps *o, uint32_t done_mask, uint32_t nack_mask, uint32_t err_mask) {
  for (unsigned i = 0; i < o->polls; ++i) {
    const uint32_t st = o->read(o->context, 0xc0c);          /* IRQ_STATUS_0 */
    if (st) { o->write(o->context, 0xc08, st); o->write(o->context, 0xc00, 1); }   /* IRQ_CLEAR_0, GLOBAL_CLEAR */
    if (st & nack_mask) return ZizhanCciNack;
    if (st & err_mask) return ZizhanCciError;
    if (st & done_mask) return ZizhanCciOk;
    o->pause_us(o->context, 10);
  }
  return ZizhanCciTimeout;
}

/* Reset, then master 0 standard mode (100 kHz) timings for a 19.2 MHz CCI clock. */
static inline int zizhan_cci_reset_init(const ZizhanCciOps *o) {
  if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return ZizhanCciArgument;
  o->write(o->context, 0xc04, 0x7fffffffu & (0x00000111u | 0x01000000u | 0x02000000u | 0x18000ee6u));   /* IRQ_MASK_0, master 0 */
  o->write(o->context, 0x004, 0x0f73f3f7u);                  /* RESET_CMD */
  const int rc = zizhan_cci_wait(o, 1u << 24, 0, 0);
  if (rc) return rc;
  o->write(o->context, 0x100, 78u << 16 | 114u);             /* SCL_CTL thigh/tlow */
  o->write(o->context, 0x104, 28u << 16 | 28u);              /* SDA_CTL_0 tsu_sto/tsu_sta */
  o->write(o->context, 0x108, 10u << 16 | 77u);              /* SDA_CTL_1 thd_dat/thd_sta */
  o->write(o->context, 0x10c, 118u);                         /* SDA_CTL_2 tbuf */
  o->write(o->context, 0x110, 1u << 8 | 6u << 4 | 1u);       /* MISC_CTL stretch, trdhld, tsp */
  return ZizhanCciOk;
}

static inline int zizhan_cci_run(const ZizhanCciOps *o, unsigned queue, uint32_t done_mask) {
  const uint32_t cur = o->read(o->context, 0x304 + 0x100 * queue);   /* Q CUR_WORD_CNT, master 0 */
  o->write(o->context, 0x300 + 0x100 * queue, cur);                  /* Q EXEC_WORD_CNT */
  o->write(o->context, 0x008, 1u << queue);                          /* QUEUE_START, master 0 */
  return zizhan_cci_wait(o, done_mask, queue ? (1u << 28) : (1u << 27), 0x00000ee6u);
}

/* Read len (1..11) bytes at a 16-bit register of a 7-bit slave on master 0. */
static inline int zizhan_cci_read16(const ZizhanCciOps *o, uint8_t addr7, uint16_t reg, uint8_t *buf, unsigned len) {
  if (!o || !o->read || !o->write || !o->pause_us || !o->polls || !buf || addr7 > 0x7f) return ZizhanCciArgument;
  if (len < 1 || len > 11) return ZizhanCciLength;
  /* queue 0: address write {SET_PARAM|addr}, {WRITE len 2, reg hi, reg lo}, REPORT with IRQ */
  o->write(o->context, 0x310, 1u | (uint32_t)addr7 << 4);
  o->write(o->context, 0x310, 9u | 2u << 4 | (uint32_t)(reg >> 8) << 8 | (uint32_t)(reg & 0xff) << 16);
  o->write(o->context, 0x310, 8u | 1u << 8);
  int rc = zizhan_cci_run(o, 0, 1u << 4);
  if (rc) return rc;
  /* queue 1: {SET_PARAM|addr}, {READ len} */
  o->write(o->context, 0x410, 1u | (uint32_t)addr7 << 4);
  o->write(o->context, 0x410, 10u | (uint32_t)len << 4);
  rc = zizhan_cci_run(o, 1, 1u << 0);
  if (rc) return rc;
  const uint32_t words = o->read(o->context, 0x11c);                 /* READ_BUF_LEVEL */
  if (words != len / 4 + 1) return ZizhanCciLength;
  unsigned index = 0; int first = 1;
  for (uint32_t w = 0; w < words; ++w) {
    const uint32_t v = o->read(o->context, 0x118);                   /* READ_DATA: first byte is the slave id echo */
    for (unsigned b = 0; b < 4 && index < len; ++b) {
      if (first) { first = 0; continue; }
      buf[index++] = (uint8_t)(v >> (8 * b));
    }
  }
  return ZizhanCciOk;
}
#endif
