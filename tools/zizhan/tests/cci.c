#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanCci.h"
/* Scripted fake CCI: queues collect loads; QUEUE_START raises the configured status; READ_DATA returns words. */
typedef struct { uint32_t loads[4][8]; unsigned nload[4], starts, reset; uint32_t status, pending; uint32_t data[4]; unsigned ndata, rd; int nack_q, never; } Cci;
static uint32_t rd(void *c, uint32_t off) {
  Cci *f = c;
  if (off == 0xc0c) { uint32_t s = f->pending; return s; }
  if (off == 0x304) return f->nload[0];
  if (off == 0x404) return f->nload[1];
  if (off == 0x504) return f->nload[2];
  if (off == 0x604) return f->nload[3];
  if (off == 0x11c || off == 0x21c) return f->ndata;
  if (off == 0x118 || off == 0x218) return f->rd < f->ndata ? f->data[f->rd++] : 0;
  return 0;
}
static void wr(void *c, uint32_t off, uint32_t v) {
  Cci *f = c;
  if (off == 0x004) { f->reset++; f->pending = 1u << 24; }
  else if (off == 0xc08) f->pending &= ~v;
  else if (off == 0x310) f->loads[0][f->nload[0]++] = v;
  else if (off == 0x410) f->loads[1][f->nload[1]++] = v;
  else if (off == 0x510) f->loads[2][f->nload[2]++] = v;
  else if (off == 0x610) f->loads[3][f->nload[3]++] = v;
  else if (off == 0x008) {
    f->starts++;
    if (f->never) return;
    if (v == 1) f->pending = f->nack_q == 1 ? (1u << 27) : (1u << 4);
    if (v == 2) f->pending = f->nack_q == 2 ? (1u << 28) : (1u << 0);
    if (v == 4) f->pending = f->nack_q == 3 ? (1u << 29) : (1u << 16);
    if (v == 8) f->pending = f->nack_q == 4 ? (1u << 30) : (1u << 12);
  }
}
static void pause(void *c, unsigned us) { (void)c; (void)us; }
int main(void) {
  Cci f; memset(&f, 0, sizeof f);
  ZizhanCciOps o = {&f, rd, wr, pause, 100};
  assert(zizhan_cci_reset_init(&o) == ZizhanCciOk && f.reset == 1);
  /* IMX chip id: 0x0016, 2 bytes -> words = 1; first byte echo then 0x05 0x96 */
  f.data[0] = 0x00 | 0x05u << 8 | 0x96u << 16; f.ndata = 1;
  uint8_t id[2] = {0};
  assert(zizhan_cci_read16(&o, 0x1a, 0x0016, id, 2) == ZizhanCciOk && id[0] == 0x05 && id[1] == 0x96);
  assert(f.loads[0][0] == (1u | 0x1au << 4) && f.loads[0][1] == (9u | 2u << 4 | 0x00u << 8 | 0x16u << 16) && f.loads[0][2] == (8u | 1u << 8));
  assert(f.loads[1][0] == (1u | 0x1au << 4) && f.loads[1][1] == (10u | 2u << 4) && f.starts == 2);
  /* NACK on the address write (no sensor powered) */
  memset(&f, 0, sizeof f); f.nack_q = 1;
  assert(zizhan_cci_read16(&o, 0x1a, 0x0016, id, 2) == ZizhanCciNack && f.starts == 1);
  /* NACK on the read */
  memset(&f, 0, sizeof f); f.nack_q = 2;
  assert(zizhan_cci_read16(&o, 0x10, 0x0000, id, 2) == ZizhanCciNack);
  /* queue never completes -> bounded timeout */
  memset(&f, 0, sizeof f); f.never = 1;
  assert(zizhan_cci_read16(&o, 0x1a, 0x0016, id, 2) == ZizhanCciTimeout);
  /* wrong word count */
  memset(&f, 0, sizeof f); f.ndata = 3;
  assert(zizhan_cci_read16(&o, 0x1a, 0x0016, id, 2) == ZizhanCciLength);
  /* master 1: queues 0x510/0x610, start bits 4/8, done bits 16/12, data 0x218 (OV13B10 id 0x300a, 3 bytes) */
  memset(&f, 0, sizeof f); f.data[0] = 0x00 | 0x56u << 8 | 0x0du << 16 | 0x42u << 24; f.ndata = 1;
  uint8_t ov[3] = {0};
  assert(zizhan_cci_reset_init_m(&o, 1) == ZizhanCciOk);
  assert(zizhan_cci_read16_m(&o, 1, 0x36, 0x300a, ov, 3) == ZizhanCciOk && ov[0] == 0x56 && ov[1] == 0x0d && ov[2] == 0x42);
  assert(f.loads[2][0] == (1u | 0x36u << 4) && f.loads[3][1] == (10u | 3u << 4) && f.nload[0] == 0 && f.nload[1] == 0);
  memset(&f, 0, sizeof f); f.nack_q = 3;
  assert(zizhan_cci_read16_m(&o, 1, 0x10, 0x0016, id, 2) == ZizhanCciNack);
  memset(&f, 0, sizeof f); f.nack_q = 4;
  assert(zizhan_cci_read16_m(&o, 1, 0x10, 0x0016, id, 2) == ZizhanCciNack);
  assert(zizhan_cci_read16_m(&o, 2, 0x10, 0x0016, id, 2) == ZizhanCciArgument);
  /* argument checks */
  assert(zizhan_cci_read16(&o, 0x80, 0, id, 2) == ZizhanCciArgument && zizhan_cci_read16(&o, 0x1a, 0, id, 12) == ZizhanCciLength);
  puts("PASS cci (masters 0 and 1): reset/init timings, chip-id read framing (SET_PARAM/WRITE/REPORT, SET_PARAM/READ, echo byte skipped), NACK q0/q1, timeout, length");
  return 0;
}
