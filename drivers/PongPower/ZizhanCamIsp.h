/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-C SM8475 CAM-01 Titan 680 ISP/MIPI register programming, OV13B10 1364x768 RAW10 mode table,
 * MIPI RAW10 unpacker, and frame statistics/preview builder (zero OS headers; host-tested). */
#ifndef ZIZHAN_CAM_ISP_H
#define ZIZHAN_CAM_ISP_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "ZizhanCamCc.h"
#include "ZizhanCci.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZIZHAN_CAM_DT_RAW10       = 0x2bu,
    ZIZHAN_CAM_DEC_PAYLOAD    = 0x0fu,
    ZIZHAN_CAM_OV13B10_ADDR7  = 0x36u,
    ZIZHAN_CAM_OV13B10_W      = 1364u,
    ZIZHAN_CAM_OV13B10_H      = 768u,
    /* MIPI RAW10 packed row: ceil(1364 * 10 / 8) = 1705 -> 16-byte aligned stride = 1712 */
    ZIZHAN_CAM_OV13B10_STRIDE = 1712u,
    ZIZHAN_CAM_OV13B10_BYTES  = 1712u * 768u,
    /* Sony IMX766 50MP 1/1.56" Main Camera (4:1 binned 2048x1536 RAW10 on CSIPHY0 3-trio C-PHY) */
    ZIZHAN_CAM_IMX766_ADDR7   = 0x1au,
    ZIZHAN_CAM_DW9800V_ADDR7  = 0x0cu,
    ZIZHAN_CAM_RUMBAS4H_ADDR7 = 0x24u,
    ZIZHAN_CAM_IMX766_W       = 2048u,
    ZIZHAN_CAM_IMX766_H       = 1536u,
    ZIZHAN_CAM_IMX766_STRIDE  = 2560u,
    ZIZHAN_CAM_IMX766_BYTES   = 2560u * 1536u
};

/* Configure & lock SM8475 Lucid OLE CAM_CC_PLL0 at 1200 MHz (OUT_EVEN=600 MHz, OUT_ODD=400 MHz). */
static inline int zizhan_camcc_pll0_enable(const ZizhanCamCcOps *o) {
    if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return ZizhanCamCcArgument;
    const uint32_t mode0 = o->read(o->context, 0x00u);
    const uint32_t op0   = o->read(o->context, 0x04u);
    if ((mode0 & 1u) && (op0 & 1u) && (mode0 & 0x80000000u)) {
        o->write(o->context, 0x18u, o->read(o->context, 0x18u) | 0x7u);
        return ZizhanCamCcOk;
    }
    /* sm8475_cam_cc_pll0_config (camcc-sm8450.c + clk-alpha-pll.c LUCID_OLE) */
    o->write(o->context, 0x10u, 0x4444003eu);   /* L=0x3e, CAL_L=0x44, RINGOSC_CAL_L=0x44 */
    o->write(o->context, 0x14u, 0x00008000u);   /* ALPHA=0x8000 -> 19.2 MHz * 62.5 = 1200 MHz */
    o->write(o->context, 0x20u, 0x20485699u);   /* CONFIG_CTL */
    o->write(o->context, 0x24u, 0x00182261u);   /* CONFIG_CTL_U */
    o->write(o->context, 0x28u, 0x82aa299cu);   /* CONFIG_CTL_U1 */
    o->write(o->context, 0x18u, 0x00008400u);   /* USER_CTL: even=/2 (600), odd=/3 (400) */
    o->write(o->context, 0x1cu, 0x00000005u);   /* USER_CTL_U */
    o->write(o->context, 0x2cu, 0x00000000u);   /* TEST_CTL */
    o->write(o->context, 0x30u, 0x00000003u);   /* TEST_CTL_U */
    o->write(o->context, 0x34u, 0x00009000u);   /* TEST_CTL_U1 */
    o->write(o->context, 0x38u, 0x00000034u);   /* TEST_CTL_U2 */

    o->write(o->context, 0x00u, o->read(o->context, 0x00u) & ~1u);
    o->write(o->context, 0x04u, 0u);            /* STANDBY */
    o->write(o->context, 0x00u, o->read(o->context, 0x00u) | 4u);   /* PLL_RESET_N */
    o->write(o->context, 0x04u, 1u);            /* PLL_RUN */

    unsigned locked = 0;
    for (unsigned i = 0; i < o->polls; ++i) {
        if (o->read(o->context, 0x00u) & 0x80000000u) { locked = 1; break; }
        o->pause_us(o->context, 10);
    }
    if (!locked) return ZizhanCamCcTimeout;
    o->write(o->context, 0x18u, o->read(o->context, 0x18u) | 0x7u); /* PLL_OUT_MASK */
    o->write(o->context, 0x00u, o->read(o->context, 0x00u) | 1u);   /* PLL_OUTCTRL */
    return ZizhanCamCcOk;
}

static inline int zizhan_camcc_rcg_cfg(const ZizhanCamCcOps *o, uint32_t rcg, uint32_t parent_sel, uint32_t hid_div) {
    if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return ZizhanCamCcArgument;
    const uint32_t cfg = o->read(o->context, rcg + 4u);
    o->write(o->context, rcg + 4u, (cfg & ~0x1037ffu) | ((parent_sel & 7u) << 8) | (hid_div & 0x1fu));
    o->write(o->context, rcg, o->read(o->context, rcg) | 1u);
    for (unsigned i = 0; i < o->polls; ++i) {
        if (!(o->read(o->context, rcg) & 1u)) return ZizhanCamCcOk;
        o->pause_us(o->context, 10);
    }
    return ZizhanCamCcTimeout;
}

/* Bring up CAM_CC 400 MHz clocks for CSIPHY1 + TPG1 + CSID-lite0 + IFE-lite0 + CAMNOC AXI. */
static inline int zizhan_camcc_isp_clocks_on(const ZizhanCamCcOps *o, unsigned *stage_out) {
    if (stage_out) *stage_out = 0;
    if (zizhan_camcc_gdsc(o, ZizhanCamCcTitanGdscr, 1)) return ZizhanCamCcTimeout;
    if (stage_out) *stage_out = 1;
    if (zizhan_camcc_pll0_enable(o)) return ZizhanCamCcTimeout;
    if (stage_out) *stage_out = 2;
    /* slow_ahb=19.2M(XO), fast_ahb=300M(PLL0_MAIN/4->7), camnoc_axi=400M(PLL0_ODD/1->1),
     * cphy_rx=400M(PLL0_MAIN/3->5), csi1phytimer=400M(PLL0_ODD/1->1),
     * ife_lite=400M(PLL0_ODD/1->1), ife_lite_csid=400M(PLL0_ODD/1->1) */
    if (zizhan_camcc_rcg_cfg(o, 0x10034u, 0u, 1u) ||
        zizhan_camcc_rcg_cfg(o, 0x10018u, 1u, 7u) ||
        zizhan_camcc_rcg_cfg(o, 0x13194u, 3u, 1u) ||
        zizhan_camcc_rcg_cfg(o, 0x1104cu, 1u, 5u) ||
        zizhan_camcc_rcg_cfg(o, 0x15104u, 3u, 1u) ||
        zizhan_camcc_rcg_cfg(o, 0x13000u, 3u, 1u) ||
        zizhan_camcc_rcg_cfg(o, 0x13024u, 3u, 1u)) return ZizhanCamCcTimeout;
    if (stage_out) *stage_out = 3;
    o->write(o->context, 0x131f0u, o->read(o->context, 0x131f0u) | 1u);   /* core_ahb halt-delay */
    static const uint32_t branches[] = {
        0x13164u, /* cpas_ahb */
        0x1316cu, /* cpas_fast_ahb */
        0x131acu, /* camnoc_axi */
        0x13048u, /* ife_lite_ahb */
        0x13018u, /* ife_lite */
        0x13020u, /* cpas_ife_lite */
        0x1303cu, /* ife_lite_csid */
        0x13044u, /* ife_lite_cphy_rx */
        0x15100u, /* csid_csiphy_rx (for TPG1) */
        0x1511cu, /* csi1phytimer */
        0x15120u  /* csiphy1 */
    };
    for (unsigned i = 0; i < sizeof(branches) / sizeof(branches[0]); ++i) {
        if (zizhan_camcc_branch(o, branches[i], 1)) return ZizhanCamCcTimeout;
    }
    if (stage_out) *stage_out = 4;
    return ZizhanCamCcOk;
}

static inline void zizhan_camcc_isp_clocks_off(const ZizhanCamCcOps *o) {
    if (!o || !o->read || !o->write || !o->pause_us || !o->polls) return;
    static const uint32_t branches_rev[] = {
        0x15120u, 0x1511cu, 0x15100u, 0x13044u, 0x1303cu, 0x13020u,
        0x13018u, 0x13048u, 0x131acu, 0x1316cu, 0x13164u
    };
    for (unsigned i = 0; i < sizeof(branches_rev) / sizeof(branches_rev[0]); ++i) {
        (void)zizhan_camcc_branch(o, branches_rev[i], 0);
    }
    /* Switch RCGs back to 19.2 MHz BI_TCXO before disabling PLL0 */
    const uint32_t rcgs[] = {0x13024u, 0x13000u, 0x15104u, 0x1104cu, 0x13194u, 0x10018u};
    for (unsigned i = 0; i < sizeof(rcgs) / sizeof(rcgs[0]); ++i) {
        (void)zizhan_camcc_rcg_cfg(o, rcgs[i], 0u, 1u);
    }
    o->write(o->context, 0x00u, o->read(o->context, 0x00u) & ~1u);
    o->write(o->context, 0x18u, o->read(o->context, 0x18u) & ~0x7u);
    o->write(o->context, 0x04u, 0u);
}

/* CSIPHY 3PH v2.1.2/2.1.3 Gen2 2PH 4-lane D-PHY (regs->offset = 0x1000, camss-csiphy-3ph-1-0.c). */
static inline uint8_t zizhan_csiphy_settle_cnt(uint64_t link_freq_hz, uint32_t timer_clk_hz) {
    if (!link_freq_hz || !timer_clk_hz) return 0x10u;
    const uint64_t ui_ps = (1000000000000ull / link_freq_hz) / 2ull;
    const uint64_t t_settle_ps = 85000ull + 6ull * ui_ps;
    const uint64_t timer_ps = 1000000000000ull / (uint64_t)timer_clk_hz;
    const uint64_t raw = timer_ps ? (t_settle_ps / timer_ps) : 16ull;
    return (uint8_t)(raw > 6ull ? (raw - 6ull) : 1ull);
}

typedef struct { uint16_t off; uint8_t val; uint8_t delay_us; uint8_t is_settle; } ZizhanCsiphyReg;

static inline uint32_t zizhan_csiphy_dphy_enable(const ZizhanCamCcOps *phy, uint64_t link_freq_hz,
                                                 uint32_t timer_clk_hz, uint8_t num_lanes) {
    if (!phy || !phy->read || !phy->write || !phy->pause_us) return 0;
    const uint32_t cmn = 0x1000u;
    /* Read HW version via CTRL6 SHOW_REV_ID */
    phy->write(phy->context, cmn + 0x18u, 0x02u);
    const uint32_t hw_ver = (phy->read(phy->context, cmn + 0xb0u + 0x30u) & 0xffu) |
                            ((phy->read(phy->context, cmn + 0xb0u + 0x34u) & 0xffu) << 8) |
                            ((phy->read(phy->context, cmn + 0xb0u + 0x38u) & 0xffu) << 16) |
                            ((phy->read(phy->context, cmn + 0xb0u + 0x3cu) & 0xffu) << 24);
    /* Reset */
    phy->write(phy->context, cmn + 0x00u, 0x01u);
    phy->pause_us(phy->context, 5000);
    phy->write(phy->context, cmn + 0x00u, 0x00u);

    const uint8_t settle = zizhan_csiphy_settle_cnt(link_freq_hz, timer_clk_hz);
    uint32_t lane_en = 0x80u; /* CLK_ENABLE */
    const uint8_t n = (num_lanes >= 1u && num_lanes <= 4u) ? num_lanes : 4u;
    for (uint8_t i = 0; i < n; ++i) lane_en |= (1u << (i * 2u));
    phy->write(phy->context, cmn + 0x14u, lane_en); /* CTRL5 */
    phy->write(phy->context, cmn + 0x18u, 0x01u);   /* CTRL6: COMMON_PWRDN_B */
    phy->write(phy->context, cmn + 0x1cu, 0x02u);   /* CTRL7 */
    phy->write(phy->context, cmn + 0x00u, 0x00u);   /* CTRL0 */

    /* Gen2 2PH D-PHY clock lane (0x0E00) + 4 data lanes (0x0000, 0x0400, 0x0800, 0x0C00) */
    static const ZizhanCsiphyReg kClkRegs[] = {
        {0x0e90, 0x0f, 0, 0}, {0x0e98, 0x08, 0, 0}, {0x0e94, 0x07, 1, 0},
        {0x0e30, 0x00, 0, 0}, {0x0e28, 0x04, 0, 0}, {0x0e00, 0x80, 0, 0},
        {0x0e0c, 0xff, 0, 0}, {0x0e38, 0x1f, 0, 0}, {0x0e2c, 0x01, 0, 0},
        {0x0e34, 0x0f, 0, 0}, {0x0e1c, 0x0a, 0, 0}, {0x0e14, 0x60, 0, 0},
        {0x0e3c, 0xb8, 0, 0}, {0x0e04, 0x0c, 0, 0}, {0x0e20, 0x00, 0, 0},
        {0x0e08, 0x10, 0, 1}, {0x0e10, 0x52, 0, 0}
    };
    static const ZizhanCsiphyReg kDataTemplate[] = {
        {0x0094, 0x00, 0, 0}, {0x00a0, 0x00, 0, 0}, {0x0090, 0x0f, 0, 0},
        {0x0098, 0x08, 0, 0}, {0x0094, 0x07, 1, 0},
        {0x0030, 0x00, 0, 0}, {0x0000, 0x8e, 0, 0}, {0x0038, 0xfe, 0, 0},
        {0x002c, 0x01, 0, 0}, {0x0034, 0x0f, 0, 0}, {0x001c, 0x0a, 0, 0},
        {0x0014, 0x60, 0, 0}, {0x003c, 0xb8, 0, 0}, {0x0004, 0x0c, 0, 0},
        {0x0020, 0x00, 0, 0}, {0x0008, 0x10, 0, 1}, {0x0010, 0x52, 0, 0},
        {0x0094, 0xd7, 0, 0}, {0x005c, 0x04, 0, 0}, {0x0060, 0xbd, 0, 0},
        {0x0064, 0x7f, 0, 0}
    };
    for (unsigned i = 0; i < sizeof(kClkRegs) / sizeof(kClkRegs[0]); ++i) {
        phy->write(phy->context, kClkRegs[i].off, kClkRegs[i].is_settle ? settle : kClkRegs[i].val);
        if (kClkRegs[i].delay_us) phy->pause_us(phy->context, kClkRegs[i].delay_us);
    }
    for (unsigned lane = 0; lane < n; ++lane) {
        const uint32_t base = lane * 0x400u;
        for (unsigned i = 0; i < sizeof(kDataTemplate) / sizeof(kDataTemplate[0]); ++i) {
            phy->write(phy->context, base + kDataTemplate[i].off,
                       kDataTemplate[i].is_settle ? settle : kDataTemplate[i].val);
            if (kDataTemplate[i].delay_us) phy->pause_us(phy->context, kDataTemplate[i].delay_us);
        }
    }
    for (unsigned i = 11; i < 22; ++i) phy->write(phy->context, cmn + 4u * i, 0u);
    return hw_ver;
}

static inline void zizhan_csiphy_dphy_disable(const ZizhanCamCcOps *phy) {
    if (!phy || !phy->write) return;
    phy->write(phy->context, 0x1014u, 0u);
    phy->write(phy->context, 0x1018u, 0u);
}

/* CSIPHY 3PH v2.1.3 Gen2 3-trio C-PHY (extracted from Xiaomi MIX Fold 2 L18 camera.ko:
 * csiphy_3ph_v2_1_3_reg + csiphy_common_reg_2_1_3 + data_rate_delta_table_2_1_3_xiaomi_l18).
 * rate_profile: 0 = ~700 Msps/trio (bw<=4.79G), 1 = ~1400 Msps/trio (bw<=10.26G),
 *               2 = ~1000 Msps/trio (bw<=6.84G), 3 = ~333 Msps/trio (bw<=2.28G). */
static inline uint32_t zizhan_csiphy_cphy_enable(const ZizhanCamCcOps *phy, uint8_t settle, uint8_t rate_profile) {
    if (!phy || !phy->read || !phy->write || !phy->pause_us) return 0;
    const uint32_t cmn = 0x1000u;
    phy->write(phy->context, cmn + 0x18u, 0x02u);
    const uint32_t hw_ver = (phy->read(phy->context, cmn + 0xb0u + 0x30u) & 0xffu) |
                            ((phy->read(phy->context, cmn + 0xb0u + 0x34u) & 0xffu) << 8) |
                            ((phy->read(phy->context, cmn + 0xb0u + 0x38u) & 0xffu) << 16) |
                            ((phy->read(phy->context, cmn + 0xb0u + 0x3cu) & 0xffu) << 24);
    /* Common C-PHY 3-trio enable (csiphy_common_reg_2_1_3):
     * odd bits (1<<1)|(1<<3)|(1<<5) = 0x2a, CTRL6=0x01, CTRL7=0x7a, then hold SW_RESET (0x1000=0x01)
     * across delta-table and 3PH per-trio programming before csiphy_reset_exit_reg_2_1_3 (0x1000=0x0e). */
    phy->write(phy->context, cmn + 0x14u, 0x2au);
    phy->write(phy->context, cmn + 0x84u, 0x00u);
    phy->write(phy->context, cmn + 0x18u, 0x01u);
    phy->write(phy->context, cmn + 0x1cu, 0x7au);
    phy->write(phy->context, cmn + 0x14u, 0x2au);
    phy->write(phy->context, cmn + 0x00u, 0x01u);
    phy->pause_us(phy->context, 10);

    /* L18 data_rate_delta_table_2_1_3_xiaomi_l18 overrides for 0x270, 0x274, 0x278, 0x214 */
    uint8_t r270 = 0x00u, r274 = 0x03u, r278 = 0x20u, r214 = 0x00u; /* 0: ~700 Msps (<=4.79G) */
    if (rate_profile == 1u)      { r270 = 0x01u; r274 = 0x00u; r278 = 0x06u; r214 = 0x00u; } /* ~1500 Msps (<=10.26G) */
    else if (rate_profile == 2u) { r270 = 0x01u; r274 = 0x00u; r278 = 0x17u; r214 = 0x00u; } /* ~1000 Msps (<=6.84G) */
    else if (rate_profile == 3u) { r270 = 0x00u; r274 = 0x03u; r278 = 0x58u; r214 = 0x09u; } /* ~333 Msps  (<=2.28G) */
    else if (rate_profile == 4u) { r270 = 0x01u; r274 = 0x00u; r278 = 0x02u; r214 = 0x00u; } /* ~1667 Msps (<=11.40G) */
    else if (rate_profile == 5u) { r270 = 0x01u; r274 = 0x00u; r278 = 0x00u; r214 = 0x00u; } /* ~2000 Msps (<=13.68G) */

    for (unsigned trio = 0; trio < 3u; ++trio) {
        const uint32_t off = trio * 0x400u;
        phy->write(phy->context, off + 0x0270u, r270);
        phy->write(phy->context, off + 0x0274u, r274);
        phy->write(phy->context, off + 0x0278u, r278);
        phy->write(phy->context, off + 0x0288u, 0x20u);
        phy->write(phy->context, off + 0x028cu, 0x30u);
        phy->write(phy->context, off + 0x026cu, 0x3du);
        phy->write(phy->context, off + 0x0290u, 0x02u);
        phy->write(phy->context, off + 0x0214u, r214);
    }
    phy->write(phy->context, 0x108cu, 0x00u);
    phy->pause_us(phy->context, 10);

    /* Base per-trio sequence (csiphy_3ph_v2_1_3_reg: trio 0=0x0200, trio 1=0x0600, trio 2=0x0a00) */
    static const ZizhanCsiphyReg kTrioTemplate[] = {
        {0x0268, 0xf1, 100, 0}, {0x0294, 0x09,   0, 0}, {0x02f4, 0x00,   0, 0},
        {0x02f8, 0x00,   0, 0}, {0x02fc, 0x00,   0, 0}, {0x02f0, 0x00,   2, 0},
        {0x02f0, 0xef, 100, 0}, {0x0204, 0x00,   0, 0}, {0x020c, 0x10,   0, 1},
        {0x0208, 0x00,   0, 0}, {0x0210, 0x00,   0, 0}, {0x02e4, 0x00,   0, 0},
        {0x02e8, 0x7f,   0, 0}, {0x02ec, 0x7f,   0, 0}, {0x0218, 0x3e,   0, 0},
        {0x021c, 0x41,   0, 0}, {0x0220, 0x41,   0, 0}, {0x0224, 0x7f,   0, 0},
        {0x0228, 0x00,   0, 0}, {0x022c, 0x00,   0, 0}, {0x0264, 0x01,   0, 0},
        {0x0284, 0x00,   0, 0}, {0x0244, 0xb2,   0, 0}, {0x0310, 0x35,   0, 0},
        {0x02bc, 0xd0,   0, 0}, {0x0254, 0x00,   0, 0}, {0x0240, 0x00,   0, 0},
        {0x0260, 0xa8,   0, 0}
    };
    for (unsigned trio = 0; trio < 3u; ++trio) {
        const uint32_t off = trio * 0x400u;
        for (unsigned i = 0; i < sizeof(kTrioTemplate) / sizeof(kTrioTemplate[0]); ++i) {
            phy->write(phy->context, off + kTrioTemplate[i].off,
                       kTrioTemplate[i].is_settle ? settle : kTrioTemplate[i].val);
            if (kTrioTemplate[i].delay_us) phy->pause_us(phy->context, kTrioTemplate[i].delay_us);
        }
    }
    phy->pause_us(phy->context, 105);

    /* csiphy_reset_exit_reg_2_1_3 (0x8e080): release SW_RESET, enable 3PH CDR (0x0e) and power up 0x102c=0xff.
     * Note: on v2.1.3 IRQ regs start at 0x1058 (not v1.x 0x102c), so never zero 0x102c..0x1054! */
    phy->write(phy->context, cmn + 0x00u, 0x00u);
    phy->write(phy->context, cmn + 0x00u, 0x0eu);
    phy->pause_us(phy->context, 3100);
    phy->write(phy->context, cmn + 0x2cu, 0xffu);
    for (unsigned i = 0; i < 11; ++i) phy->write(phy->context, cmn + 0x58u + 4u * i, 0u);
    return hw_ver;
}

/* TPG1 (0xacf7000, phy_id=1) RAW10 color-bar / incrementing generator (camss-tpg-gen1.c). */
static inline uint32_t zizhan_tpg_start(const ZizhanCamCcOps *tpg, uint16_t width, uint16_t height, uint8_t lanes) {
    if (!tpg || !tpg->read || !tpg->write) return 0;
    const uint32_t ver = tpg->read(tpg->context, 0x00u);
    tpg->write(tpg->context, 0x64u, 0u);
    tpg->write(tpg->context, 0x1f4u, 1u);
    const uint8_t ln = (lanes >= 1u && lanes <= 4u) ? lanes : 4u;
    tpg->write(tpg->context, 0x68u, 0u);            /* VC0 CFG0: 1 DT, continuous frames */
    tpg->write(tpg->context, 0x6cu, 0x12345678u);   /* LFSR seed */
    const uint32_t hbi = ((uint32_t)width * 10u * 545u + (800u * ln - 1u)) / (800u * ln);
    const uint32_t vbi = ((uint32_t)height * 10u) / 100u;
    tpg->write(tpg->context, 0x70u, hbi);
    tpg->write(tpg->context, 0x74u, vbi);
    tpg->write(tpg->context, 0x78u, 0xau << 8);     /* COLOR_BARS_CFG */
    tpg->write(tpg->context, 0x7cu, ((uint32_t)width << 16) | (uint32_t)height);
    tpg->write(tpg->context, 0x80u, ZIZHAN_CAM_DT_RAW10);
    /* PAYLOAD_MODE = COLOR_BARS - 1 = 8, USER=0xBE, ENCODE_FORMAT=2 (RAW10) */
    const uint32_t cfg2 = (ver >= 0x20010000u)
        ? (8u | (0xbeu << 4) | (2u << 28))
        : (8u | (0xbeu << 4) | (2u << 20));
    tpg->write(tpg->context, 0x84u, cfg2);
    tpg->write(tpg->context, 0x64u, 1u | ((uint32_t)(ln - 1u) << 4));
    return ver;
}

static inline void zizhan_tpg_stop(const ZizhanCamCcOps *tpg) {
    if (!tpg || !tpg->write) return;
    tpg->write(tpg->context, 0x64u, 0u);
    tpg->write(tpg->context, 0x1f4u, 1u);
}

/* CSID-lite0 (0xacc6000, camss-csid-680.c) reset + RDI0 RAW10 stream configuration. */
static inline int zizhan_csid_lite_reset(const ZizhanCamCcOps *csid) {
    if (!csid || !csid->read || !csid->write || !csid->pause_us || !csid->polls) return -1;
    csid->write(csid->context, 0x84u, 0xffffffffu); /* TOP_IRQ_CLEAR */
    csid->write(csid->context, 0x94u, 0xffffffffu); /* BUF_DONE_IRQ_CLEAR */
    csid->write(csid->context, 0x14u, 1u);          /* IRQ_CMD_CLEAR */
    csid->write(csid->context, 0x80u, 0xffffffffu); /* TOP_IRQ_MASK */
    csid->write(csid->context, 0x90u, 0xffffffffu); /* BUF_DONE_IRQ_MASK */
    csid->write(csid->context, 0xf0u, 1u << 23);    /* RDI0 RUP_DONE mask */
    csid->write(csid->context, 0x0cu, 0x11u);       /* RESET_CFG: IMMEDIATE | LOCATION_COMPLETE */
    csid->write(csid->context, 0x10u, 0x07u);       /* RESET_CMD: HW | SW | IRQ */
    for (unsigned i = 0; i < csid->polls; ++i) {
        const uint32_t top = csid->read(csid->context, 0x7cu);
        if (top & 1u) {
            csid->write(csid->context, 0x84u, top);
            csid->write(csid->context, 0x14u, 1u);
            return 0;
        }
        csid->pause_us(csid->context, 10);
    }
    return -2;
}

static inline void zizhan_csid_lite_start_rdi0_ex(const ZizhanCamCcOps *csid, uint8_t csiphy_id,
                                                  uint8_t lanes, uint16_t lane_assign, int use_tpg, int is_cphy) {
    if (!csid || !csid->read || !csid->write) return;
    const uint8_t ln = (lanes >= 1u && lanes <= 4u) ? lanes : (is_cphy ? 3u : 4u);
    csid->write(csid->context, 0x544u, 0u);         /* FRM_DROP_PERIOD(0) */
    const uint32_t rdi_cfg0 = (1u << 6) | (1u << 8) |
                              (ZIZHAN_CAM_DEC_PAYLOAD << 12) |
                              (ZIZHAN_CAM_DT_RAW10 << 16);
    csid->write(csid->context, 0x500u, rdi_cfg0);
    /* RDI_CFG1: TIMESTAMP_STB_FRAME | BYTE_CNTR | TIMESTAMP | DROP_H/V | CROP_H/V | PIX_STORE | PACKING_MIPI */
    csid->write(csid->context, 0x510u, (1u << 0) | (1u << 2) | (1u << 4) | (0xfu << 5) | (1u << 10) | (1u << 15));
    csid->write(csid->context, 0x54cu, 0u);         /* IRQ_SUBSAMPLE_PERIOD */
    csid->write(csid->context, 0x548u, 1u);         /* IRQ_SUBSAMPLE_PATTERN */
    csid->write(csid->context, 0x504u, 0u);
    csid->write(csid->context, 0x500u, rdi_cfg0 | 0x80000000u);

    uint32_t rx0 = ((uint32_t)(ln - 1u)) | ((uint32_t)lane_assign << 4);
    if (use_tpg) {
        rx0 |= ((uint32_t)(csiphy_id + 1u) << 28) | (1u << 27);
    } else {
        rx0 |= ((uint32_t)(csiphy_id + 1u) << 20);
        if (is_cphy) rx0 |= (1u << 24);             /* CSI2_RX_CFG0_PHY_TYPE_SEL = 1 (C-PHY) */
    }
    csid->write(csid->context, 0x200u, rx0);        /* CSI2_RX_CFG0 */
    csid->write(csid->context, 0x204u, 1u);         /* ECC correction enable */
    /* RUP + AUP for RDI0, then resume RDI0 at frame boundary */
    csid->write(csid->context, 0x018u, (1u << 4) | (1u << 20));
    csid->write(csid->context, 0x504u, 1u);
}

static inline void zizhan_csid_lite_start_rdi0(const ZizhanCamCcOps *csid, uint8_t csiphy_id,
                                               uint8_t lanes, uint16_t lane_assign, int use_tpg) {
    zizhan_csid_lite_start_rdi0_ex(csid, csiphy_id, lanes, lane_assign, use_tpg, 0);
}

static inline void zizhan_csid_lite_stop_rdi0(const ZizhanCamCcOps *csid) {
    if (!csid || !csid->read || !csid->write) return;
    csid->write(csid->context, 0x504u, 0u);
    csid->write(csid->context, 0x500u, csid->read(csid->context, 0x500u) & ~0x80000000u);
}

/* VFE-lite0 (0xacc7000, camss-vfe-680.c / gen3) RDI0 write master (WM 0 at +0x400).
 * addr_shifted=0: 32-bit direct address (camss-vfe-680.c).
 * addr_shifted=1: 36-bit >>8 address & MODE_MIPI_RAW bit 16 (camss-vfe-gen3.c). */
static inline void zizhan_vfe_lite_wm0_start(const ZizhanCamCcOps *vfe, uint32_t dst_iova,
                                             uint16_t height, uint16_t stride, int addr_shifted) {
    if (!vfe || !vfe->write) return;
    const uint32_t frame_bytes = (uint32_t)stride * (uint32_t)height;
    vfe->write(vfe->context, 0x208u, 0x07ffffffu);  /* VFE_BUS_WM_CGC_OVERRIDE */
    vfe->write(vfe->context, 0x2dcu, 0u);           /* TEST_BUS_CTRL */
    vfe->write(vfe->context, 0x024u, 0u);           /* TOP_IRQ0_MASK */
    vfe->write(vfe->context, 0x028u, 0u);           /* TOP_IRQ1_MASK */
    vfe->write(vfe->context, 0x218u, 0xffffffffu);  /* BUS_IRQ0_MASK (polled status) */
    vfe->write(vfe->context, 0x220u, 0xffffffffu);  /* BUS_IRQ0_CLEAR */
    vfe->write(vfe->context, 0x230u, 1u);           /* BUS_IRQ_GLOBAL_CLEAR */

    if (addr_shifted) {
        vfe->write(vfe->context, 0x404u, dst_iova >> 8);
        vfe->write(vfe->context, 0x408u, frame_bytes >> 8);
        vfe->write(vfe->context, 0x40cu, 0x0000ffffu);
        vfe->write(vfe->context, 0x410u, 0u);
        vfe->write(vfe->context, 0x414u, 0x0000ffffu);
    } else {
        vfe->write(vfe->context, 0x404u, dst_iova);
        vfe->write(vfe->context, 0x408u, frame_bytes);
        vfe->write(vfe->context, 0x40cu, ((uint32_t)height << 16) | ((uint32_t)stride >> 4));
        vfe->write(vfe->context, 0x410u, 0u);
        vfe->write(vfe->context, 0x414u, stride);
    }
    vfe->write(vfe->context, 0x418u, 0u);           /* PACKER_CFG */
    vfe->write(vfe->context, 0x460u, 1u);           /* MMU_PREFETCH_CFG_EN */
    vfe->write(vfe->context, 0x464u, 0xffffffffu);  /* MMU_PREFETCH_MAX_OFFSET */
    vfe->write(vfe->context, 0x438u, 0u);           /* FRAMEDROP_PERIOD */
    vfe->write(vfe->context, 0x43cu, 1u);           /* FRAMEDROP_PATTERN */
    vfe->write(vfe->context, 0x430u, 0u);           /* IRQ_SUBSAMPLE_PERIOD */
    vfe->write(vfe->context, 0x434u, 1u);           /* IRQ_SUBSAMPLE_PATTERN */
    vfe->write(vfe->context, 0x400u, addr_shifted ? ((1u << 16) | 1u) : 1u);
}

static inline void zizhan_vfe_lite_wm0_stop(const ZizhanCamCcOps *vfe) {
    if (!vfe || !vfe->write) return;
    vfe->write(vfe->context, 0x400u, 0u);
}

/* Mainline drivers/media/i2c/ov13b10.c 19.2 MHz MCLK -> 1120 Mbps/lane 4-lane 1364x768 RAW10 (SGRBG10). */
static const ZizhanCciReg8 kZizhanOv13b10Init1120[] = {
    {0x0103, 0x01}, {0x0303, 0x04}, {0x0305, 0xaf}, {0x0321, 0x00}, {0x0323, 0x04},
    {0x0324, 0x01}, {0x0325, 0xa4}, {0x0326, 0x81}, {0x0327, 0x04}, {0x3012, 0x07},
    {0x3013, 0x32}, {0x3107, 0x23}, {0x3501, 0x0c}, {0x3502, 0x10}, {0x3504, 0x08},
    {0x3508, 0x07}, {0x3509, 0xc0}, {0x3600, 0x16}, {0x3601, 0x54}, {0x3612, 0x4e},
    {0x3620, 0x00}, {0x3621, 0x68}, {0x3622, 0x66}, {0x3623, 0x03}, {0x3662, 0x92},
    {0x3666, 0xbb}, {0x3667, 0x44}, {0x366e, 0xff}, {0x366f, 0xf3}, {0x3675, 0x44},
    {0x3676, 0x00}, {0x367f, 0xe9}, {0x3681, 0x32}, {0x3682, 0x1f}, {0x3683, 0x0b},
    {0x3684, 0x0b}, {0x3704, 0x0f}, {0x3706, 0x40}, {0x3708, 0x3b}, {0x3709, 0x72},
    {0x370b, 0xa2}, {0x3714, 0x24}, {0x371a, 0x3e}, {0x3725, 0x42}, {0x3739, 0x12},
    {0x3767, 0x00}, {0x377a, 0x0d}, {0x3789, 0x18}, {0x3790, 0x40}, {0x3791, 0xa2},
    {0x37c2, 0x04}, {0x37c3, 0xf1}, {0x37d9, 0x0c}, {0x37da, 0x02}, {0x37dc, 0x02},
    {0x37e1, 0x04}, {0x37e2, 0x0a}, {0x3800, 0x00}, {0x3801, 0x00}, {0x3802, 0x00},
    {0x3803, 0x08}, {0x3804, 0x10}, {0x3805, 0x8f}, {0x3806, 0x0c}, {0x3807, 0x47},
    {0x3808, 0x10}, {0x3809, 0x70}, {0x380a, 0x0c}, {0x380b, 0x30}, {0x380c, 0x04},
    {0x380d, 0x98}, {0x380e, 0x0c}, {0x380f, 0x7c}, {0x3811, 0x0f}, {0x3813, 0x09},
    {0x3814, 0x01}, {0x3815, 0x01}, {0x3816, 0x01}, {0x3817, 0x01}, {0x381f, 0x08},
    {0x3820, 0x88}, {0x3821, 0x00}, {0x3822, 0x14}, {0x382e, 0xe6}, {0x3c80, 0x00},
    {0x3c87, 0x01}, {0x3c8c, 0x19}, {0x3c8d, 0x1c}, {0x3ca0, 0x00}, {0x3ca1, 0x00},
    {0x3ca2, 0x00}, {0x3ca3, 0x00}, {0x3ca4, 0x50}, {0x3ca5, 0x11}, {0x3ca6, 0x01},
    {0x3ca7, 0x00}, {0x3ca8, 0x00}, {0x4008, 0x02}, {0x4009, 0x0f}, {0x400a, 0x01},
    {0x400b, 0x19}, {0x4011, 0x21}, {0x4017, 0x08}, {0x4019, 0x04}, {0x401a, 0x58},
    {0x4032, 0x1e}, {0x4050, 0x02}, {0x4051, 0x09}, {0x405e, 0x00}, {0x4066, 0x02},
    {0x4501, 0x00}, {0x4502, 0x10}, {0x4505, 0x00}, {0x4800, 0x64}, {0x481b, 0x3e},
    {0x481f, 0x30}, {0x4825, 0x34}, {0x4837, 0x0e}, {0x484b, 0x01}, {0x4883, 0x02},
    {0x5000, 0xff}, {0x5001, 0x0f}, {0x5045, 0x20}, {0x5046, 0x20}, {0x5047, 0xa4},
    {0x5048, 0x20}, {0x5049, 0xa4}
};

static const ZizhanCciReg8 kZizhanOv13b10Mode1364x768[] = {
    {0x0305, 0xaf}, {0x3011, 0x7c}, {0x3501, 0x03}, {0x3502, 0x00}, {0x3662, 0x88},
    {0x3714, 0x28}, {0x3739, 0x10}, {0x37c2, 0x14}, {0x37d9, 0x06}, {0x37e2, 0x0c},
    {0x37e4, 0x00}, {0x3800, 0x02}, {0x3801, 0xe4}, {0x3802, 0x03}, {0x3803, 0x48},
    {0x3804, 0x0d}, {0x3805, 0xab}, {0x3806, 0x09}, {0x3807, 0x60}, {0x3808, 0x05},
    {0x3809, 0x54}, {0x380a, 0x03}, {0x380b, 0x00}, {0x380c, 0x04}, {0x380d, 0x8e},
    {0x380e, 0x06}, {0x380f, 0x3e}, /* VTS=0x063e (~60fps -> ~15ms exposure budget for indoor light!) */
    {0x3501, 0x05}, {0x3502, 0x80}, /* coarse integration = 0x580 lines + 4x analog gain (0x3508=0x02, 0x3509=0x00) */
    {0x3508, 0x02}, {0x3509, 0x00},
    {0x3811, 0x07}, {0x3813, 0x07}, {0x3814, 0x03}, {0x3816, 0x03}, {0x3820, 0x8b},
    {0x3c8c, 0x18}, {0x4008, 0x00}, {0x4009, 0x05}, {0x4050, 0x00}, {0x4051, 0x05},
    {0x4501, 0x08}, {0x4505, 0x04}, {0x5000, 0xfd}, {0x5001, 0x0d}
};

/* Unpack 4 MIPI CSI-2 RAW10 pixels from 5 packed bytes. */
static inline void zizhan_raw10_unpack4(const uint8_t b[5], uint16_t p[4]) {
    p[0] = (uint16_t)(((uint32_t)b[0] << 2) | (((uint32_t)b[4] >> 0) & 3u));
    p[1] = (uint16_t)(((uint32_t)b[1] << 2) | (((uint32_t)b[4] >> 2) & 3u));
    p[2] = (uint16_t)(((uint32_t)b[2] << 2) | (((uint32_t)b[4] >> 4) & 3u));
    p[3] = (uint16_t)(((uint32_t)b[3] << 2) | (((uint32_t)b[4] >> 6) & 3u));
}

typedef struct {
    uint16_t min10;
    uint16_t max10;
    uint16_t mean10;
    uint16_t mean_gr;
    uint16_t mean_r;
    uint16_t mean_b;
    uint16_t mean_gb;
    uint32_t total_pixels;
    uint32_t nonzero_pixels;
    uint32_t saturated_pixels;
    uint32_t fnv1a32;
} ZizhanRaw10Stats;

static inline void zizhan_raw10_analyze(const uint8_t *buf, uint16_t width, uint16_t height,
                                        uint16_t stride, ZizhanRaw10Stats *out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!buf || !width || !height || stride < (uint32_t)(width * 10u / 8u)) return;
    uint16_t mn = 1023u, mx = 0u;
    uint64_t sum = 0, sum_c[4] = {0, 0, 0, 0};
    uint32_t cnt_c[4] = {0, 0, 0, 0}, nz = 0, sat = 0;
    uint32_t h = 2166136261u;
    const uint16_t groups = width / 4u;
    for (uint16_t y = 0; y < height; ++y) {
        const uint8_t *row = buf + (size_t)y * stride;
        const unsigned y_odd = y & 1u;
        for (uint16_t g = 0; g < groups; ++g) {
            const uint8_t *b = row + (size_t)g * 5u;
            for (unsigned k = 0; k < 5u; ++k) h = (h ^ b[k]) * 16777619u;
            uint16_t p[4];
            zizhan_raw10_unpack4(b, p);
            for (unsigned k = 0; k < 4u; ++k) {
                const uint16_t v = p[k];
                if (v < mn) mn = v;
                if (v > mx) mx = v;
                if (v > 0u) ++nz;
                if (v >= 1020u) ++sat;
                sum += v;
                /* SGRBG10: even row = GR, R; odd row = B, GB */
                const unsigned ch = (y_odd << 1) | (k & 1u);
                sum_c[ch] += v;
                ++cnt_c[ch];
            }
        }
    }
    const uint32_t total = (uint32_t)groups * 4u * (uint32_t)height;
    out->min10 = total ? mn : 0u;
    out->max10 = mx;
    out->mean10 = total ? (uint16_t)(sum / total) : 0u;
    out->mean_gr = cnt_c[0] ? (uint16_t)(sum_c[0] / cnt_c[0]) : 0u;
    out->mean_r  = cnt_c[1] ? (uint16_t)(sum_c[1] / cnt_c[1]) : 0u;
    out->mean_b  = cnt_c[2] ? (uint16_t)(sum_c[2] / cnt_c[2]) : 0u;
    out->mean_gb = cnt_c[3] ? (uint16_t)(sum_c[3] / cnt_c[3]) : 0u;
    out->total_pixels = total;
    out->nonzero_pixels = nz;
    out->saturated_pixels = sat;
    out->fnv1a32 = h;
}

/* Downsample SGRBG10 RAW10 frame into 8-bit RGB24 thumbnail (out_w x out_h, each cell averages 2x2 Bayer quad). */
static inline void zizhan_raw10_downsample_rgb(const uint8_t *buf, uint16_t width, uint16_t height,
                                               uint16_t stride, uint8_t *rgb_out,
                                               uint16_t out_w, uint16_t out_h, uint16_t black_level) {
    if (!buf || !rgb_out || !width || !height || !out_w || !out_h) return;
    /* Find auto-stretch peak above black level so indoor scenes render clearly in 8-bit preview */
    uint16_t peak = 0u;
    for (uint16_t y = 0; y < height; y += 4u) {
        const uint8_t *row = buf + (size_t)y * stride;
        for (uint16_t g = 0; g + 1u < width / 4u; g += 2u) {
            uint16_t p[4];
            zizhan_raw10_unpack4(row + (size_t)g * 5u, p);
            for (unsigned k = 0; k < 4u; ++k) if (p[k] > peak) peak = p[k];
        }
    }
    const uint32_t span = (peak > black_level + 8u) ? (uint32_t)(peak - black_level) : 16u;
    for (uint16_t oy = 0; oy < out_h; ++oy) {
        const uint16_t sy = (uint16_t)(((uint32_t)oy * (uint32_t)(height & ~1u)) / out_h) & ~1u;
        const uint8_t *r0 = buf + (size_t)sy * stride;
        const uint8_t *r1 = buf + (size_t)(sy + 1u) * stride;
        for (uint16_t ox = 0; ox < out_w; ++ox) {
            const uint16_t sx = (uint16_t)(((uint32_t)ox * (uint32_t)(width & ~3u)) / out_w) & ~3u;
            uint16_t p0[4], p1[4];
            zizhan_raw10_unpack4(r0 + (size_t)(sx / 4u) * 5u, p0);
            zizhan_raw10_unpack4(r1 + (size_t)(sx / 4u) * 5u, p1);
            /* SGRBG10: r0=[GR, R, GR, R], r1=[B, GB, B, GB] */
            const int32_t gr = ((int32_t)p0[0] + p0[2] + p1[1] + p1[3]) / 4 - (int32_t)black_level;
            const int32_t rr = ((int32_t)p0[1] + p0[3]) / 2 - (int32_t)black_level;
            const int32_t bb = ((int32_t)p1[0] + p1[2]) / 2 - (int32_t)black_level;
            const uint32_t r8 = rr <= 0 ? 0u : ((uint32_t)rr * 255u / span);
            const uint32_t g8 = gr <= 0 ? 0u : ((uint32_t)gr * 255u / span);
            const uint32_t b8 = bb <= 0 ? 0u : ((uint32_t)bb * 255u / span);
            uint8_t *dst = rgb_out + ((size_t)oy * out_w + ox) * 3u;
            dst[0] = (uint8_t)(r8 > 255u ? 255u : r8);
            dst[1] = (uint8_t)(g8 > 255u ? 255u : g8);
            dst[2] = (uint8_t)(b8 > 255u ? 255u : b8);
        }
    }
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_CAM_ISP_H */
