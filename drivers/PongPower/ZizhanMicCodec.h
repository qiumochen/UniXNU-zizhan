/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* SM8475 (zizhan / Mix Fold 2) physical main-mic hardware bring-up and reverse teardown:
 *   TLMM gpio43 (wcd938x_reset) -> LPASS LPI pinctrl (gpio0/1/2/14 + slew 0x34da000) ->
 *   VA island mux (0x3420000) + VA_MACRO (0x33f0000) + TX_MACRO (0x3220000) ->
 *   va_swr_master (0x33b0000, SoundWire Master v1.7) ->
 *   wcd938x-tx-slave (0x0d:0x01170223, AMIC1 / MICBIAS1 / ADC1 -> SWRM_TX2_CH1 / DP2 ch1 lane1) ->
 *   VA_MACRO -> TX_MACRO DEC0 (SWR_MIC4 -> CODEC_TX3 channel_mask 0x01).
 * Pure register state machine with no IOKit headers so host ASan/UBSan tests verify every step. */
#ifndef ZIZHAN_MIC_CODEC_H
#define ZIZHAN_MIC_CODEC_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZIZHAN_MIC_TLMM_BASE 0x0f100000ULL
#define ZIZHAN_MIC_WCD_RESET_PIN 43u
#define ZIZHAN_MIC_LPASS_LPI_TLMM 0x03440000ULL
#define ZIZHAN_MIC_LPASS_SLEW 0x034da000ULL
#define ZIZHAN_MIC_VA_ISLAND_MUX 0x03420000ULL
#define ZIZHAN_MIC_VA_MACRO 0x033f0000ULL
#define ZIZHAN_MIC_TX_MACRO 0x03220000ULL
#define ZIZHAN_MIC_VA_SWRM 0x033b0000ULL

#define ZIZHAN_SWRM_VER_1_7_HI 0x0107u
#define ZIZHAN_WCD938X_TX_ID0_MASK 0xfffffff0u
#define ZIZHAN_WCD938X_TX_ID0_VAL 0x01170220u
#define ZIZHAN_WCD938X_TX_ID1_VAL 0x000du

enum {
  ZizhanMicStageIdle = 0,
  ZizhanMicStageReset = 1,
  ZizhanMicStagePins = 2,
  ZizhanMicStageMacros = 3,
  ZizhanMicStageSwrmInit = 4,
  ZizhanMicStageEnumerate = 5,
  ZizhanMicStageWcdInit = 6,
  ZizhanMicStageWcdAdc = 7,
  ZizhanMicStagePorts = 8,
  ZizhanMicStageDecimator = 9,
  ZizhanMicStageReady = 10,
  ZizhanMicStageUnmuted = 11,
  ZizhanMicStageStopped = 12
};

typedef struct {
  void *ctx;
  uint32_t (*tlmm_rd32)(void *ctx, uint32_t off);
  void (*tlmm_wr32)(void *ctx, uint32_t off, uint32_t val);
  uint32_t (*lpi_rd32)(void *ctx, uint32_t off);
  void (*lpi_wr32)(void *ctx, uint32_t off, uint32_t val);
  uint32_t (*slew_rd32)(void *ctx, uint32_t off);
  void (*slew_wr32)(void *ctx, uint32_t off, uint32_t val);
  uint32_t (*mux_rd32)(void *ctx, uint32_t off);
  void (*mux_wr32)(void *ctx, uint32_t off, uint32_t val);
  uint32_t (*va_rd32)(void *ctx, uint32_t off);
  void (*va_wr32)(void *ctx, uint32_t off, uint32_t val);
  uint32_t (*tx_rd32)(void *ctx, uint32_t off);
  void (*tx_wr32)(void *ctx, uint32_t off, uint32_t val);
  uint32_t (*swrm_rd32)(void *ctx, uint32_t off);
  void (*swrm_wr32)(void *ctx, uint32_t off, uint32_t val);
  void (*delay_us)(void *ctx, unsigned us);
  void (*sleep_ms)(void *ctx, unsigned ms);
} ZizhanMicHwOps;

typedef struct {
  int stage;
  uint8_t cmd_id;
  uint8_t slave_dev;
  uint8_t dec_idx;
  uint8_t adc_gain;
  uint8_t dec_vol;
  uint8_t efuse;
  uint8_t bank_after;
  uint8_t saved_tlmm;
  uint8_t saved_lpi;
  uint8_t macros_on;
  uint8_t swrm_on;
  uint8_t teardown_ok;
  uint16_t dev_id;
  uint32_t swrm_ver;
  uint32_t clk_status;
  uint32_t slaves;
  uint32_t slave_id0;
  uint32_t slave_id1;
  uint32_t channel_mask;
  uint32_t fifo_timeouts;
  uint32_t tlmm_cfg_orig;
  uint32_t tlmm_io_orig;
  uint32_t lpi_cfg_orig[4];
  uint32_t slew_orig;
  uint32_t mux_orig;
} ZizhanMicHwState;

static inline uint32_t zizhan_swrm_pack_cmd(uint8_t data, uint8_t dev, uint8_t cmd_id, uint16_t reg) {
  return (uint32_t)reg | ((uint32_t)(cmd_id & 0xfu) << 16) |
         ((uint32_t)(dev & 0xfu) << 20) | ((uint32_t)data << 24);
}

/* Normal command IDs cycle 0..14; 0x0f is reserved by SWRM hardware for broadcast SSP bank switch. */
static inline uint8_t zizhan_swrm_next_id(uint8_t *cursor) {
  const uint8_t next = (*cursor < 14u) ? (uint8_t)(*cursor + 1u) : 0u;
  *cursor = next;
  return next;
}

static inline int zizhan_swrm_fifo_write_raw(const ZizhanMicHwOps *ops, ZizhanMicHwState *st,
                                             uint8_t data, uint8_t dev, uint8_t cmd_id, uint16_t reg) {
  unsigned waited = 0;
  while (((ops->swrm_rd32(ops->ctx, 0x0328u) >> 8) & 0x1fu) >= 8u) {
    if (++waited > 200u) {
      ++st->fifo_timeouts;
      return 0;
    }
    ops->delay_us(ops->ctx, 50u);
  }
  ops->swrm_wr32(ops->ctx, 0x031cu, zizhan_swrm_pack_cmd(data, dev, cmd_id, reg));
  ops->delay_us(ops->ctx, 150u);
  return 1;
}

static inline int zizhan_swrm_write(const ZizhanMicHwOps *ops, ZizhanMicHwState *st,
                                    uint8_t dev, uint16_t reg, uint8_t value) {
  const uint8_t id = zizhan_swrm_next_id(&st->cmd_id);
  return zizhan_swrm_fifo_write_raw(ops, st, value, dev, id, reg);
}

static inline int zizhan_swrm_read(const ZizhanMicHwOps *ops, ZizhanMicHwState *st,
                                   uint8_t dev, uint16_t reg, uint8_t *out) {
  unsigned waited = 0;
  while (((ops->swrm_rd32(ops->ctx, 0x0328u) >> 8) & 0x1fu) >= 8u) {
    if (++waited > 200u) {
      ++st->fifo_timeouts;
      return 0;
    }
    ops->delay_us(ops->ctx, 50u);
  }
  const uint8_t id = zizhan_swrm_next_id(&st->cmd_id);
  ops->swrm_wr32(ops->ctx, 0x0320u, zizhan_swrm_pack_cmd(1u, dev, id, reg));
  for (unsigned i = 0; i < 200u; ++i) {
    if (((ops->swrm_rd32(ops->ctx, 0x0328u) >> 16) & 0x1fu) != 0u) {
      const uint32_t v = ops->swrm_rd32(ops->ctx, 0x0334u);
      if (((v >> 8) & 0xfu) == id) {
        *out = (uint8_t)(v & 0xffu);
        return 1;
      }
      ops->swrm_wr32(ops->ctx, 0x0324u, 1u);
      break;
    }
    ops->delay_us(ops->ctx, 50u);
  }
  ++st->fifo_timeouts;
  return 0;
}

static inline int zizhan_swrm_update(const ZizhanMicHwOps *ops, ZizhanMicHwState *st,
                                     uint8_t dev, uint16_t reg, uint8_t mask, uint8_t val) {
  uint8_t cur = 0;
  if (!zizhan_swrm_read(ops, st, dev, reg, &cur)) {
    return 0;
  }
  const uint8_t next = (uint8_t)((cur & (uint8_t)~mask) | (val & mask));
  return zizhan_swrm_write(ops, st, dev, reg, next);
}

static inline void zizhan_mic_hw_teardown(const ZizhanMicHwOps *ops, ZizhanMicHwState *st) {
  if (!ops || !st) {
    return;
  }
  if (st->macros_on && st->dec_idx < 8u) {
    const uint32_t mux0 = 0x0100u + 8u * st->dec_idx;
    const uint32_t mux1 = 0x0104u + 8u * st->dec_idx;
    const uint32_t ctl = 0x0400u + 0x80u * st->dec_idx;
    const uint32_t vol = 0x0404u + 0x80u * st->dec_idx;
    const uint32_t sec6 = 0x0418u + 0x80u * st->dec_idx;
    ops->tx_wr32(ops->ctx, ctl, 0x14u);
    ops->tx_wr32(ops->ctx, sec6, 0x00u);
    ops->tx_wr32(ops->ctx, ctl, 0x04u);
    ops->tx_wr32(ops->ctx, vol, 0x00u);
    ops->tx_wr32(ops->ctx, mux1, 0x00u);
    ops->tx_wr32(ops->ctx, mux0, 0x00u);
    ops->va_wr32(ops->ctx, 0x00dcu, ops->va_rd32(ops->ctx, 0x00dcu) & ~0x0fu);
  }
  if (st->swrm_on && st->slave_dev >= 1u && st->slave_dev <= 11u) {
    const uint8_t d = st->slave_dev;
    (void)zizhan_swrm_write(ops, st, d, 0x0120u, 0x00u);
    (void)zizhan_swrm_write(ops, st, d, 0x0130u, 0x00u);
    (void)zizhan_swrm_write(ops, st, d, 0x0105u, 0x00u);
    (void)zizhan_swrm_write(ops, st, d, 0x300eu, 0x06u);
    (void)zizhan_swrm_write(ops, st, d, 0x3410u, 0x00u);
    (void)zizhan_swrm_write(ops, st, d, 0x3405u, 0x00u);
    (void)zizhan_swrm_write(ops, st, d, 0x3408u, 0x00u);
    (void)zizhan_swrm_write(ops, st, d, 0x3022u, 0x23u);
  }
  if (st->swrm_on) {
    ops->swrm_wr32(ops->ctx, 0x1224u, 0x00u);
    ops->swrm_wr32(ops->ctx, 0x1264u, 0x00u);
    ops->swrm_wr32(ops->ctx, 0x1254u, 0x00u);
    ops->swrm_wr32(ops->ctx, 0x0254u, 0x00u);
    ops->swrm_wr32(ops->ctx, 0x0004u, 0x00u);
    st->swrm_on = 0u;
  }
  if (st->macros_on) {
    ops->tx_wr32(ops->ctx, 0x0004u, ops->tx_rd32(ops->ctx, 0x0004u) & ~1u);
    ops->tx_wr32(ops->ctx, 0x0000u, ops->tx_rd32(ops->ctx, 0x0000u) & ~1u);
    ops->va_wr32(ops->ctx, 0x0008u, ops->va_rd32(ops->ctx, 0x0008u) & ~1u);
    ops->va_wr32(ops->ctx, 0x0004u, ops->va_rd32(ops->ctx, 0x0004u) & ~3u);
    ops->va_wr32(ops->ctx, 0x0000u, ops->va_rd32(ops->ctx, 0x0000u) & ~1u);
    ops->mux_wr32(ops->ctx, 0x0000u, st->mux_orig);
    st->macros_on = 0u;
  }
  if (st->saved_lpi) {
    static const uint32_t kPins[4] = {0u, 1u, 2u, 14u};
    for (unsigned i = 0; i < 4u; ++i) {
      ops->lpi_wr32(ops->ctx, kPins[i] * 0x1000u, st->lpi_cfg_orig[i]);
    }
    ops->slew_wr32(ops->ctx, 0x0000u, st->slew_orig);
    st->saved_lpi = 0u;
  }
  if (st->saved_tlmm) {
    ops->tlmm_wr32(ops->ctx, 0x0004u, 0u);
    ops->sleep_ms(ops->ctx, 2u);
    ops->tlmm_wr32(ops->ctx, 0x0000u, st->tlmm_cfg_orig);
    ops->tlmm_wr32(ops->ctx, 0x0004u, st->tlmm_io_orig);
    st->saved_tlmm = 0u;
  }
  st->teardown_ok = 1u;
  if (st->stage > 0) {
    st->stage = ZizhanMicStageStopped;
  }
}

static inline int zizhan_mic_hw_bringup_vol(const ZizhanMicHwOps *ops, ZizhanMicHwState *st,
                                            uint8_t dec_idx, uint8_t adc_gain, uint8_t dec_vol) {
  if (!ops || !st || dec_idx > 7u || adc_gain > 20u || dec_vol > 40u) {
    return 0;
  }
  memset(st, 0, sizeof(*st));
  st->cmd_id = 14u;
  st->dec_idx = dec_idx;
  st->adc_gain = adc_gain;
  st->dec_vol = dec_vol;

  /* 1. Save TLMM gpio43 and pulse active-low WCD9385 reset. */
  st->stage = ZizhanMicStageReset;
  st->tlmm_cfg_orig = ops->tlmm_rd32(ops->ctx, 0x0000u);
  st->tlmm_io_orig = ops->tlmm_rd32(ops->ctx, 0x0004u);
  st->saved_tlmm = 1u;
  ops->tlmm_wr32(ops->ctx, 0x0004u, 0u);
  ops->tlmm_wr32(ops->ctx, 0x0000u, (1u << 9) | (7u << 6));
  ops->sleep_ms(ops->ctx, 20u);
  ops->tlmm_wr32(ops->ctx, 0x0004u, 1u << 1);
  ops->sleep_ms(ops->ctx, 20u);

  /* 2. Configure LPASS LPI SoundWire pins (gpio0 clk, gpio1/2/14 data0..2) and slew rate. */
  st->stage = ZizhanMicStagePins;
  static const struct {
    uint32_t pin;
    uint32_t slew_shift;
    uint32_t pull;
  } kSwrPins[4] = {
      {0u, 0u, 0u},
      {1u, 2u, 2u},
      {2u, 4u, 2u},
      {14u, 6u, 2u},
  };
  st->slew_orig = ops->slew_rd32(ops->ctx, 0x0000u);
  uint32_t sv = st->slew_orig;
  for (unsigned i = 0; i < 4u; ++i) {
    const uint32_t off = kSwrPins[i].pin * 0x1000u;
    st->lpi_cfg_orig[i] = ops->lpi_rd32(ops->ctx, off);
    ops->lpi_wr32(ops->ctx, off, (1u << 6) | (1u << 2) | kSwrPins[i].pull);
    sv = (sv & ~(3u << kSwrPins[i].slew_shift)) | (1u << kSwrPins[i].slew_shift);
  }
  ops->slew_wr32(ops->ctx, 0x0000u, sv);
  st->saved_lpi = 1u;

  /* 3. Ungate VA island mux, VA_MACRO SoundWire/MCLK/FSGEN + SWR bridge, and TX_MACRO MCLK/FSGEN + SWR inputs. */
  st->stage = ZizhanMicStageMacros;
  st->mux_orig = ops->mux_rd32(ops->ctx, 0x0000u);
  ops->mux_wr32(ops->ctx, 0x0000u, st->mux_orig & ~2u);
  ops->sleep_ms(ops->ctx, 1u);
  ops->va_wr32(ops->ctx, 0x0000u, ops->va_rd32(ops->ctx, 0x0000u) | 1u);
  ops->va_wr32(ops->ctx, 0x0004u, ops->va_rd32(ops->ctx, 0x0004u) | 3u);
  ops->va_wr32(ops->ctx, 0x0004u, (ops->va_rd32(ops->ctx, 0x0004u) & ~2u) | 1u);
  ops->va_wr32(ops->ctx, 0x0080u, ops->va_rd32(ops->ctx, 0x0080u) | 2u);
  ops->va_wr32(ops->ctx, 0x0094u, 0x80u);
  ops->va_wr32(ops->ctx, 0x00a4u, 0x0cu);
  ops->va_wr32(ops->ctx, 0x00c0u, 0x02u);
  ops->va_wr32(ops->ctx, 0x00c4u, 0x0fu);
  ops->va_wr32(ops->ctx, 0x00c8u, 0x50u);
  ops->va_wr32(ops->ctx, 0x00ccu, 0x40u);
  for (uint32_t r = 0x00d0u; r <= 0x00d8u; r += 4u) {
    ops->va_wr32(ops->ctx, r, (ops->va_rd32(ops->ctx, r) & ~0xeeu) | 0xccu);
  }
  ops->va_wr32(ops->ctx, 0x0008u, ops->va_rd32(ops->ctx, 0x0008u) | 2u);
  ops->va_wr32(ops->ctx, 0x0008u, ops->va_rd32(ops->ctx, 0x0008u) | 1u);
  ops->va_wr32(ops->ctx, 0x0008u, ops->va_rd32(ops->ctx, 0x0008u) & ~2u);
  ops->va_wr32(ops->ctx, 0x00dcu, (ops->va_rd32(ops->ctx, 0x00dcu) & ~0x0fu) | 0x06u);
  ops->tx_wr32(ops->ctx, 0x0000u, ops->tx_rd32(ops->ctx, 0x0000u) | 1u);
  ops->tx_wr32(ops->ctx, 0x0004u, ops->tx_rd32(ops->ctx, 0x0004u) | 1u);
  ops->tx_wr32(ops->ctx, 0x0088u, 0x60u);
  ops->tx_wr32(ops->ctx, 0x00a4u, 0x0cu);
  for (uint32_t r = 0x00c0u; r <= 0x00d4u; r += 4u) {
    ops->tx_wr32(ops->ctx, r, 0x0eu);
  }
  ops->tx_wr32(ops->ctx, 0x042cu, (ops->tx_rd32(ops->ctx, 0x042cu) & ~0x3fu) | 0x0au);
  st->macros_on = 1u;

  /* 4. Bring up va_swr_master (0x33b0000, SWRM v1.7). */
  st->stage = ZizhanMicStageSwrmInit;
  st->swrm_ver = ops->swrm_rd32(ops->ctx, 0x0000u);
  if ((st->swrm_ver >> 16) != ZIZHAN_SWRM_VER_1_7_HI) {
    st->stage = -ZizhanMicStageSwrmInit;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }
  ops->swrm_wr32(ops->ctx, 0x0008u, 1u);
  ops->sleep_ms(ops->ctx, 1u);
  ops->swrm_wr32(ops->ctx, 0x101cu, 0x002f0008u);
  ops->swrm_wr32(ops->ctx, 0x0500u, 1u);
  ops->swrm_wr32(ops->ctx, 0x1048u, ops->swrm_rd32(ops->ctx, 0x1048u) | 0x003e0000u);
  ops->swrm_wr32(ops->ctx, 0x0314u, 3u);
  ops->swrm_wr32(ops->ctx, 0x1044u, 4u);
  ops->swrm_wr32(ops->ctx, 0x0018u, 1u);
  ops->swrm_wr32(ops->ctx, 0x0004u, 2u);
  ops->swrm_wr32(ops->ctx, 0x0208u, 0xffffffffu);
  ops->swrm_wr32(ops->ctx, 0x0210u, 0x001dfdfdU);
  ops->swrm_wr32(ops->ctx, 0x0220u, 0x04u);
  ops->swrm_wr32(ops->ctx, 0x0004u, 3u);
  st->swrm_on = 1u;
  for (unsigned i = 0; i < 200u; ++i) {
    st->clk_status = ops->swrm_rd32(ops->ctx, 0x0014u);
    if (st->clk_status & 1u) {
      break;
    }
    ops->sleep_ms(ops->ctx, 1u);
  }
  if (!(st->clk_status & 1u)) {
    st->stage = -ZizhanMicStageSwrmInit;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }
  ops->swrm_wr32(ops->ctx, 0x0314u, ops->swrm_rd32(ops->ctx, 0x0314u) | 0x80000000u);

  /* 5. Wait for WCD9385 TX slave enumeration (0x0d:0x01170223). */
  st->stage = ZizhanMicStageEnumerate;
  for (unsigned i = 0; i < 200u; ++i) {
    st->slaves = ops->swrm_rd32(ops->ctx, 0x1090u);
    if (st->slaves & 0x3fffcu) {
      break;
    }
    ops->sleep_ms(ops->ctx, 5u);
  }
  uint8_t found_dev = 0u;
  for (uint8_t d = 1u; d <= 11u; ++d) {
    if (((st->slaves >> (2u * d)) & 3u) == 0u) {
      continue;
    }
    const uint32_t id0 = ops->swrm_rd32(ops->ctx, 0x0530u + 8u * d);
    const uint32_t id1 = ops->swrm_rd32(ops->ctx, 0x0534u + 8u * d);
    if ((id0 & ZIZHAN_WCD938X_TX_ID0_MASK) == ZIZHAN_WCD938X_TX_ID0_VAL &&
        (id1 & 0xffffu) == ZIZHAN_WCD938X_TX_ID1_VAL) {
      found_dev = d;
      st->slave_id0 = id0;
      st->slave_id1 = id1;
      break;
    }
    if (!found_dev) {
      found_dev = d;
      st->slave_id0 = id0;
      st->slave_id1 = id1;
    }
  }
  if (!found_dev || (st->slave_id0 & ZIZHAN_WCD938X_TX_ID0_MASK) != ZIZHAN_WCD938X_TX_ID0_VAL ||
      (st->slave_id1 & 0xffffu) != ZIZHAN_WCD938X_TX_ID1_VAL) {
    st->stage = -ZizhanMicStageEnumerate;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }
  st->slave_dev = found_dev;
  {
    uint8_t ef = 0, id_lo = 0, id_hi = 0;
    if (zizhan_swrm_read(ops, st, found_dev, 0x34b0u, &ef)) {
      st->efuse = ef;
    }
    if (zizhan_swrm_read(ops, st, found_dev, 0x0050u, &id_lo) &&
        zizhan_swrm_read(ops, st, found_dev, 0x0051u, &id_hi)) {
      st->dev_id = (uint16_t)(id_lo | ((uint16_t)id_hi << 8));
    }
  }

  /* 6. WCD9385 wcd938x_init_reg sequence + ADC1 ChMap -> SWRM_TX2_CH1. */
  st->stage = ZizhanMicStageWcdInit;
  const uint8_t dev = st->slave_dev;
  int ok = zizhan_swrm_update(ops, st, dev, 0x3103u, 0x0eu, 0x0eu) &&
           zizhan_swrm_update(ops, st, dev, 0x3103u, 0x80u, 0x80u);
  ops->sleep_ms(ops->ctx, 1u);
  ok = ok && zizhan_swrm_update(ops, st, dev, 0x3103u, 0x40u, 0x40u);
  ops->sleep_ms(ops->ctx, 1u);
  ok = ok && zizhan_swrm_update(ops, st, dev, 0x312au, 0x10u, 0x00u) &&
       zizhan_swrm_update(ops, st, dev, 0x3029u, 0xf0u, 0x80u) &&
       zizhan_swrm_update(ops, st, dev, 0x3001u, 0x80u, 0x80u) &&
       zizhan_swrm_update(ops, st, dev, 0x3001u, 0x40u, 0x40u);
  ops->sleep_ms(ops->ctx, 10u);
  ok = ok && zizhan_swrm_update(ops, st, dev, 0x3001u, 0x40u, 0x00u) &&
       zizhan_swrm_update(ops, st, dev, 0x3132u, 0xf0u, 0x00u) &&
       zizhan_swrm_update(ops, st, dev, 0x3140u, 0x1fu, 0x15u) &&
       zizhan_swrm_update(ops, st, dev, 0x3141u, 0x1fu, 0x15u) &&
       zizhan_swrm_update(ops, st, dev, 0x30ddu, 0xc0u, 0x80u) &&
       zizhan_swrm_update(ops, st, dev, 0x345bu, 0x02u, 0x02u) &&
       zizhan_swrm_write(ops, st, dev, 0x31e1u, 0x14u) &&
       zizhan_swrm_update(ops, st, dev, 0x31dfu, 0x1fu, 0x08u);
  static const uint8_t kTxDsmCfg[9] = {0x55u, 0x44u, 0x11u, 0x00u, 0x00u, 0x55u, 0x55u, 0x55u, 0x01u};
  for (unsigned i = 0; i < 9u && ok; ++i) {
    ok = zizhan_swrm_write(ops, st, dev, (uint16_t)(0x34d0u + i), kTxDsmCfg[i]);
  }
  static const uint16_t kFlybackRegs[4] = {0x306bu, 0x306eu, 0x3071u, 0x3074u};
  for (unsigned i = 0; i < 4u && ok; ++i) {
    ok = zizhan_swrm_update(ops, st, dev, kFlybackRegs[i], 0xe0u, 0xe0u) &&
         zizhan_swrm_update(ops, st, dev, (uint16_t)(kFlybackRegs[i] + 1u), 0x01u, 0x01u);
  }
  ok = ok && zizhan_swrm_update(ops, st, dev, 0x308fu, 0x01u, 0x00u);
  uint8_t ce = 0;
  if (ok && zizhan_swrm_read(ops, st, dev, 0x34ceu, &ce)) {
    ok = zizhan_swrm_update(ops, st, dev, 0x3103u, 0x0eu, (uint8_t)((ce & 0x07u) << 1));
  }
  if (!ok) {
    st->stage = -ZizhanMicStageWcdInit;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }

  /* 7. Enable MICBIAS1 (2.75 V, 0x3022=0x63) and AMIC1 -> ADC1 (0x300e = 0x20 | adc_gain). */
  st->stage = ZizhanMicStageWcdAdc;
  ok = zizhan_swrm_update(ops, st, dev, 0x3022u, 0x3fu, 0x23u) &&
       zizhan_swrm_update(ops, st, dev, 0x3409u, 0xf0u, 0xf0u) &&
       zizhan_swrm_update(ops, st, dev, 0x3417u, 0x1fu, 0x1fu) &&
       zizhan_swrm_update(ops, st, dev, 0x3022u, 0xc0u, 0x40u) &&
       zizhan_swrm_update(ops, st, dev, 0x3408u, 0x18u, 0x18u) &&
       zizhan_swrm_update(ops, st, dev, 0x3457u, 0x03u, 0x02u) &&
       zizhan_swrm_write(ops, st, dev, 0x3405u, 0x11u) &&
       zizhan_swrm_update(ops, st, dev, 0x300fu, 0x40u, 0x40u) &&
       zizhan_swrm_update(ops, st, dev, 0x3410u, 0x0fu, 0x05u) &&
       zizhan_swrm_update(ops, st, dev, 0x300fu, 0x40u, 0x00u) &&
       zizhan_swrm_write(ops, st, dev, 0x300eu, (uint8_t)(0x20u | (adc_gain & 0x1fu)));
  if (!ok) {
    st->stage = -ZizhanMicStageWcdAdc;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }

  /* 8. Configure Slave DP1 (slv_port_id=0: base 0x0100, ch 1, lane 1, sinterval=1, offset1=0) + Master Port 2 (SWRM_TX2_CH1) + SSP bank switch. */
  st->stage = ZizhanMicStagePorts;
  const uint32_t cur_bank = ops->swrm_rd32(ops->ctx, 0x104cu) & 1u;
  const uint32_t next_bank = cur_bank ^ 1u;
  const uint16_t nb_off = (uint16_t)(0x10u * next_bank);
  const uint16_t cb_off = (uint16_t)(0x10u * cur_bank);
  ok = zizhan_swrm_write(ops, st, 0x0fu, (uint16_t)(0x00e0u + nb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0120u + nb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0122u + nb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0123u + nb_off), 0x00u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0124u + nb_off), 0x00u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0125u + nb_off), 0x00u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0128u + nb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, 0x0105u, 0x01u);
  if (!ok) {
    st->stage = -ZizhanMicStagePorts;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }
  ops->swrm_wr32(ops->ctx, 0x0010u, 0x041eu);
  ops->swrm_wr32(ops->ctx, 0x1254u, 0x01u);
  ops->swrm_wr32(ops->ctx, 0x0254u, 0x01u);
  ops->swrm_wr32(ops->ctx, 0x122cu, 0x00u);
  ops->swrm_wr32(ops->ctx, 0x1224u + 0x40u * next_bank, 0x01000001u);
  ops->swrm_wr32(ops->ctx, 0x123cu + 0x40u * next_bank, 0x00u);
  ops->swrm_wr32(ops->ctx, 0x1228u + 0x40u * next_bank, 0x01u);
  ops->swrm_wr32(ops->ctx, 0x1234u + 0x40u * next_bank, 0xf0u);
  ops->swrm_wr32(ops->ctx, 0x101cu + 0x40u * next_bank, 0x0002010fu);
  if (!zizhan_swrm_fifo_write_raw(ops, st, 0x0fu, 0x0fu, 0x0fu, (uint16_t)(0x0060u + nb_off))) {
    st->stage = -ZizhanMicStagePorts;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }
  ops->sleep_ms(ops->ctx, 5u);
  st->bank_after = (uint8_t)(ops->swrm_rd32(ops->ctx, 0x104cu) & 1u);
  /* Mirror active port config into previous bank after bank switch, matching swrm_slvdev_datapath_control. */
  ok = zizhan_swrm_write(ops, st, 0x0fu, (uint16_t)(0x00e0u + cb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0120u + cb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0122u + cb_off), 0x01u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0123u + cb_off), 0x00u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0124u + cb_off), 0x00u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0125u + cb_off), 0x00u) &&
       zizhan_swrm_write(ops, st, dev, (uint16_t)(0x0128u + cb_off), 0x01u);
  if (!ok) {
    st->stage = -ZizhanMicStagePorts;
    zizhan_mic_hw_teardown(ops, st);
    return 0;
  }
  ops->swrm_wr32(ops->ctx, 0x1224u + 0x40u * cur_bank, 0x01000001u);
  ops->swrm_wr32(ops->ctx, 0x123cu + 0x40u * cur_bank, 0x00u);
  ops->swrm_wr32(ops->ctx, 0x1228u + 0x40u * cur_bank, 0x01u);
  ops->swrm_wr32(ops->ctx, 0x1234u + 0x40u * cur_bank, 0xf0u);

  /* 9. Route SWR_MIC4 (0x05) into TX_MACRO DEC{dec_idx} at 48 kHz +16 dB with SWR PDM clock enabled, initially muted. */
  st->stage = ZizhanMicStageDecimator;
  const uint32_t mux0 = 0x0100u + 8u * dec_idx;
  const uint32_t mux1 = 0x0104u + 8u * dec_idx;
  const uint32_t ctl = 0x0400u + 0x80u * dec_idx;
  const uint32_t vol = 0x0404u + 0x80u * dec_idx;
  const uint32_t sec2 = 0x0408u + 0x80u * dec_idx;
  const uint32_t sec6 = 0x0418u + 0x80u * dec_idx;
  const uint32_t sec7_0 = 0x041cu + 0x80u * dec_idx;
  const uint32_t sec7 = 0x0420u + 0x80u * dec_idx;
  ops->tx_wr32(ops->ctx, mux0, (ops->tx_rd32(ops->ctx, mux0) & ~0xfu) | 0x05u);
  ops->tx_wr32(ops->ctx, mux1, (ops->tx_rd32(ops->ctx, mux1) & ~0x3u) | 0x01u);
  ops->tx_wr32(ops->ctx, sec2, 0x0bu);
  ops->tx_wr32(ops->ctx, sec7_0, 0x3cu);
  ops->tx_wr32(ops->ctx, sec7, 0x20u);
  ops->tx_wr32(ops->ctx, vol, dec_vol);
  ops->tx_wr32(ops->ctx, ctl, 0x74u);
  ops->tx_wr32(ops->ctx, ctl, 0x34u);
  ops->tx_wr32(ops->ctx, sec6, 0x02u);
  ops->tx_wr32(ops->ctx, sec6, 0x03u);
  ops->delay_us(ops->ctx, 1000u);
  ops->tx_wr32(ops->ctx, sec6, 0x01u);
  st->channel_mask = 1u << dec_idx;
  st->stage = ZizhanMicStageReady;
  return 1;
}

static inline int zizhan_mic_hw_bringup(const ZizhanMicHwOps *ops, ZizhanMicHwState *st,
                                        uint8_t dec_idx, uint8_t adc_gain) {
  return zizhan_mic_hw_bringup_vol(ops, st, dec_idx, adc_gain, 0x10u);
}

static inline void zizhan_mic_hw_unmute(const ZizhanMicHwOps *ops, ZizhanMicHwState *st) {
  if (!ops || !st || st->stage != ZizhanMicStageReady || !st->macros_on || st->dec_idx > 7u) {
    return;
  }
  ops->sleep_ms(ops->ctx, 150u);
  const uint32_t ctl = 0x0400u + 0x80u * st->dec_idx;
  ops->tx_wr32(ops->ctx, ctl, ops->tx_rd32(ops->ctx, ctl) & ~0x10u);
  st->stage = ZizhanMicStageUnmuted;
}

int zizhan_mic_codec_start(uint32_t *channel_mask_out);
void zizhan_mic_codec_unmute(void);
void zizhan_mic_codec_stop(void);

#ifdef __cplusplus
}
#endif

#endif
