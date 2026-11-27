/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../../../drivers/PongPower/ZizhanHapticsCore.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void noop_pause(void *ctx, unsigned us) { (void)ctx; (void)us; }

int main(void) {
    static uint32_t core[0x3000 / 4];
    static uint32_t cnfg[0x4000 / 4];
    static uint32_t obs[0x10000 / 4];
    static uint32_t chn[0x20000 / 4];
    memset(core, 0, sizeof(core));
    memset(cnfg, 0, sizeof(cnfg));
    memset(obs, 0, sizeof(obs));
    memset(chn, 0, sizeof(chn));

    /* SPMI v7 arbiter with 8 APIDs: APID 3 = 0x3f0 (CFG), APID 4 = 0x3f1 (PTN), APID 5 = 0x3f2 (HBOOST) */
    core[0] = 0x70010000u;
    core[BR_FEATURES / 4] = 8u;
    core[(BR_APID_MAP + 4 * 3) / 4] = (ZHAP_PPID_CFG << 8);
    core[(BR_APID_MAP + 4 * 4) / 4] = (ZHAP_PPID_PTN << 8);
    core[(BR_APID_MAP + 4 * 5) / 4] = (ZHAP_PPID_HBST << 8);
    cnfg[3] = 0u; /* EE 0 owns CFG */
    cnfg[4] = 0u; /* EE 0 owns PTN */
    cnfg[5] = 0u;

    /* Pre-set DONE bit on observer & write channels for APIDs 3 and 4 */
    obs[(BR_OBS_APID_STRIDE * 3 + BR_STATUS) / 4] = BR_STATUS_DONE;
    obs[(BR_OBS_APID_STRIDE * 3 + BR_RDATA0) / 4] = 0x02u;
    obs[(BR_OBS_APID_STRIDE * 4 + BR_STATUS) / 4] = BR_STATUS_DONE;
    obs[(BR_OBS_APID_STRIDE * 4 + BR_RDATA0) / 4] = 0x01u;
    chn[(BR_CHNL_APID_STRIDE * 3 + BR_STATUS) / 4] = BR_STATUS_DONE;
    chn[(BR_CHNL_APID_STRIDE * 4 + BR_STATUS) / 4] = BR_STATUS_DONE;

    struct br_arbiter arb = {core, cnfg, 0u};
    ZizhanHapticsReceipt rec;

    /* 1. Unattended dry=1 MUST issue ZERO SPMI writes even if user_present=1 */
    assert(zizhan_haptics_trigger_core(&arb, obs, chn, ZHAP_PAT_MEDIUM, 1, 1, noop_pause, NULL, &rec) == ZHAP_OK_DRY);
    assert(rec.apid_cfg == 3 && rec.apid_ptn == 4 && rec.apid_hbst == 5);
    assert(rec.reads_done == 6u && rec.writes_attempted == 0u);
    assert(chn[(BR_CHNL_APID_STRIDE * 3 + BR_CMD) / 4] == 0u);

    /* 2. user_present=0 MUST also issue ZERO SPMI writes even if dry=0 */
    assert(zizhan_haptics_trigger_core(&arb, obs, chn, ZHAP_PAT_HEAVY, 0, 0, noop_pause, NULL, &rec) == ZHAP_OK_DRY);
    assert(rec.writes_attempted == 0u);

    /* 3. Non-owned APID (owner != EE 0) MUST refuse writes cleanly */
    cnfg[3] = 2u;
    assert(zizhan_haptics_trigger_core(&arb, obs, chn, ZHAP_PAT_LIGHT, 0, 1, noop_pause, NULL, &rec) == ZHAP_ERR_OWNER);
    assert(rec.writes_attempted == 0u);
    cnfg[3] = 0u;

    /* 4. Latched hardware fault (RDATA0 = 0x02 -> fault_sts & 0x07 != 0) MUST abort with ZHAP_ERR_FAULT */
    assert(zizhan_haptics_trigger_core(&arb, obs, chn, ZHAP_PAT_MEDIUM, 0, 1, noop_pause, NULL, &rec) == ZHAP_ERR_FAULT);

    /* 5. Attended dry=0 && user_present=1 (fault_sts == 0) programs finite PATTERN1 + brake and disarms EN_CTL=0 */
    obs[(BR_OBS_APID_STRIDE * 3 + BR_RDATA0) / 4] = 0x00u;
    assert(zizhan_haptics_trigger_core(&arb, obs, chn, ZHAP_PAT_MEDIUM, 0, 1, noop_pause, NULL, &rec) == ZHAP_OK_PLAYED);
    assert(rec.writes_attempted > 30u);
    /* Final write to CFG channel must be EN_CTL (0x46) = 0x00 */
    assert(chn[(BR_CHNL_APID_STRIDE * 3 + BR_WDATA0) / 4] == 0x00u);
    assert(chn[(BR_CHNL_APID_STRIDE * 3 + BR_CMD) / 4] == br_cmd(BR_OP_EXT_WRITEL, 0x46u, 0));

    puts("PASS zizhan-haptics");
    return 0;
}
