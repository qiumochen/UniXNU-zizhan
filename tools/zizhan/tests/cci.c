#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanCci.h"
/* Scripted fake CCI: queues collect loads; QUEUE_START raises the configured status; READ_DATA returns words. */
typedef struct { uint32_t loads[2][8]; unsigned nload[2], starts, reset; uint32_t status, pending; uint32_t data[4]; unsigned ndata, rd; int nack_q, never; } Cci;
static uint32_t rd(void *c, uint32_t off) {
  Cci *f = c;
  if (off == 0xc0c) { uint32_t s = f->pending; return s; }
  if (off == 0x304) return f->nload[0];
  if (off == 0x404) return f->nload[1];
  if (off == 0x11c) return f->ndata;
  if (off == 0x118) return f->rd < f->ndata ? f->data[f->rd++] : 0;
  return 0;
}
static void wr(void *c, uint32_t off, uint32_t v) {
  Cci *f = c;
  if (off == 0x004) { f->reset++; f->pending = 1u << 24; }
  else if (off == 0xc08) f->pending &= ~v;
  else if (off == 0x310) f->loads[0][f->nload[0]++] = v;
  else if (off == 0x410) f->loads[1][f->nload[1]++] = v;
  else if (off == 0x008) {
    f->starts++;
    if (f->never) return;
    if (v == 1) f->pending = f->nack_q == 1 ? (1u << 27) : (1u << 4);
    if (v == 2) f->pending = f->nack_q == 2 ? (1u << 28) : (1u << 0);
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
  /* argument checks */
  assert(zizhan_cci_read16(&o, 0x80, 0, id, 2) == ZizhanCciArgument && zizhan_cci_read16(&o, 0x1a, 0, id, 12) == ZizhanCciLength);
  puts("PASS cci: reset/init timings, chip-id read framing (SET_PARAM/WRITE/REPORT, SET_PARAM/READ, echo byte skipped), NACK q0/q1, timeout, length");
  return 0;
}
