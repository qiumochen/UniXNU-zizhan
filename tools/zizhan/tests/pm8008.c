#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanPm8008.h"
typedef struct { uint8_t regs[0x10000]; unsigned ops, fail_at, sticky_bit; uint16_t log[64]; unsigned writes; } Fake;
static int rd(void *c, uint16_t reg, uint8_t *v) { Fake *f = c; if (++f->ops == f->fail_at) return 1; *v = f->regs[reg]; return 0; }
static int wr(void *c, uint16_t reg, uint8_t v) {
  Fake *f = c; if (++f->ops == f->fail_at) return 1;
  if (f->writes < 64) f->log[f->writes] = reg;
  ++f->writes;
  f->regs[reg] = (uint8_t)(v & ~f->sticky_bit);
  return 0;
}
int main(void) {
  static Fake f;
  ZizhanPm8008Ops o = {&f, rd, wr};
  /* encoding and ranges */
  assert(zizhan_pm8008_base(1) == 0x4000 && zizhan_pm8008_base(5) == 0x4400 && zizhan_pm8008_base(7) == 0x4600);
  assert(zizhan_pm8008_mv_ok(1, 1056) && zizhan_pm8008_mv_ok(5, 1800) && zizhan_pm8008_mv_ok(3, 2800) == 0 + ((2800 - 1504) % 8 == 0));
  assert(!zizhan_pm8008_mv_ok(1, 1800) && !zizhan_pm8008_mv_ok(5, 1056) && !zizhan_pm8008_mv_ok(1, 1057) && !zizhan_pm8008_mv_ok(0, 1056) && !zizhan_pm8008_mv_ok(8, 1800));
  assert(zizhan_pm8008_set_mv(&o, 1, 1800) == ZizhanPm8008Range && f.writes == 0);
  /* set + readback */
  assert(zizhan_pm8008_set_mv(&o, 1, 1056) == ZizhanPm8008Ok && f.regs[0x4040] == 0x20 && f.regs[0x4041] == 0x04);
  unsigned mv = 0; int en = -1;
  f.regs[0x4046] = 0x05;
  assert(zizhan_pm8008_read_ldo(&o, 1, &mv, &en) == ZizhanPm8008Ok && mv == 1056 && en == 0);
  /* enable touches only bit 7 */
  assert(zizhan_pm8008_enable(&o, 1, 1) == ZizhanPm8008Ok && f.regs[0x4046] == 0x85);
  assert(zizhan_pm8008_enable(&o, 1, 0) == ZizhanPm8008Ok && f.regs[0x4046] == 0x05);
  /* readback mismatch (bit refuses to stick) */
  f.sticky_bit = 0x80;
  assert(zizhan_pm8008_enable(&o, 5, 1) == ZizhanPm8008Readback);
  f.sticky_bit = 0;
  /* rails: order vio(L5) then vdig(L1); writes go to 0x44xx before 0x40xx */
  memset(&f, 0, sizeof f);
  unsigned done = 9;
  assert(zizhan_pm8008_rails_on(&o, zizhan_pm8008_front_rails, 2, &done) == ZizhanPm8008Ok && done == 2);
  assert(f.log[0] == 0x4440 && f.log[1] == 0x4441 && f.log[2] == 0x4446 && f.log[3] == 0x4040);
  assert((f.regs[0x4446] & 0x80) && (f.regs[0x4046] & 0x80) && f.regs[0x4440] == (1800 & 255) && f.regs[0x4441] == (1800 >> 8));
  assert(zizhan_pm8008_rails_off(&o, zizhan_pm8008_front_rails, 2) == ZizhanPm8008Ok && !(f.regs[0x4446] & 0x80) && !(f.regs[0x4046] & 0x80));
  /* every single transport failure stops rails_on with Io and never reports more rails done than completed */
  for (unsigned at = 1; at <= 14; ++at) {
    memset(&f, 0, sizeof f); f.fail_at = at; done = 9;
    const int rc = zizhan_pm8008_rails_on(&o, zizhan_pm8008_front_rails, 2, &done);
    assert(rc == ZizhanPm8008Io && done <= 1);
    if (done == 0) assert(!(f.regs[0x4046] & 0x80));
  }
  /* rails_off keeps going after a failure on the first (reverse) rail */
  memset(&f, 0, sizeof f); f.regs[0x4446] = 0x80; f.regs[0x4046] = 0x80; f.fail_at = 1;
  assert(zizhan_pm8008_rails_off(&o, zizhan_pm8008_front_rails, 2) == ZizhanPm8008Io && !(f.regs[0x4446] & 0x80));
  puts("PASS pm8008: map/ranges, set+readback, enable bit7 only, sticky readback, front rail order vio->vdig, every transport failure, off continues");
  return 0;
}
