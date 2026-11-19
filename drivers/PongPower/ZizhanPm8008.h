/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* CAM-01 prerequisite: PM8008 camera PMIC LDO control (not wired into the kernel yet, host tested only).
 * Register map from Linux drivers/regulator/qcom-pm8008-regulator.c: LDOn base 0x4000 + 0x100*(n-1),
 * VSET 0x40/0x41 = millivolts little endian, ENABLE 0x46 bit 7. L1/L2 NLDO 528..1504 mV, L3..L7 PLDO
 * 1504..3400 mV, both in 8 mV steps. On zizhan the regulators of PM8008J answer at I2C 0x0d (chip/revid at
 * 0x0c) on the QUP at 0x994000, enable GPIO11. Front IMX596: vdig = L1 1056 mV, vio = L5 1800 mV.
 * Every write is read back; any transport error or mismatch stops and is reported, nothing is retried. */
#ifndef ZIZHAN_PM8008_H
#define ZIZHAN_PM8008_H
#include <stdint.h>

enum { ZizhanPm8008Ok = 0, ZizhanPm8008Argument, ZizhanPm8008Io, ZizhanPm8008Readback, ZizhanPm8008Range };
enum { ZizhanPm8008RegAddr = 0x0d, ZizhanPm8008ChipAddr = 0x0c };

typedef struct {
  void *context;
  /* One byte at a 16-bit register address; return 0 on success. */
  int (*read)(void *context, uint16_t reg, uint8_t *value);
  int (*write)(void *context, uint16_t reg, uint8_t value);
} ZizhanPm8008Ops;

static inline uint16_t zizhan_pm8008_base(unsigned ldo) { return (uint16_t)(0x4000u + 0x100u * (ldo - 1u)); }

/* 1: millivolts are representable on this LDO (range and 8 mV step). */
static inline int zizhan_pm8008_mv_ok(unsigned ldo, unsigned mv) {
  if (ldo < 1 || ldo > 7) return 0;
  const unsigned lo = ldo <= 2 ? 528u : 1504u, hi = ldo <= 2 ? 1504u : 3400u;
  return mv >= lo && mv <= hi && (mv - lo) % 8u == 0;
}

static inline int zizhan_pm8008_read_ldo(const ZizhanPm8008Ops *o, unsigned ldo, unsigned *mv, int *enabled) {
  if (!o || !o->read || ldo < 1 || ldo > 7 || !mv || !enabled) return ZizhanPm8008Argument;
  const uint16_t base = zizhan_pm8008_base(ldo);
  uint8_t lo = 0, hi = 0, en = 0;
  if (o->read(o->context, (uint16_t)(base + 0x40), &lo) || o->read(o->context, (uint16_t)(base + 0x41), &hi) ||
      o->read(o->context, (uint16_t)(base + 0x46), &en)) return ZizhanPm8008Io;
  *mv = (unsigned)lo | ((unsigned)hi << 8);
  *enabled = (en & 0x80u) != 0;
  return ZizhanPm8008Ok;
}

static inline int zizhan_pm8008_set_mv(const ZizhanPm8008Ops *o, unsigned ldo, unsigned mv) {
  if (!o || !o->read || !o->write || ldo < 1 || ldo > 7) return ZizhanPm8008Argument;
  if (!zizhan_pm8008_mv_ok(ldo, mv)) return ZizhanPm8008Range;
  const uint16_t base = zizhan_pm8008_base(ldo);
  uint8_t lo = 0, hi = 0;
  if (o->write(o->context, (uint16_t)(base + 0x40), (uint8_t)mv) ||
      o->write(o->context, (uint16_t)(base + 0x41), (uint8_t)(mv >> 8))) return ZizhanPm8008Io;
  if (o->read(o->context, (uint16_t)(base + 0x40), &lo) || o->read(o->context, (uint16_t)(base + 0x41), &hi)) return ZizhanPm8008Io;
  return ((unsigned)lo | ((unsigned)hi << 8)) == mv ? ZizhanPm8008Ok : ZizhanPm8008Readback;
}

/* Only bit 7 of ENABLE changes; the rest of the register is preserved. */
static inline int zizhan_pm8008_enable(const ZizhanPm8008Ops *o, unsigned ldo, int on) {
  if (!o || !o->read || !o->write || ldo < 1 || ldo > 7) return ZizhanPm8008Argument;
  const uint16_t reg = (uint16_t)(zizhan_pm8008_base(ldo) + 0x46);
  uint8_t v = 0, back = 0;
  if (o->read(o->context, reg, &v)) return ZizhanPm8008Io;
  const uint8_t want = on ? (uint8_t)(v | 0x80u) : (uint8_t)(v & ~0x80u);
  if (o->write(o->context, reg, want) || o->read(o->context, reg, &back)) return ZizhanPm8008Io;
  return back == want ? ZizhanPm8008Ok : ZizhanPm8008Readback;
}

/* Front camera rails in power-up order (vio before vdig); power-down is the reverse.
 * vana (GPIO53 regulator), MCLK3 and RESET (GPIO128) are sequenced by the caller around these. */
typedef struct { unsigned ldo, mv; } ZizhanPm8008Rail;
static const ZizhanPm8008Rail zizhan_pm8008_front_rails[2] = {{5, 1800}, {1, 1056}};

static inline int zizhan_pm8008_rails_on(const ZizhanPm8008Ops *o, const ZizhanPm8008Rail *rails, unsigned n, unsigned *done) {
  if (done) *done = 0;
  for (unsigned i = 0; i < n; ++i) {
    int rc = zizhan_pm8008_set_mv(o, rails[i].ldo, rails[i].mv);
    if (!rc) rc = zizhan_pm8008_enable(o, rails[i].ldo, 1);
    if (rc) return rc;
    if (done) *done = i + 1;
  }
  return ZizhanPm8008Ok;
}
static inline int zizhan_pm8008_rails_off(const ZizhanPm8008Ops *o, const ZizhanPm8008Rail *rails, unsigned n) {
  int first = ZizhanPm8008Ok;
  for (unsigned i = n; i-- > 0;) {   /* reverse; keep going so a later rail still gets switched off */
    const int rc = zizhan_pm8008_enable(o, rails[i].ldo, 0);
    if (rc && !first) first = rc;
  }
  return first;
}
#endif
