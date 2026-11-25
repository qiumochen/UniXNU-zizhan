/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanMicCodec.h"

typedef struct {
  uint32_t tlmm[4];
  uint32_t lpi[0x4000];
  uint32_t slew[4];
  uint32_t mux[4];
  uint32_t va[0x400];
  uint32_t tx[0x400];
  uint32_t swrm[0x800];
  uint8_t wcd[0x4000];
  uint8_t bcast[0x100];
  uint8_t pending_rd_id;
  uint8_t pending_rd_val;
  uint8_t has_rd;
  uint8_t ssp_cmd_count;
  uint16_t last_ssp_reg;
  uint8_t last_ssp_val;
  int force_no_clk;
  int force_no_slave;
  unsigned sleeps_ms;
} MockBus;

static uint32_t m_tlmm_rd(void *c, uint32_t o) { return ((MockBus *)c)->tlmm[o / 4u]; }
static void m_tlmm_wr(void *c, uint32_t o, uint32_t v) { ((MockBus *)c)->tlmm[o / 4u] = v; }
static uint32_t m_lpi_rd(void *c, uint32_t o) { return ((MockBus *)c)->lpi[o / 4u]; }
static void m_lpi_wr(void *c, uint32_t o, uint32_t v) { ((MockBus *)c)->lpi[o / 4u] = v; }
static uint32_t m_slew_rd(void *c, uint32_t o) { return ((MockBus *)c)->slew[o / 4u]; }
static void m_slew_wr(void *c, uint32_t o, uint32_t v) { ((MockBus *)c)->slew[o / 4u] = v; }
static uint32_t m_mux_rd(void *c, uint32_t o) { return ((MockBus *)c)->mux[o / 4u]; }
static void m_mux_wr(void *c, uint32_t o, uint32_t v) { ((MockBus *)c)->mux[o / 4u] = v; }
static uint32_t m_va_rd(void *c, uint32_t o) { return ((MockBus *)c)->va[o / 4u]; }
static void m_va_wr(void *c, uint32_t o, uint32_t v) { ((MockBus *)c)->va[o / 4u] = v; }
static uint32_t m_tx_rd(void *c, uint32_t o) { return ((MockBus *)c)->tx[o / 4u]; }
static void m_tx_wr(void *c, uint32_t o, uint32_t v) { ((MockBus *)c)->tx[o / 4u] = v; }

static uint32_t m_swrm_rd(void *c, uint32_t o) {
  MockBus *b = (MockBus *)c;
  if (o == 0x0328u) {
    return b->has_rd ? (1u << 16) : 0u;
  }
  if (o == 0x0334u) {
    b->has_rd = 0u;
    return ((uint32_t)b->pending_rd_id << 8) | (uint32_t)b->pending_rd_val;
  }
  return b->swrm[o / 4u];
}

static void m_swrm_wr(void *c, uint32_t o, uint32_t v) {
  MockBus *b = (MockBus *)c;
  b->swrm[o / 4u] = v;
  if (o == 0x0004u && v == 3u && !b->force_no_clk) {
    b->swrm[0x0014u / 4u] = 0x00100001u;
    if (!b->force_no_slave) {
      b->swrm[0x1090u / 4u] = 0x00000004u; /* dev 1 attached */
      b->swrm[(0x0530u + 8u) / 4u] = 0x01170223u;
      b->swrm[(0x0534u + 8u) / 4u] = 0x0000000du;
    }
  }
  if (o == 0x031cu) {
    const uint16_t reg = (uint16_t)(v & 0xffffu);
    const uint8_t id = (uint8_t)((v >> 16) & 0xfu);
    const uint8_t dev = (uint8_t)((v >> 20) & 0xfu);
    const uint8_t val = (uint8_t)((v >> 24) & 0xffu);
    if (id == 0x0fu && dev == 0x0fu) {
      ++b->ssp_cmd_count;
      b->last_ssp_reg = reg;
      b->last_ssp_val = val;
      if (reg == 0x0070u) {
        b->swrm[0x104cu / 4u] = 1u;
      } else if (reg == 0x0060u) {
        b->swrm[0x104cu / 4u] = 0u;
      }
    } else {
      assert(id <= 14u);
      if (dev == 1u && reg < sizeof(b->wcd)) {
        b->wcd[reg] = val;
      } else if (dev == 0x0fu && reg < sizeof(b->bcast)) {
        b->bcast[reg] = val;
      }
    }
  } else if (o == 0x0320u) {
    const uint16_t reg = (uint16_t)(v & 0xffffu);
    const uint8_t id = (uint8_t)((v >> 16) & 0xfu);
    assert(id <= 14u);
    b->pending_rd_id = id;
    b->pending_rd_val = (reg < sizeof(b->wcd)) ? b->wcd[reg] : 0u;
    b->has_rd = 1u;
  } else if (o == 0x0324u && v == 1u) {
    b->has_rd = 0u;
  }
}

static void m_delay_us(void *c, unsigned us) { (void)c; (void)us; }
static void m_sleep_ms(void *c, unsigned ms) { ((MockBus *)c)->sleeps_ms += ms; }

static void init_bus(MockBus *b) {
  memset(b, 0, sizeof(*b));
  b->tlmm[0] = 0xdead0001u;
  b->tlmm[1] = 0x00000002u;
  b->lpi[0] = 0x111u;
  b->lpi[0x1000u / 4u] = 0x222u;
  b->lpi[0x2000u / 4u] = 0x333u;
  b->lpi[0xe000u / 4u] = 0x444u;
  b->slew[0] = 0xaa00u;
  b->mux[0] = 0x00000003u;
  b->swrm[0x0000u / 4u] = 0x01070000u;
  b->swrm[0x1048u / 4u] = 0x0001ff00u;
  b->wcd[0x0050u] = 0x23u;
  b->wcd[0x0051u] = 0x02u;
  b->wcd[0x34b0u] = 0x11u;
  b->wcd[0x34ceu] = 0x06u;
  b->wcd[0x306bu] = 0x1au;
  b->wcd[0x306eu] = 0x1au;
  b->wcd[0x3071u] = 0x1au;
  b->wcd[0x3074u] = 0x1au;
  b->wcd[0x308fu] = 0xfbu;
  b->wcd[0x0126u] = 0x1fu;
  b->wcd[0x0136u] = 0x1fu;
}

int main(void) {
  MockBus bus;
  ZizhanMicHwState st;
  const ZizhanMicHwOps ops = {
      &bus,
      m_tlmm_rd, m_tlmm_wr,
      m_lpi_rd,  m_lpi_wr,
      m_slew_rd, m_slew_wr,
      m_mux_rd,  m_mux_wr,
      m_va_rd,   m_va_wr,
      m_tx_rd,   m_tx_wr,
      m_swrm_rd, m_swrm_wr,
      m_delay_us,
      m_sleep_ms,
  };

  /* 1. Happy path: SWRM v1.7, dev 1, DEC0 (mask 0x01), gain 12 -> unmute -> clean reverse teardown. */
  init_bus(&bus);
  assert(zizhan_mic_hw_bringup(&ops, &st, 0u, 12u) == 1);
  assert(st.stage == ZizhanMicStageReady);
  assert(st.slave_dev == 1u);
  assert(st.channel_mask == 0x01u);
  assert(st.bank_after == 1u);
  assert(st.efuse == 0x11u);
  assert(st.dev_id == 0x0223u);
  assert(st.fifo_timeouts == 0u);
  assert(bus.ssp_cmd_count == 1u);
  assert(bus.last_ssp_reg == 0x0070u && bus.last_ssp_val == 0x0fu);
  assert(bus.va[0x00c4u / 4u] == 0x0fu);
  assert(bus.va[0x00c8u / 4u] == 0x50u);
  assert(bus.va[0x00dcu / 4u] == 0x06u);
  assert(bus.tx[0x0088u / 4u] == 0x60u);
  assert(bus.tx[0x00c0u / 4u] == 0x0eu);
  assert(bus.tx[0x042cu / 4u] == 0x0au);
  assert(bus.swrm[0x0010u / 4u] == 0x041eu);
  assert(bus.swrm[0x0254u / 4u] == 0x01u);
  assert(bus.swrm[0x1254u / 4u] == 0x01u);
  assert(bus.swrm[0x105cu / 4u] == 0x0002010fu);
  assert(bus.swrm[0x1224u / 4u] == 0x01000001u);
  assert(bus.swrm[0x1264u / 4u] == 0x01000001u);
  assert(bus.swrm[0x1228u / 4u] == 0x01u);
  assert(bus.swrm[0x1268u / 4u] == 0x01u);
  assert(bus.bcast[0x00e0u] == 0x01u && bus.bcast[0x00f0u] == 0x01u);
  assert(bus.wcd[0x3022u] == 0x63u);
  assert(bus.wcd[0x3405u] == 0x11u);
  assert(bus.wcd[0x3408u] == 0x18u);
  assert(bus.wcd[0x3409u] == 0xf0u);
  assert(bus.wcd[0x3410u] == 0x05u);
  assert((bus.wcd[0x300fu] & 0x40u) == 0u);
  assert(bus.wcd[0x300eu] == (0x20u | 12u));
  assert(bus.wcd[0x34d8u] == 0x01u);
  assert(bus.wcd[0x0105u] == 0x01u);
  assert(bus.wcd[0x0120u] == 0x01u && bus.wcd[0x0130u] == 0x01u);
  assert(bus.wcd[0x0122u] == 0x01u && bus.wcd[0x0132u] == 0x01u);
  assert(bus.wcd[0x0128u] == 0x01u && bus.wcd[0x0138u] == 0x01u);
  /* Slave DP1_HCTRL (0x0126/0x0136) must stay untouched at hardware default 0x1f (never HSTART=0). */
  assert(bus.wcd[0x0126u] == 0x1fu && bus.wcd[0x0136u] == 0x1fu);
  assert(bus.tx[0x0100u / 4u] == 0x05u);
  assert(bus.tx[0x0104u / 4u] == 0x01u);
  assert(bus.tx[0x0400u / 4u] == 0x34u);
  assert(bus.tx[0x0418u / 4u] == 0x01u);
  assert(bus.tx[0x041cu / 4u] == 0x3cu);

  zizhan_mic_hw_unmute(&ops, &st);
  assert(st.stage == ZizhanMicStageUnmuted);
  assert(bus.tx[0x0400u / 4u] == 0x24u);

  zizhan_mic_hw_teardown(&ops, &st);
  assert(st.stage == ZizhanMicStageStopped);
  assert(st.teardown_ok == 1u);
  assert(bus.tx[0x0400u / 4u] == 0x04u);
  assert(bus.tx[0x0418u / 4u] == 0x00u);
  assert(bus.va[0x00dcu / 4u] == 0x00u);
  assert(bus.wcd[0x0105u] == 0x00u);
  assert(bus.wcd[0x0120u] == 0x00u && bus.wcd[0x0130u] == 0x00u);
  assert(bus.wcd[0x3022u] == 0x23u);
  assert(bus.swrm[0x0004u / 4u] == 0x00u);
  assert(bus.lpi[0] == 0x111u);
  assert(bus.lpi[0x1000u / 4u] == 0x222u);
  assert(bus.lpi[0x2000u / 4u] == 0x333u);
  assert(bus.lpi[0xe000u / 4u] == 0x444u);
  assert(bus.slew[0] == 0xaa00u);
  assert(bus.tlmm[0] == 0xdead0001u && bus.tlmm[1] == 0x00000002u);

  /* 2. Reject unsupported SWRM version (e.g. v2.0 0x02000000) and restore pins/reset cleanly. */
  init_bus(&bus);
  bus.swrm[0x0000u / 4u] = 0x02000000u;
  assert(zizhan_mic_hw_bringup(&ops, &st, 2u, 12u) == 0);
  assert(st.stage == -ZizhanMicStageSwrmInit);
  assert(st.teardown_ok == 1u);
  assert(bus.lpi[0] == 0x111u && bus.tlmm[0] == 0xdead0001u);

  /* 3. Reject missing SoundWire slave enumeration and restore cleanly. */
  init_bus(&bus);
  bus.force_no_slave = 1;
  assert(zizhan_mic_hw_bringup(&ops, &st, 2u, 12u) == 0);
  assert(st.stage == -ZizhanMicStageEnumerate);
  assert(st.teardown_ok == 1u);
  assert(bus.swrm[0x0004u / 4u] == 0x00u);
  assert(bus.tlmm[0] == 0xdead0001u);

  printf("PASS zizhan-mic-codec\n");
  return 0;
}
