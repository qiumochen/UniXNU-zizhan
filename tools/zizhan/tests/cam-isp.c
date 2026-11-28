/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanCamIsp.h"
#include "../PongAdreno/ArmSmmu4K/ArmSmmu4K.h"

typedef struct {
    uint32_t regs[0x16000 / 4];
} FakeMmio;

static uint32_t fake_rd(void *ctx, uint32_t off) {
    FakeMmio *m = (FakeMmio *)ctx;
    if (off / 4 >= sizeof(m->regs) / sizeof(m->regs[0])) return 0;
    return m->regs[off / 4];
}

static void fake_wr(void *ctx, uint32_t off, uint32_t v) {
    FakeMmio *m = (FakeMmio *)ctx;
    if (off / 4 >= sizeof(m->regs) / sizeof(m->regs[0])) return;
    m->regs[off / 4] = v;
    /* Simulate hardware lock/ready bits */
    if (off == 0x131dcu) { /* GDSCR */
        if (!(v & 1u)) m->regs[off / 4] |= 0x80000000u;
        else m->regs[off / 4] &= ~0x80000000u;
    } else if (off == 0x0004u && v == 1u) { /* PLL0 RUN -> LOCK_DET */
        m->regs[0] |= 0x80000000u;
    } else if (off == 0x10034u || off == 0x10018u || off == 0x13194u ||
               off == 0x1104cu || off == 0x15104u || off == 0x13000u || off == 0x13024u) {
        m->regs[off / 4] &= ~1u; /* CMD_RCGR update complete */
    } else if (off == 0x13164u || off == 0x1316cu || off == 0x131acu || off == 0x13048u ||
               off == 0x13018u || off == 0x13020u || off == 0x1303cu || off == 0x13044u ||
               off == 0x15100u || off == 0x1511cu || off == 0x15120u) {
        if (v & 1u) m->regs[off / 4] &= ~0x80000000u;
        else m->regs[off / 4] |= 0x80000000u;
    } else if (off == 0x0010u && (v & 1u)) { /* CSID reset -> TOP_IRQ_STATUS bit 0 */
        m->regs[0x7cu / 4] |= 1u;
    }
}

static void fake_pause(void *ctx, unsigned us) { (void)ctx; (void)us; }

int main(void) {
    /* 1. CAM_CC PLL0 + ISP 400 MHz clock tree on/off */
    static FakeMmio camcc, csiphy, tpg, csid, vfe;
    memset(&camcc, 0, sizeof(camcc));
    ZizhanCamCcOps cc = {&camcc, fake_rd, fake_wr, fake_pause, 100};
    unsigned stage = 0;
    assert(zizhan_camcc_isp_clocks_on(&cc, &stage) == ZizhanCamCcOk && stage == 4u);
    assert(camcc.regs[0x10u / 4] == 0x4444003eu);
    assert((camcc.regs[0x00u / 4] & 1u) == 1u);
    assert((camcc.regs[0x15120u / 4] & 0x80000001u) == 1u);
    zizhan_camcc_isp_clocks_off(&cc);
    assert((camcc.regs[0x15120u / 4] & 0x80000000u) != 0u);
    assert((camcc.regs[0x00u / 4] & 1u) == 0u);

    /* 2. CSIPHY1 D-PHY settle count + 4-lane Gen2 programming */
    assert(zizhan_csiphy_settle_cnt(560000000ull, 400000000u) == 30u);
    csiphy.regs[(0x10b0u + 0x30u) / 4] = 0x03u;
    csiphy.regs[(0x10b0u + 0x34u) / 4] = 0x01u;
    csiphy.regs[(0x10b0u + 0x38u) / 4] = 0x02u;
    ZizhanCamCcOps phy_ops = {&csiphy, fake_rd, fake_wr, fake_pause, 100};
    assert(zizhan_csiphy_dphy_enable(&phy_ops, 560000000ull, 400000000u, 4u) == 0x00020103u);
    assert(csiphy.regs[0x1014u / 4] == 0xd5u);
    assert(csiphy.regs[0x0e08u / 4] == 30u);
    assert(csiphy.regs[0x0c08u / 4] == 30u);
    zizhan_csiphy_dphy_disable(&phy_ops);
    assert(csiphy.regs[0x1014u / 4] == 0u && csiphy.regs[0x1018u / 4] == 0u);

    /* 2b. CSIPHY0 3-trio C-PHY v2.1.3 L18 delta table programming (IMX766 1400 Msps profile 1) */
    assert(zizhan_csiphy_cphy_enable(&phy_ops, 0x18u, 1u) == 0x00020103u);
    assert(csiphy.regs[0x1000u / 4] == 0x0eu); /* csiphy_reset_exit_reg_2_1_3 3PH CDR enable */
    assert(csiphy.regs[0x1014u / 4] == 0x2au); /* 3 odd C-PHY trios: (1<<1)|(1<<3)|(1<<5) */
    assert(csiphy.regs[0x101cu / 4] == 0x7au);
    assert(csiphy.regs[0x102cu / 4] == 0xffu);
    assert(csiphy.regs[0x020cu / 4] == 0x18u && csiphy.regs[0x0a0cu / 4] == 0x18u);
    assert(csiphy.regs[0x0270u / 4] == 0x01u && csiphy.regs[0x0278u / 4] == 0x06u);

    /* 3. TPG1 + CSID-lite0 + VFE-lite0 (both direct 680 and shifted gen3 bus modes + C-PHY bit 24) */
    tpg.regs[0] = 0x20000000u;
    ZizhanCamCcOps tpg_ops = {&tpg, fake_rd, fake_wr, fake_pause, 100};
    assert(zizhan_tpg_start(&tpg_ops, 1364u, 768u, 4u) == 0x20000000u);
    assert((tpg.regs[0x64u / 4] & 1u) == 1u);
    zizhan_tpg_stop(&tpg_ops);
    assert(tpg.regs[0x64u / 4] == 0u);

    ZizhanCamCcOps csid_ops = {&csid, fake_rd, fake_wr, fake_pause, 100};
    assert(zizhan_csid_lite_reset(&csid_ops) == 0);
    zizhan_csid_lite_start_rdi0(&csid_ops, 1u, 4u, 0x3210u, 0);
    assert((csid.regs[0x500u / 4] & 0x80000000u) != 0u);
    assert(((csid.regs[0x200u / 4] >> 20) & 7u) == 2u); /* PHY_NUM_SEL = csiphy1 + 1 = 2 */
    assert((csid.regs[0x200u / 4] & (1u << 24)) == 0u);
    zizhan_csid_lite_start_rdi0_ex(&csid_ops, 0u, 3u, 0x0210u, 0, 1);
    assert(((csid.regs[0x200u / 4] >> 20) & 7u) == 1u); /* PHY_NUM_SEL = csiphy0 + 1 = 1 */
    assert((csid.regs[0x200u / 4] & (1u << 24)) != 0u); /* CSI2_RX_CFG0_PHY_TYPE_SEL = C-PHY */
    zizhan_csid_lite_stop_rdi0(&csid_ops);
    assert((csid.regs[0x500u / 4] & 0x80000000u) == 0u);

    ZizhanCamCcOps vfe_ops = {&vfe, fake_rd, fake_wr, fake_pause, 100};
    zizhan_vfe_lite_wm0_start(&vfe_ops, 0x9f500000u, 768u, 1712u, 0);
    assert(vfe.regs[0x404u / 4] == 0x9f500000u && vfe.regs[0x400u / 4] == 1u);
    zizhan_vfe_lite_wm0_start(&vfe_ops, 0x9f500000u, 768u, 1712u, 1);
    assert(vfe.regs[0x404u / 4] == 0x009f5000u && vfe.regs[0x400u / 4] == 0x10001u);
    zizhan_vfe_lite_wm0_stop(&vfe_ops);
    assert(vfe.regs[0x400u / 4] == 0u);

    /* 4. MIPI RAW10 unpack + Bayer statistics + RGB24 downsampler */
    static uint8_t frame[160 * 32];
    memset(frame, 0, sizeof(frame));
    for (unsigned y = 0; y < 32u; ++y) {
        uint8_t *row = frame + y * 160u;
        for (unsigned g = 0; g < 32u; ++g) {
            uint16_t p0 = (y & 1u) ? 200u : 400u;
            uint16_t p1 = (y & 1u) ? 420u : 800u;
            row[g * 5u + 0] = (uint8_t)(p0 >> 2);
            row[g * 5u + 1] = (uint8_t)(p1 >> 2);
            row[g * 5u + 2] = (uint8_t)(p0 >> 2);
            row[g * 5u + 3] = (uint8_t)(p1 >> 2);
            row[g * 5u + 4] = (uint8_t)(((p0 & 3u) << 0) | ((p1 & 3u) << 2) | ((p0 & 3u) << 4) | ((p1 & 3u) << 6));
        }
    }
    ZizhanRaw10Stats st;
    zizhan_raw10_analyze(frame, 128u, 32u, 160u, &st);
    assert(st.total_pixels == 4096u && st.nonzero_pixels == 4096u);
    assert(st.min10 == 200u && st.max10 == 800u);
    assert(st.mean_gr == 400u && st.mean_r == 800u && st.mean_b == 200u && st.mean_gb == 420u);

    uint8_t rgb[16 * 8 * 3];
    zizhan_raw10_downsample_rgb(frame, 128u, 32u, 160u, rgb, 16u, 8u, 64u);
    assert(rgb[0] == 255u && rgb[1] > 100u && rgb[2] < 80u); /* Red > Green > Blue */

    /* Low-light desk-resting auto-stretch (53..94 above black pedestal) */
    for (unsigned y = 0; y < 32u; ++y) {
        uint8_t *row = frame + y * 160u;
        for (unsigned g = 0; g < 32u; ++g) {
            uint16_t p0 = (y & 1u) ? 60u : 74u;
            uint16_t p1 = (y & 1u) ? 74u : 94u;
            row[g * 5u + 0] = (uint8_t)(p0 >> 2);
            row[g * 5u + 1] = (uint8_t)(p1 >> 2);
            row[g * 5u + 2] = (uint8_t)(p0 >> 2);
            row[g * 5u + 3] = (uint8_t)(p1 >> 2);
            row[g * 5u + 4] = (uint8_t)(((p0 & 3u) << 0) | ((p1 & 3u) << 2) | ((p0 & 3u) << 4) | ((p1 & 3u) << 6));
        }
    }
    zizhan_raw10_downsample_rgb(frame, 128u, 32u, 160u, rgb, 16u, 8u, 53u);
    assert(rgb[0] == 255u && rgb[1] > 100u && rgb[2] < 80u);

    /* 5. APPS SMMU 4K Stage-1 table build for 8 MiB camera_region (0x9f500000..0x9fd00000, 2048 leaves, 7 table pages) */
    static uint64_t smmu_tables[16 * 512];
    memset(smmu_tables, 0, sizeof(smmu_tables));
    ArmSmmu4KConfig lower = {32u, 36u, 0x88cc83000ull, (0x800000u / 4096u) + 16u};
    ArmSmmu4KRange range = {0x9f500000ull, 0x9f500000ull, 0x800000ull, ArmSmmu4KRead | ArmSmmu4KWrite, 0u, 2u};
    ArmSmmu4KResult built;
    assert(ArmSmmu4KBuild(&lower, &range, 1u, smmu_tables, 16u, &built) == ArmSmmu4KSuccess);
    assert(built.leaf_pages == 2048u && built.table_pages == 7u);

    puts("PASS cam-isp");
    return 0;
}
