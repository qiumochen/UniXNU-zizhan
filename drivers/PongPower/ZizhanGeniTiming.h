/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_GENI_TIMING_H
#define ZIZHAN_GENI_TIMING_H
#include "GeniI2c.h"
namespace zizhan_geni {
enum Result { Ok, RootBusy, NoXo, Protocol, Busy, Fifo, PrepareFailed, Readback };
struct Receipt {
    uint32_t root = 0, current = 0, dfs = 0, selected = 0;
    uint32_t level = 0, tx_parameter = 0, rx_parameter = 0;
    uint32_t config = 0;
    bool retained = false;
    bool clock_valid = false;
};
inline bool xo(uint32_t cfg) {
    // Matched parent map: source0=bi_tcxo. Mode0, HID0/1 both divide1.
    // Saved live xo_board76.8MHz / matched RPMh divider4 =19.2MHz.
    return !(cfg & 0x700) && !(cfg & 0x3000) && (cfg & 31) <= 1;
}
template <typename Root>
Result select(Root &gcc, Receipt &r) {
    r.clock_valid = false;
    // cmd_rcgr=2e014, DFS enable+14, eight perf CFG slots+1c (fixed BSP).
    r.root = gcc.read(0x14); r.current = gcc.read(0x18); r.dfs = gcc.read(0x28);
    if (r.root & 1) return RootBusy;
    if (r.dfs & 1) {
        for (unsigned i = 0; i < 8; ++i) {
            const uint32_t cfg = gcc.read(0x30 + 4 * i);
            if (xo(cfg)) { r.level = i; r.selected = cfg; r.clock_valid = true; return Ok; }
        }
        return NoXo;
    }
    // Without DFS, CLK_SEL cannot select a new root rate. Only accept an
    // already matching root; never write GCC RCGR/PLL to force a fit.
    if (!xo(r.current)) return NoXo;
    r.level = 0; r.selected = r.current; r.clock_valid = true; return Ok;
}
template <typename Engine>
Result prepare(Engine &se, Receipt &r) {
    if (!r.clock_valid || r.level >= 8 || !xo(r.selected)) return NoXo;
    if (((se.read(GENI_FW_REVISION) >> 8) & 255) != GENI_PROTO_I2C || (se.read(GENI_IF_DISABLE) & 1)) return Protocol;
    if ((se.read(0x40) & 0x1001) || se.read(GENI_M_IRQ_STATUS) || (se.read(GENI_RX_FIFO_STATUS) & 0x1ffffff)) return Busy;
    r.tx_parameter = se.read(0xe24); r.rx_parameter = se.read(0xe28);
    const uint32_t parameters[] = {r.tx_parameter, r.rx_parameter};
    for (uint32_t p : parameters) {
        if (((p >> 24) & 63) != 32 || ((p >> 16) & 63) < 2) return Fifo;
    }
    // Fixed BSP nominal100kHz timing (divider7/high10/low11/cycle26).
    // This is configured timing, not a measured electrical bus frequency.
    r.retained = true;
    se.write(0x7c, r.level); se.write(GENI_CLK_CFG, 0x71);
    se.write(GENI_SCL_COUNTERS, (10u << 20) | (11u << 10) | 26);
    GeniI2c bus = {};
    GeniIo io = {
        [](void *p, uint32_t at) { return static_cast<Engine *>(p)->read(at); },
        [](void *p, uint32_t at, uint32_t value) { static_cast<Engine *>(p)->write(at, value); }, &se};
    if (geni_i2c_prepare_poll(&bus, &io, 0x40, &r.config)) return PrepareFailed;
    if (se.read(0x7c) != r.level || se.read(GENI_CLK_CFG) != 0x71 ||
        se.read(GENI_SCL_COUNTERS) != ((10u << 20) | (11u << 10) | 26) ||
        se.read(GENI_DMA_MODE) || se.read(GENI_BYTE_GRAN) ||
        se.read(GENI_TX_PACK0) != 0x7f8fe || se.read(GENI_TX_PACK1) != 0xffefe ||
        se.read(GENI_RX_PACK0) != 0x7f8fe || se.read(GENI_RX_PACK1) != 0xffefe ||
        se.read(GENI_TX_WATERMARK) || se.read(GENI_M_IRQ_EN) != GENI_M_IRQ_WANTED ||
        (se.read(GENI_SE_IRQ_EN) & 15)) return Readback;
    return Ok;
}
struct RxReceipt {
    uint32_t tx_parameter = 0, rx_parameter = 0;
    uint32_t watermark_before = 0, rfr_before = 0, watermark_after = 0, rfr_after = 0;
    unsigned writes = 0;
};
/* Separate opt-in RX setup, before any I2C command/reset release. Fixed
 * vendor cb7a356... drivers/platform/msm/msm-geni-se.c geni_se_init writes
 * RX watermark/RFR; its I2C caller supplies TXdepth-1/TXdepth. Both actual
 * zizhan FIFOs have32-bit width/depth16. Import only these threshold writes,
 * not the helper's output-control,IRQ enabling or blanket IRQ clearing. */
template <typename Engine>
Result prepare_rx(Engine &se, const Receipt &timing, RxReceipt &r) {
    if (!timing.clock_valid) return NoXo;
    if (((se.read(GENI_FW_REVISION) >> 8) & 255) != GENI_PROTO_I2C ||
        (se.read(GENI_IF_DISABLE) & 1) || se.read(GENI_DMA_MODE)) return Protocol;
    if ((se.read(0x40) & 0x1001) || se.read(GENI_M_IRQ_STATUS) ||
        (se.read(GENI_RX_FIFO_STATUS) & 0x1ffffff)) return Busy;
    r.tx_parameter = se.read(0xe24); r.rx_parameter = se.read(0xe28);
    const uint32_t parameters[] = {r.tx_parameter, r.rx_parameter};
    for (uint32_t value : parameters)
        if (((value >> 24) & 63) != 32 || ((value >> 16) & 63) != 16) return Fifo;
    r.watermark_before = se.read(0x810); r.rfr_before = se.read(0x814);
    const uint32_t target_wm = ((r.tx_parameter >> 16) & 63) - 1;
    const uint32_t target_rfr = (r.tx_parameter >> 16) & 63;
    if (r.watermark_before != target_wm) { ++r.writes; se.write(0x810, target_wm); }
    r.watermark_after = se.read(0x810);
    if (r.watermark_after != target_wm) return Readback;
    if (r.rfr_before != target_rfr) { ++r.writes; se.write(0x814, target_rfr); }
    r.rfr_after = se.read(0x814);
    if (r.rfr_after != target_rfr) return Readback;
    if ((se.read(0x40) & 0x1001) || se.read(GENI_M_IRQ_STATUS) ||
        (se.read(GENI_RX_FIFO_STATUS) & 0x1ffffff)) return Busy;
    return Ok;
}
} // namespace zizhan_geni
#endif
