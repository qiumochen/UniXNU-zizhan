#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanCamCc.h"
/* Fake CAM_CC: GDSC PWR_ON follows SW_COLLAPSE after `lag` polls, RCG UPDATE clears after `lag`, branch CLK_OFF follows enable. */
typedef struct { uint32_t gdscr, rcg_cmd, rcg_cfg, cbcr; unsigned lag, gd_wait, rcg_wait, br_wait, stuck; unsigned writes; uint32_t order[16]; } Fake;
static uint32_t rd(void *c, uint32_t off) {
  Fake *f = c;
  if (off == ZizhanCamCcTitanGdscr) {
    if (f->gd_wait && --f->gd_wait == 0 && !(f->stuck & 1)) { if (f->gdscr & 1) f->gdscr &= ~0x80000000u; else f->gdscr |= 0x80000000u; }
    return f->gdscr;
  }
  if (off == ZizhanCamCcMclk3Rcg) { if (f->rcg_wait && --f->rcg_wait == 0 && !(f->stuck & 2)) f->rcg_cmd &= ~1u; return f->rcg_cmd; }
  if (off == ZizhanCamCcMclk3Rcg + 4) return f->rcg_cfg;
  if (off == ZizhanCamCcMclk3Cbcr) {
    if (f->br_wait && --f->br_wait == 0 && !(f->stuck & 4)) { if (f->cbcr & 1) f->cbcr &= ~0x80000000u; else f->cbcr |= 0x80000000u; }
    return f->cbcr;
  }
  assert(0); return 0;
}
static void wr(void *c, uint32_t off, uint32_t v) {
  Fake *f = c; if (f->writes < 16) f->order[f->writes] = off; ++f->writes;
  if (off == ZizhanCamCcTitanGdscr) { f->gdscr = (f->gdscr & 0x80000000u) | (v & ~0x80000000u); f->gd_wait = f->lag + 1; }
  else if (off == ZizhanCamCcMclk3Rcg) { f->rcg_cmd = v; f->rcg_wait = f->lag + 1; }
  else if (off == ZizhanCamCcMclk3Rcg + 4) f->rcg_cfg = v;
  else if (off == ZizhanCamCcMclk3Cbcr) { f->cbcr = (f->cbcr & 0x80000000u) | (v & ~0x80000000u); f->br_wait = f->lag + 1; }
  else assert(0);
}
static void pause(void *c, unsigned us) { (void)c; (void)us; }
static Fake reset_fake(unsigned lag) { Fake f; memset(&f, 0, sizeof f); f.gdscr = 1; f.rcg_cfg = 0x100605; f.cbcr = 0x80000000u; f.lag = lag; return f; }
int main(void) {
  for (unsigned lag = 0; lag < 5; ++lag) {
    Fake f = reset_fake(lag);
    ZizhanCamCcOps o = {&f, rd, wr, pause, 100};
    unsigned stage = 0;
    assert(zizhan_camcc_mclk3_on(&o, &stage) == ZizhanCamCcOk && stage == 3);
    assert((f.gdscr & 0x80000000u) && !(f.gdscr & 1) && (f.rcg_cfg & 0x1037ffu) == 1u && !(f.cbcr & 0x80000000u) && (f.cbcr & 1));
    assert(f.order[0] == ZizhanCamCcTitanGdscr && f.order[1] == ZizhanCamCcMclk3Rcg + 4 && f.order[2] == ZizhanCamCcMclk3Rcg && f.order[3] == ZizhanCamCcMclk3Cbcr);
    assert(zizhan_camcc_mclk3_off(&o) == ZizhanCamCcOk && (f.cbcr & 0x80000000u) && !(f.cbcr & 1) && !(f.gdscr & 0x80000000u) && (f.gdscr & 1));
  }
  /* each stuck stage times out, reports where, and leaves the GDSC collapsed again */
  for (unsigned s = 0; s < 3; ++s) {
    Fake f = reset_fake(1); f.stuck = 1u << s;
    ZizhanCamCcOps o = {&f, rd, wr, pause, 50};
    unsigned stage = 0;
    assert(zizhan_camcc_mclk3_on(&o, &stage) == ZizhanCamCcTimeout && stage == s + 1);
    assert(f.gdscr & 1);
  }
  ZizhanCamCcOps bad = {0, rd, wr, pause, 0};
  assert(zizhan_camcc_gdsc(&bad, ZizhanCamCcTitanGdscr, 1) == ZizhanCamCcArgument);
  puts("PASS camcc: GDSC -> RCG XO 19.2 MHz -> MCLK3 branch, reverse off, bounded waits, undo on every stuck stage");
  return 0;
}
