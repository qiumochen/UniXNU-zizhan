/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-C helpers for SM8475 SE20 GENI Bluetooth UART baudrate planning, QCA6490 EDL/NVM
 * 3 Mbps setup, BlueTool/bluetoothd H4 command shimming, and Qualcomm in-band sleep (IBS).
 * Free of IOKit/XNU headers so host ASan/UBSan tests exercise every state machine directly. */
#ifndef ZIZHAN_BT_PROTOCOL_H
#define ZIZHAN_BT_PROTOCOL_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZIZHAN_BT_BAUD_INIT 115200u
#define ZIZHAN_BT_BAUD_OPER 3000000u

/* Linux drivers/bluetooth/btqca.h enum qca_baudrate */
#define ZIZHAN_QCA_BAUD_115200 0x00u
#define ZIZHAN_QCA_BAUD_3000000 0x0eu

/* Linux include/linux/soc/qcom/geni-se.h & drivers/tty/serial/qcom_geni_serial.c */
#define ZIZHAN_GENI_STATUS_M_ACTIVE (1u << 0)
#define ZIZHAN_GENI_STATUS_S_ACTIVE (1u << 12)
#define ZIZHAN_GENI_S_CMD_ABORT (1u << 1)
#define ZIZHAN_GENI_S_CMD_CANCEL (1u << 2)
#define ZIZHAN_GENI_S_IRQ_CMD_CANCEL (1u << 4)
#define ZIZHAN_GENI_S_IRQ_CMD_ABORT (1u << 5)
#define ZIZHAN_GENI_TX_CTS_MASK (1u << 1)
#define ZIZHAN_GENI_MANUAL_RFR_EN (1u << 31)
#define ZIZHAN_GENI_RFR_NOT_READY (1u << 1)
#define ZIZHAN_GENI_MANUAL_RFR_NOT_READY (ZIZHAN_GENI_MANUAL_RFR_EN | ZIZHAN_GENI_RFR_NOT_READY)

typedef struct {
  int dfs_enabled;
  int level;
  uint32_t rate_hz;
  uint32_t clk_div;
  uint32_t rcg_cfg;
  uint32_t rcg_m;
  uint32_t rcg_n;
  uint32_t rcg_d;
} ZizhanBtRcgPlan;

static inline int zizhan_bt_qca_baud_code(uint32_t baud, uint8_t *code_out) {
  if (!code_out) {
    return 0;
  }
  if (baud == ZIZHAN_BT_BAUD_INIT) {
    *code_out = ZIZHAN_QCA_BAUD_115200;
    return 1;
  }
  if (baud == ZIZHAN_BT_BAUD_OPER) {
    *code_out = ZIZHAN_QCA_BAUD_3000000;
    return 1;
  }
  return 0;
}

/* Plan SE20 S5 clock rate + divider for 115200 or 3000000 baud:
 * - Non-DFS (dfsr bit 0 == 0): GPLL0_OUT_EVEN (300 MHz, src=6, pre=1, dual-edge mode=2):
 *     115200  -> M=384, N=15625 ->  7,372,800 Hz, clk_div = 64 / sampling (4 at 16x, 2 at 32x)
 *     3000000 -> M=4,   N=25    -> 48,000,000 Hz, clk_div = 16 / sampling (1 at 16x; requires sampling==16)
 * - DFS (dfsr bit 0 == 1): pick the lowest exact-multiple table entry with clk_div <= 4095. */
static inline int zizhan_bt_plan_rcg(uint32_t dfsr, const uint32_t *dfs_perf,
                                     const uint32_t *dfs_m, const uint32_t *dfs_n,
                                     uint32_t cur_cfg, unsigned sampling,
                                     uint32_t baud, ZizhanBtRcgPlan *out) {
  if (!out || (sampling != 16u && sampling != 32u) ||
      (baud != ZIZHAN_BT_BAUD_INIT && baud != ZIZHAN_BT_BAUD_OPER)) {
    return 0;
  }
  memset(out, 0, sizeof(*out));
  out->level = -1;
  const uint64_t unit = (uint64_t)baud * (uint64_t)sampling;
  if (dfsr & 1u) {
    if (!dfs_perf || !dfs_m || !dfs_n) {
      return 0;
    }
    out->dfs_enabled = 1;
    for (unsigned l = 0u; l < 8u; ++l) {
      const uint32_t perf = dfs_perf[l], mreg = dfs_m[l], nreg = dfs_n[l];
      const uint32_t src = (perf >> 8) & 7u, pre = perf & 31u, mode = (perf >> 12) & 3u;
      const uint64_t parent = (src == 0u) ? 19200000ull
                            : (src == 1u) ? 600000000ull
                            : (src == 6u) ? 300000000ull
                                          : 0ull;
      uint64_t rate = pre ? (parent * 2ull / (pre + 1ull)) : parent;
      if (mode == 2u && mreg != 0u) {
        const uint32_t n = ((~nreg) & 0xffffu) + mreg;
        rate = n ? (rate * mreg / n) : 0ull;
      }
      if (rate != 0ull && (rate % unit) == 0ull && (rate / unit) <= 4095ull &&
          (out->level < 0 || rate < out->rate_hz)) {
        out->level = (int)l;
        out->rate_hz = (uint32_t)rate;
        out->clk_div = (uint32_t)(rate / unit);
      }
    }
    return out->level >= 0 ? 1 : 0;
  }

  out->dfs_enabled = 0;
  uint32_t m = 0u, n = 0u, rate = 0u;
  if (baud == ZIZHAN_BT_BAUD_INIT) {
    m = 384u;
    n = 15625u;
    rate = 7372800u;
  } else {
    if (sampling != 16u) {
      return 0;
    }
    m = 4u;
    n = 25u;
    rate = 48000000u;
  }
  out->level = 0;
  out->rate_hz = rate;
  out->clk_div = (uint32_t)(rate / unit);
  out->rcg_cfg = (cur_cfg & ~0x1037ffu) | 1u | (6u << 8) | (2u << 12);
  out->rcg_m = m;
  out->rcg_n = (~(n - m)) & 0xffffu;
  out->rcg_d = (~n) & 0xffffu;
  return 1;
}

static inline unsigned zizhan_bt_edl_set_baud_pkt(uint32_t baud, uint8_t out[5]) {
  uint8_t code = 0u;
  if (!out || !zizhan_bt_qca_baud_code(baud, &code)) {
    return 0u;
  }
  out[0] = 0x01u;
  out[1] = 0x48u;
  out[2] = 0xfcu;
  out[3] = 0x01u;
  out[4] = code;
  return 5u;
}

/* Fix up QCA6490 TLV NVM image:
 * - Tag 17 (HCI config): clear bit 7 (in-band sleep) and set byte 1 to QCA baudrate enum.
 * - Tag 27 (sleep): clear bit 0 (deep sleep). */
static inline int zizhan_bt_nvm_fixup(uint8_t *d, unsigned size, uint32_t baud,
                                      unsigned *hci_out, unsigned *sleep_out) {
  if (hci_out) {
    *hci_out = 0u;
  }
  if (sleep_out) {
    *sleep_out = 0u;
  }
  uint8_t baud_code = 0u;
  if (!d || size < 4u || !zizhan_bt_qca_baud_code(baud, &baud_code)) {
    return 0;
  }
  unsigned off = 0u;
  uint32_t tl = (uint32_t)d[0] | ((uint32_t)d[1] << 8) |
                ((uint32_t)d[2] << 16) | ((uint32_t)d[3] << 24);
  if ((tl & 0xffu) == 4u && size >= 8u) {
    off = 4u;
    tl = (uint32_t)d[4] | ((uint32_t)d[5] << 8) |
         ((uint32_t)d[6] << 16) | ((uint32_t)d[7] << 24);
  }
  if ((tl & 0xffu) != 2u) {
    return 0;
  }
  const unsigned length = tl >> 8;
  const unsigned base = off + 4u;
  unsigned idx = 0u, hci = 0u, sleep = 0u;
  while (idx + 12u <= length && base + idx + 12u <= size) {
    uint8_t *t = d + base + idx;
    const unsigned id = (unsigned)t[0] | ((unsigned)t[1] << 8);
    const unsigned len = (unsigned)t[2] | ((unsigned)t[3] << 8);
    if (base + idx + 12u + len > size) {
      break;
    }
    if (id == 17u && len >= 3u) {
      t[12] &= (uint8_t)~0x80u;
      t[13] = baud_code;
      ++hci;
    }
    if (id == 27u && len >= 1u) {
      t[12] &= (uint8_t)~0x01u;
      ++sleep;
    }
    idx += 12u + len;
  }
  if (hci_out) {
    *hci_out = hci;
  }
  if (sleep_out) {
    *sleep_out = sleep;
  }
  return 1;
}

typedef struct {
  uint8_t pkt[1100];
  unsigned have;
  unsigned need;
  unsigned logged;
  unsigned shimmed;
  unsigned forwarded;
} ZizhanBtH4Tx;

typedef struct {
  void *ctx;
  int (*uart_tx)(void *ctx, const uint8_t *data, unsigned len);
  void (*host_rx)(void *ctx, const uint8_t *data, unsigned len);
  void (*log_cmd)(void *ctx, uint16_t op, uint8_t plen, int shimmed);
} ZizhanBtH4Ops;

static inline void zizhan_bt_h4_complete(ZizhanBtH4Tx *h4, const ZizhanBtH4Ops *ops) {
  const uint8_t type = h4->pkt[0];
  if (type == 0x01u && h4->have >= 4u) {
    const uint16_t op = (uint16_t)((uint16_t)h4->pkt[1] | ((uint16_t)h4->pkt[2] << 8));
    const int vendor = ((op >> 10) == 0x3fu) ? 1 : 0;
    if (h4->logged < 48u) {
      ++h4->logged;
      if (ops->log_cmd) {
        ops->log_cmd(ops->ctx, op, h4->pkt[3], vendor);
      }
    }
    if (vendor) {
      uint8_t cc[7 + 31];
      memset(cc, 0, sizeof(cc));
      cc[0] = 0x04u;
      cc[1] = 0x0eu;
      cc[2] = (uint8_t)(4u + 31u);
      cc[3] = 0x01u;
      cc[4] = (uint8_t)op;
      cc[5] = (uint8_t)(op >> 8);
      cc[6] = 0x00u;
      for (unsigned i = 0u; i < h4->pkt[3] && i < 31u && (4u + i) < h4->have; ++i) {
        cc[7u + i] = h4->pkt[4u + i];
      }
      ops->host_rx(ops->ctx, cc, sizeof(cc));
      ++h4->shimmed;
      return;
    }
    if (op == 0x204au) {
      const uint8_t cc[8] = {0x04u, 0x0eu, 0x05u, 0x01u, 0x4au, 0x20u, 0x00u, 0x03u};
      ops->host_rx(ops->ctx, cc, sizeof(cc));
      ++h4->shimmed;
      return;
    }
    if (op == 0x0c5bu || op == 0x0c5au) {
      const uint8_t cc[8] = {
          0x04u, 0x0eu, (uint8_t)(op == 0x0c5au ? 0x05u : 0x04u), 0x01u,
          (uint8_t)op, (uint8_t)(op >> 8), 0x00u, 0x00u,
      };
      ops->host_rx(ops->ctx, cc, op == 0x0c5au ? 8u : 7u);
      ++h4->shimmed;
      return;
    }
  }
  ++h4->forwarded;
  (void)ops->uart_tx(ops->ctx, h4->pkt, h4->have);
}

static inline void zizhan_bt_h4_feed(ZizhanBtH4Tx *h4, const ZizhanBtH4Ops *ops,
                                     const uint8_t *b, unsigned n) {
  for (unsigned i = 0u; i < n; ++i) {
    if (h4->have == 0u) {
      const uint8_t t = b[i];
      if (t != 0x01u && t != 0x02u && t != 0x03u) {
        const uint8_t one = t;
        (void)ops->uart_tx(ops->ctx, &one, 1u);
        continue;
      }
      h4->need = (t == 0x02u) ? 5u : 4u;
    }
    h4->pkt[h4->have++] = b[i];
    if (h4->have == h4->need) {
      const uint8_t t = h4->pkt[0];
      const unsigned body = (t == 0x01u) ? h4->pkt[3]
                          : (t == 0x02u) ? ((unsigned)h4->pkt[3] | ((unsigned)h4->pkt[4] << 8))
                                         : h4->pkt[3];
      if (h4->need == ((t == 0x02u) ? 5u : 4u)) {
        h4->need += body;
      }
      if (h4->need > sizeof(h4->pkt)) {
        (void)ops->uart_tx(ops->ctx, h4->pkt, h4->have);
        h4->have = 0u;
        continue;
      }
    }
    if (h4->have == h4->need && h4->have >= ((h4->pkt[0] == 0x02u) ? 5u : 4u)) {
      zizhan_bt_h4_complete(h4, ops);
      h4->have = 0u;
    }
  }
}

typedef struct {
  unsigned state;
  unsigned param;
  uint32_t patched;
} ZizhanBtRxPatch;

static inline void zizhan_bt_rx_patch(ZizhanBtRxPatch *rxp, uint8_t *b, unsigned n) {
  static const uint8_t kHead[7] = {0x04u, 0x0eu, 0x44u, 0x01u, 0x02u, 0x10u, 0x00u};
  for (unsigned i = 0u; i < n; ++i) {
    if (rxp->state < sizeof(kHead)) {
      if (b[i] == kHead[rxp->state]) {
        ++rxp->state;
      } else {
        rxp->state = (b[i] == kHead[0]) ? 1u : 0u;
      }
      rxp->param = 0u;
      continue;
    }
    if (rxp->param == 18u) {
      b[i] = (uint8_t)(b[i] | 0x0cu);
      ++rxp->patched;
    }
    if (++rxp->param >= 64u) {
      rxp->state = 0u;
    }
  }
}

typedef struct {
  uint8_t hdr[5];
  unsigned have;
  unsigned hdr_len;
  unsigned need;
  uint8_t asleep;
  uint8_t acked;
  uint32_t wake_ind;
  uint32_t wake_ack;
  uint32_t sleep_ind;
  uint32_t wakes;
  uint32_t wake_fail;
} ZizhanBtIbs;

static inline unsigned zizhan_bt_ibs_rx(ZizhanBtIbs *ibs,
                                        int (*uart_tx)(void *ctx, const uint8_t *data, unsigned len),
                                        void *ctx, const uint8_t *in, unsigned n, uint8_t *out) {
  unsigned k = 0u;
  for (unsigned i = 0u; i < n; ++i) {
    const uint8_t b = in[i];
    if (ibs->hdr_len == 0u) {
      if (b == 0xfdu) {
        ++ibs->wake_ind;
        const uint8_t ack = 0xfcu;
        if (uart_tx) {
          (void)uart_tx(ctx, &ack, 1u);
        }
        ibs->asleep = 0u;
        continue;
      }
      if (b == 0xfeu) {
        ++ibs->sleep_ind;
        ibs->asleep = 1u;
        continue;
      }
      if (b == 0xfcu) {
        ++ibs->wake_ack;
        ibs->acked = 1u;
        ibs->asleep = 0u;
        continue;
      }
      const unsigned hl = (b == 0x04u) ? 3u
                        : (b == 0x02u) ? 5u
                        : (b == 0x03u) ? 4u
                        : (b == 0x05u) ? 5u
                                       : 0u;
      out[k++] = b;
      if (!hl) {
        continue;
      }
      ibs->hdr[0] = b;
      ibs->have = 1u;
      ibs->hdr_len = hl;
      ibs->need = hl;
      ibs->asleep = 0u;
      continue;
    }
    out[k++] = b;
    if (ibs->have < ibs->hdr_len) {
      ibs->hdr[ibs->have] = b;
    }
    ++ibs->have;
    if (ibs->have == ibs->hdr_len) {
      const uint8_t *h = ibs->hdr;
      const unsigned body = (h[0] == 0x04u) ? h[2]
                          : (h[0] == 0x03u) ? h[3]
                          : (h[0] == 0x02u) ? ((unsigned)h[3] | ((unsigned)h[4] << 8))
                                            : (((unsigned)h[3] | ((unsigned)h[4] << 8)) & 0x3fffu);
      ibs->need = ibs->hdr_len + body;
    }
    if (ibs->have >= ibs->hdr_len && ibs->have == ibs->need) {
      ibs->hdr_len = 0u;
      ibs->have = 0u;
    }
  }
  return k;
}

#ifdef __cplusplus
}
#endif

#endif
