/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanBtProtocol.h"

typedef struct {
  uint8_t uart_out[2048];
  unsigned uart_len;
  uint8_t host_in[2048];
  unsigned host_len;
  unsigned cmd_logs;
} MockBridge;

static int mock_uart_tx(void *ctx, const uint8_t *data, unsigned len) {
  MockBridge *m = (MockBridge *)ctx;
  assert(m->uart_len + len <= sizeof(m->uart_out));
  memcpy(m->uart_out + m->uart_len, data, len);
  m->uart_len += len;
  return 1;
}

static void mock_host_rx(void *ctx, const uint8_t *data, unsigned len) {
  MockBridge *m = (MockBridge *)ctx;
  assert(m->host_len + len <= sizeof(m->host_in));
  memcpy(m->host_in + m->host_len, data, len);
  m->host_len += len;
}

static void mock_log_cmd(void *ctx, uint16_t op, uint8_t plen, int shimmed) {
  (void)op;
  (void)plen;
  (void)shimmed;
  ++((MockBridge *)ctx)->cmd_logs;
}

int main(void) {
  /* 1. SE20 non-DFS RCG planning: 115200 -> 7.3728 MHz (div=4) and 3 Mbps -> 48 MHz (div=1). */
  ZizhanBtRcgPlan p = {};
  assert(zizhan_bt_plan_rcg(0u, NULL, NULL, NULL, 0x00000000u, 16u, 115200u, &p) == 1);
  assert(p.dfs_enabled == 0 && p.level == 0);
  assert(p.rate_hz == 7372800u && p.clk_div == 4u);
  assert(p.rcg_m == 384u);
  assert(p.rcg_n == ((~15241u) & 0xffffu));
  assert(p.rcg_d == ((~15625u) & 0xffffu));
  assert(p.rcg_cfg == 0x2601u);

  assert(zizhan_bt_plan_rcg(0u, NULL, NULL, NULL, 0x00000000u, 16u, 3000000u, &p) == 1);
  assert(p.dfs_enabled == 0 && p.level == 0);
  assert(p.rate_hz == 48000000u && p.clk_div == 1u);
  assert(p.rcg_m == 4u);
  assert(p.rcg_n == ((~21u) & 0xffffu));
  assert(p.rcg_d == ((~25u) & 0xffffu));
  assert(p.rcg_cfg == 0x2601u);
  /* 32x oversampling cannot produce 96 MHz cleanly from 300 MHz dual-edge here -> rejected so caller falls back. */
  assert(zizhan_bt_plan_rcg(0u, NULL, NULL, NULL, 0u, 32u, 3000000u, &p) == 0);

  /* 2. SE20 DFS table planning: chooses exact multiple for 115200 and 3000000, rejects missing 3000000 entry. */
  uint32_t perf[8] = {0};
  uint32_t mreg[8] = {0};
  uint32_t nreg[8] = {0};
  perf[2] = 1u | (6u << 8) | (2u << 12);
  mreg[2] = 384u;
  nreg[2] = (~(15625u - 384u)) & 0xffffu;
  assert(zizhan_bt_plan_rcg(1u, perf, mreg, nreg, 0u, 16u, 115200u, &p) == 1);
  assert(p.dfs_enabled == 1 && p.level == 2 && p.rate_hz == 7372800u && p.clk_div == 4u);
  assert(zizhan_bt_plan_rcg(1u, perf, mreg, nreg, 0u, 16u, 3000000u, &p) == 0);

  perf[6] = 1u | (6u << 8) | (2u << 12);
  mreg[6] = 4u;
  nreg[6] = (~(25u - 4u)) & 0xffffu;
  assert(zizhan_bt_plan_rcg(1u, perf, mreg, nreg, 0u, 16u, 3000000u, &p) == 1);
  assert(p.dfs_enabled == 1 && p.level == 6 && p.rate_hz == 48000000u && p.clk_div == 1u);

  /* 3. EDL_CHANGE_BAUDRATE packet & NVM Tag 17 / Tag 27 fixup for 3 Mbps and 115200 fallback. */
  uint8_t edl[5] = {0};
  assert(zizhan_bt_edl_set_baud_pkt(3000000u, edl) == 5u);
  const uint8_t expect_3m[5] = {0x01u, 0x48u, 0xfcu, 0x01u, 0x0eu};
  assert(memcmp(edl, expect_3m, 5u) == 0);
  assert(zizhan_bt_edl_set_baud_pkt(115200u, edl) == 5u);
  assert(edl[4] == 0x00u);
  assert(zizhan_bt_edl_set_baud_pkt(921600u, edl) == 0u);
  assert(ZIZHAN_GENI_MANUAL_RFR_NOT_READY == 0x80000002u);
  assert(ZIZHAN_GENI_STATUS_S_ACTIVE == 0x1000u);
  assert(ZIZHAN_GENI_S_IRQ_CMD_CANCEL == 0x10u);

  /* Realistic Mix Fold 2 hpnv21g.bin header (type 4 wrapper + type 2 NVM + Tag 17 + Tag 27). */
  uint8_t nvm[64];
  memset(nvm, 0, sizeof(nvm));
  const uint32_t body_len = (12u + 6u) + (12u + 11u);
  nvm[0] = 0x04u;
  nvm[4] = 0x02u;
  nvm[5] = (uint8_t)body_len;
  /* Tag 17 @ offset 8: factory payload [0x95 (IBS bit7 set), 0x11 (3.2M), 0x22, 0x00, 0x2e, 0x00] */
  nvm[8] = 17u;
  nvm[10] = 6u;
  nvm[20] = 0x95u;
  nvm[21] = 0x11u;
  nvm[22] = 0x22u;
  nvm[24] = 0x2eu;
  /* Tag 27 @ offset 26: factory payload [0x01 (deep sleep), 0x01, ...] */
  nvm[26] = 27u;
  nvm[28] = 11u;
  nvm[38] = 0x01u;
  nvm[39] = 0x01u;

  unsigned hci_cnt = 0u, sleep_cnt = 0u;
  assert(zizhan_bt_nvm_fixup(nvm, 8u + body_len, 3000000u, &hci_cnt, &sleep_cnt) == 1);
  assert(hci_cnt == 1u && sleep_cnt == 1u);
  assert(nvm[20] == 0x15u); /* IBS bit cleared */
  assert(nvm[21] == 0x0eu); /* QCA_BAUDRATE_3000000 */
  assert(nvm[38] == 0x00u && nvm[39] == 0x01u);

  nvm[20] = 0x95u;
  nvm[38] = 0x01u;
  assert(zizhan_bt_nvm_fixup(nvm, 8u + body_len, 115200u, &hci_cnt, &sleep_cnt) == 1);
  assert(nvm[20] == 0x15u && nvm[21] == 0x00u && nvm[38] == 0x00u);

  /* 4. H4 TX shim: Broadcom VSC echo (fd3c), LE periodic adv list size (204a), err-data reporting (0c5a/0c5b), standard HCI & ACL forward. */
  MockBridge mb;
  memset(&mb, 0, sizeof(mb));
  ZizhanBtH4Tx h4;
  memset(&h4, 0, sizeof(h4));
  const ZizhanBtH4Ops ops = {&mb, mock_uart_tx, mock_host_rx, mock_log_cmd};

  const uint8_t vsc_fd3c[] = {0x01u, 0x3cu, 0xfdu, 0x04u, 0xa1u, 0xb2u, 0xc3u, 0xd4u};
  zizhan_bt_h4_feed(&h4, &ops, vsc_fd3c, sizeof(vsc_fd3c));
  assert(h4.shimmed == 1u && h4.forwarded == 0u && mb.uart_len == 0u);
  assert(mb.host_len == 38u);
  assert(mb.host_in[0] == 0x04u && mb.host_in[1] == 0x0eu && mb.host_in[4] == 0x3cu && mb.host_in[5] == 0xfdu && mb.host_in[6] == 0x00u);
  assert(mb.host_in[7] == 0xa1u && mb.host_in[10] == 0xd4u && mb.host_in[11] == 0x00u);

  mb.host_len = 0u;
  const uint8_t cmd_204a[] = {0x01u, 0x4au, 0x20u, 0x00u};
  const uint8_t cmd_0c5a[] = {0x01u, 0x5au, 0x0cu, 0x00u};
  const uint8_t cmd_reset[] = {0x01u, 0x03u, 0x0cu, 0x00u};
  const uint8_t acl_pkt[] = {0x02u, 0x40u, 0x00u, 0x03u, 0x00u, 0xdeu, 0xadu, 0xfdu};
  zizhan_bt_h4_feed(&h4, &ops, cmd_204a, sizeof(cmd_204a));
  zizhan_bt_h4_feed(&h4, &ops, cmd_0c5a, sizeof(cmd_0c5a));
  zizhan_bt_h4_feed(&h4, &ops, cmd_reset, sizeof(cmd_reset));
  zizhan_bt_h4_feed(&h4, &ops, acl_pkt, sizeof(acl_pkt));
  assert(h4.shimmed == 3u && h4.forwarded == 2u);
  assert(mb.host_len == 16u && mb.host_in[7] == 0x03u && mb.host_in[15] == 0x00u);
  assert(mb.uart_len == sizeof(cmd_reset) + sizeof(acl_pkt));

  /* 5. RX patcher across arbitrary chunk split: sets bits 2..3 on Supported Commands octet 18 only. */
  ZizhanBtRxPatch rxp = {0};
  uint8_t sup_ev[7 + 64];
  memset(sup_ev, 0, sizeof(sup_ev));
  const uint8_t head[7] = {0x04u, 0x0eu, 0x44u, 0x01u, 0x02u, 0x10u, 0x00u};
  memcpy(sup_ev, head, 7u);
  sup_ev[7 + 18] = 0x83u;
  zizhan_bt_rx_patch(&rxp, sup_ev, 10u);
  zizhan_bt_rx_patch(&rxp, sup_ev + 10u, sizeof(sup_ev) - 10u);
  assert(rxp.patched == 1u && sup_ev[7 + 18] == 0x8fu && rxp.state == 0u);

  /* 6. IBS framing: strips 0xfe/0xfd/0xfc between packets (acking 0xfd with 0xfc) while preserving 0xfd/0xfe inside payload. */
  memset(&mb, 0, sizeof(mb));
  ZizhanBtIbs ibs;
  memset(&ibs, 0, sizeof(ibs));
  const uint8_t rx_stream[] = {
      0xfeu,                                     /* SLEEP_IND */
      0xfdu,                                     /* WAKE_IND -> sends 0xfc */
      0x04u, 0x0eu, 0x04u, 0xfdu, 0xfeu, 0xfcu, 0x00u, /* H4 event containing 0xfd/0xfe/0xfc payload bytes! */
      0xfcu,                                     /* WAKE_ACK */
  };
  uint8_t clean[32] = {0};
  const unsigned k = zizhan_bt_ibs_rx(&ibs, mock_uart_tx, &mb, rx_stream, sizeof(rx_stream), clean);
  assert(ibs.sleep_ind == 1u && ibs.wake_ind == 1u && ibs.wake_ack == 1u);
  assert(ibs.acked == 1u && ibs.asleep == 0u);
  assert(mb.uart_len == 1u && mb.uart_out[0] == 0xfcu);
  const uint8_t expect_ev[7] = {0x04u, 0x0eu, 0x04u, 0xfdu, 0xfeu, 0xfcu, 0x00u};
  assert(k == 7u && memcmp(clean, expect_ev, 7u) == 0);

  /* 7. Stage 6 pairing readiness: Classic SSP, BLE LESC, LE Privacy Resolving List, and L2CAP SMP ACL
   *    frames (including 0xfc/0xfd/0xfe key material bytes) must forward 100% intact without shimming. */
  memset(&mb, 0, sizeof(mb));
  memset(&h4, 0, sizeof(h4));
  /* Classic SSP: Link Key Request Reply (0x040b, 22B), Set Connection Encryption (0x0413, 3B),
   * IO Capability Request Reply (0x042b, 9B), User Confirmation Request Reply (0x042c, 6B). */
  uint8_t ssp_lk_reply[4 + 22] = {0x01u, 0x0bu, 0x04u, 22u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0xfcu, 0xfdu, 0xfeu};
  const uint8_t ssp_enc[4 + 3] = {0x01u, 0x13u, 0x04u, 0x03u, 0x40u, 0x00u, 0x01u};
  const uint8_t ssp_iocap[4 + 9] = {0x01u, 0x2bu, 0x04u, 0x09u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u, 0x01u, 0x00u, 0x03u};
  const uint8_t ssp_conf[4 + 6] = {0x01u, 0x2cu, 0x04u, 0x06u, 0x11u, 0x22u, 0x33u, 0x44u, 0x55u, 0x66u};
  /* BLE LESC & Privacy Resolving List: LE Enable Encryption (0x2019, 28B), LE LTK Reply (0x201a, 18B),
   * LE Read Local P-256 PubKey (0x2025, 0B), LE Generate DHKey (0x2026, 64B),
   * LE Add Device To Resolving List (0x2027, 39B), LE Set Address Resolution Enable (0x202d, 1B),
   * LE Set Privacy Mode (0x204e, 8B). */
  uint8_t le_enc[4 + 28] = {0x01u, 0x19u, 0x20u, 28u, 0x40u, 0x00u};
  le_enc[16] = 0xfdu;
  le_enc[17] = 0xfeu;
  uint8_t le_ltk[4 + 18] = {0x01u, 0x1au, 0x20u, 18u, 0x40u, 0x00u, 0xfcu};
  const uint8_t le_p256[4] = {0x01u, 0x25u, 0x20u, 0x00u};
  uint8_t le_dhkey[4 + 64] = {0x01u, 0x26u, 0x20u, 64u, 0xfdu, 0xfeu, 0xfcu};
  uint8_t le_rlist_add[4 + 39] = {0x01u, 0x27u, 0x20u, 39u, 0x01u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu, 0xfdu, 0xfeu};
  const uint8_t le_rlist_en[4 + 1] = {0x01u, 0x2du, 0x20u, 0x01u, 0x01u};
  const uint8_t le_priv_mode[4 + 8] = {0x01u, 0x4eu, 0x20u, 0x08u, 0x01u, 0xaau, 0xbbu, 0xccu, 0xddu, 0xeeu, 0xffu, 0x01u};
  /* L2CAP SMP Pairing Public Key ACL packet (type 0x02, 5B header + 69B L2CAP/SMP body containing 0xfc/0xfd/0xfe). */
  uint8_t smp_acl[5 + 69];
  memset(smp_acl, 0x5au, sizeof(smp_acl));
  smp_acl[0] = 0x02u;
  smp_acl[1] = 0x40u;
  smp_acl[2] = 0x20u;
  smp_acl[3] = 69u;
  smp_acl[4] = 0x00u;
  smp_acl[10] = 0xfcu;
  smp_acl[20] = 0xfdu;
  smp_acl[30] = 0xfeu;

  zizhan_bt_h4_feed(&h4, &ops, ssp_lk_reply, sizeof(ssp_lk_reply));
  zizhan_bt_h4_feed(&h4, &ops, ssp_enc, sizeof(ssp_enc));
  zizhan_bt_h4_feed(&h4, &ops, ssp_iocap, sizeof(ssp_iocap));
  zizhan_bt_h4_feed(&h4, &ops, ssp_conf, sizeof(ssp_conf));
  zizhan_bt_h4_feed(&h4, &ops, le_enc, sizeof(le_enc));
  zizhan_bt_h4_feed(&h4, &ops, le_ltk, sizeof(le_ltk));
  zizhan_bt_h4_feed(&h4, &ops, le_p256, sizeof(le_p256));
  zizhan_bt_h4_feed(&h4, &ops, le_dhkey, sizeof(le_dhkey));
  zizhan_bt_h4_feed(&h4, &ops, le_rlist_add, sizeof(le_rlist_add));
  zizhan_bt_h4_feed(&h4, &ops, le_rlist_en, sizeof(le_rlist_en));
  zizhan_bt_h4_feed(&h4, &ops, le_priv_mode, sizeof(le_priv_mode));
  zizhan_bt_h4_feed(&h4, &ops, smp_acl, sizeof(smp_acl));

  const unsigned expect_pair_len =
      (unsigned)(sizeof(ssp_lk_reply) + sizeof(ssp_enc) + sizeof(ssp_iocap) + sizeof(ssp_conf) +
                 sizeof(le_enc) + sizeof(le_ltk) + sizeof(le_p256) + sizeof(le_dhkey) +
                 sizeof(le_rlist_add) + sizeof(le_rlist_en) + sizeof(le_priv_mode) + sizeof(smp_acl));
  assert(h4.shimmed == 0u && h4.forwarded == 12u && mb.host_len == 0u);
  assert(mb.uart_len == expect_pair_len);
  assert(memcmp(mb.uart_out + expect_pair_len - sizeof(smp_acl), smp_acl, sizeof(smp_acl)) == 0);

  /* Inbound SMP ACL frame over IBS with 0xfc/0xfd/0xfe key bytes must not trigger spurious WAKE_ACK or byte loss. */
  memset(&mb, 0, sizeof(mb));
  memset(&ibs, 0, sizeof(ibs));
  uint8_t smp_rx_clean[128] = {0};
  const unsigned smp_rx_n = zizhan_bt_ibs_rx(&ibs, mock_uart_tx, &mb, smp_acl, sizeof(smp_acl), smp_rx_clean);
  assert(smp_rx_n == sizeof(smp_acl));
  assert(memcmp(smp_rx_clean, smp_acl, sizeof(smp_acl)) == 0);
  assert(ibs.wake_ind == 0u && ibs.sleep_ind == 0u && ibs.wake_ack == 0u && mb.uart_len == 0u);

  printf("PASS bt-hci-shim\n");
  return 0;
}
