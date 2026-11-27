/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Tier 2.4 (PERIPH-01): Pure-C PM8350B qcom,hv-haptics@f000 SPMI v7 controller core.
 * Hardware topology from Xiaomi MIX Fold 2 (zizhan) live DT:
 *   - SPMI SID 3: HAPTICS_CFG @ 0xf000 (PPID 0x3f0), HAPTICS_PTN @ 0xf100 (PPID 0x3f1), HAPTICS_HBOOST @ 0xf200 (PPID 0x3f2)
 *   - qcom,vmax-mv = <0xcb2> (3250 mV -> VMAX_CFG = 65 = 0x41)
 *   - qcom,lra-period-us = <0x13ee> (5102 us -> ~196 Hz X-axis LRA -> TLRA = 1020 = 0x03fc)
 *   - qcom,brake-pattern = [ff 3f 1f], qcom,drv-sig-shape = 1 (sine), qcom,brake-sig-shape = 1 (sine)
 *
 * Unattended safety invariant:
 *   When dry != 0 || user_present != 1, ZERO SPMI write transactions are issued (observer read-only).
 *   When attended (dry == 0 && user_present == 1), plays a finite 8-sample hardware PATTERN1 click
 *   (~12-25 ms) followed by brake [0xff, 0x3f, 0x1f] and explicit EN_CTL = 0 teardown.
 */
#ifndef ZIZHAN_HAPTICS_CORE_H
#define ZIZHAN_HAPTICS_CORE_H

#include <stdint.h>
#include "../ArmDTPlatform/BootResetCore.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZHAP_PPID_CFG  = 0x3f0u,
    ZHAP_PPID_PTN  = 0x3f1u,
    ZHAP_PPID_HBST = 0x3f2u,

    ZHAP_OK_DRY      = 0,
    ZHAP_OK_PLAYED   = 1,
    ZHAP_ERR_ARGS    = -1,
    ZHAP_ERR_ARBITER = -2,
    ZHAP_ERR_NO_APID = -3,
    ZHAP_ERR_OWNER   = -4,
    ZHAP_ERR_READ    = -5,
    ZHAP_ERR_WRITE   = -6,
    ZHAP_ERR_FAULT   = -7
};

enum {
    ZHAP_PAT_LIGHT     = 1,
    ZHAP_PAT_MEDIUM    = 2,
    ZHAP_PAT_HEAVY     = 3,
    ZHAP_PAT_SELECTION = 4,
    ZHAP_PAT_SUCCESS   = 5,
    ZHAP_PAT_WARNING   = 6,
    ZHAP_PAT_ERROR     = 7
};

typedef struct {
    int status;
    int apid_cfg, owner_cfg;
    int apid_ptn, owner_ptn;
    int apid_hbst, owner_hbst;
    uint8_t cfg_rev2;
    uint8_t ptn_rev2;
    uint8_t status_msb;
    uint8_t fault_sts;
    uint8_t en_ctl_before;
    uint8_t vmax_before;
    uint32_t reads_done;
    uint32_t writes_attempted;
} ZizhanHapticsReceipt;

static inline int zizhan_spmi_find_ppid(const struct br_arbiter *a, uint16_t ppid, int *apid_out, int *owner_out) {
    if (!a || !a->core || !a->cnfg || !apid_out || !owner_out) return ZHAP_ERR_ARGS;
    const uint32_t ver = a->core[0];
    const uint32_t apids = a->core[BR_FEATURES / 4] & BR_FEATURES_APIDS_MASK;
    *apid_out = -1;
    *owner_out = -1;
    if (ver < BR_VERSION_V7_MIN || ver >= BR_VERSION_V8_MIN || !apids || apids > 1024u) {
        return ZHAP_ERR_ARBITER;
    }
    for (uint32_t i = 0; i < apids; ++i) {
        const uint32_t map = a->core[(BR_APID_MAP + 4u * i) / 4u];
        if (!map || ((map >> 8) & 0xfffu) != ppid) continue;
        const int owner = (int)(a->cnfg[(BR_CNFG_OWNER + 4u * i) / 4u] & 7u);
        if (*apid_out < 0 || owner == a->ee) {
            *apid_out = (int)i;
            *owner_out = owner;
        }
    }
    return (*apid_out < 0) ? ZHAP_ERR_NO_APID : 0;
}

static inline uint16_t zizhan_haptics_amp_for_pattern(int pattern, unsigned *cycles_out) {
    unsigned cyc = 3u;
    uint16_t amp = 0x100u;
    switch (pattern) {
    case ZHAP_PAT_SELECTION: amp = 0x068u; cyc = 2u; break;
    case ZHAP_PAT_LIGHT:     amp = 0x098u; cyc = 2u; break;
    case ZHAP_PAT_MEDIUM:    amp = 0x118u; cyc = 3u; break;
    case ZHAP_PAT_HEAVY:     amp = 0x190u; cyc = 4u; break;
    case ZHAP_PAT_SUCCESS:   amp = 0x130u; cyc = 4u; break;
    case ZHAP_PAT_WARNING:   amp = 0x150u; cyc = 5u; break;
    case ZHAP_PAT_ERROR:     amp = 0x180u; cyc = 6u; break;
    default:                 amp = 0x0d0u; cyc = 3u; break;
    }
    if (cycles_out) *cycles_out = cyc;
    return amp;
}

static inline int zizhan_haptics_trigger_core(
    const struct br_arbiter *a, volatile uint32_t *obs, volatile uint32_t *chn,
    int pattern, int dry, int user_present,
    void (*pause_us)(void *, unsigned), void *pause_ctx,
    ZizhanHapticsReceipt *out)
{
    if (!out) return ZHAP_ERR_ARGS;
    for (unsigned i = 0; i < sizeof(*out); ++i) ((uint8_t *)out)[i] = 0;
    out->apid_cfg = out->apid_ptn = out->apid_hbst = -1;
    out->owner_cfg = out->owner_ptn = out->owner_hbst = -1;
    if (!a || !obs) { out->status = ZHAP_ERR_ARGS; return out->status; }

    int rc = zizhan_spmi_find_ppid(a, ZHAP_PPID_CFG, &out->apid_cfg, &out->owner_cfg);
    if (rc) { out->status = rc; return rc; }
    rc = zizhan_spmi_find_ppid(a, ZHAP_PPID_PTN, &out->apid_ptn, &out->owner_ptn);
    if (rc) { out->status = rc; return rc; }
    (void)zizhan_spmi_find_ppid(a, ZHAP_PPID_HBST, &out->apid_hbst, &out->owner_hbst);

    /* Observer-channel readback (100% read-only, works even in dry mode) */
    if (br_read8(obs, a, out->apid_cfg, 0x01u, &out->cfg_rev2) ||
        br_read8(obs, a, out->apid_ptn, 0x01u, &out->ptn_rev2) ||
        br_read8(obs, a, out->apid_cfg, 0x09u, &out->status_msb) ||
        br_read8(obs, a, out->apid_cfg, 0x0cu, &out->fault_sts) ||
        br_read8(obs, a, out->apid_cfg, 0x46u, &out->en_ctl_before) ||
        br_read8(obs, a, out->apid_cfg, 0x48u, &out->vmax_before)) {
        out->status = ZHAP_ERR_READ;
        return out->status;
    }
    out->reads_done = 6u;

    /* Unattended guard: ZERO SPMI writes when dry != 0 or user_present != 1 */
    if (dry != 0 || user_present != 1) {
        out->status = ZHAP_OK_DRY;
        return ZHAP_OK_DRY;
    }

    if (!chn || out->owner_cfg != (int)a->ee || out->owner_ptn != (int)a->ee) {
        out->status = ZHAP_ERR_OWNER;
        return out->status;
    }
    /* Refuse if active hardware short/OC/OT fault is latched */
    if (out->fault_sts & 0x07u) {
        ++out->writes_attempted;
        (void)br_write8(chn, out->apid_cfg, 0x66u, 0x07u);
        if (br_read8(obs, a, out->apid_cfg, 0x0cu, &out->fault_sts) || (out->fault_sts & 0x07u)) {
            out->status = ZHAP_ERR_FAULT;
            return out->status;
        }
        ++out->reads_done;
    }

    unsigned cycles = 3u;
    const uint16_t amp = zizhan_haptics_amp_for_pattern(pattern, &cycles);
    /* T_LRA = 5102 us / 5 us = 1020 = 0x03fc; VMAX = 3250 mV / 50 mV = 65 = 0x41 */
    const uint8_t cfg_seq[][2] = {
        {0x48u, 0x41u}, /* VMAX_CFG = 3250 mV */
        {0x49u, 0x01u}, /* DRV_WF_SEL = sine, WF_OVD = 0 */
        {0x4au, 0x00u}, /* AUTORES_CFG = open-loop crisp transient */
        {0x5cu, 0x03u}, /* TLRA_OL_HIGH = 0x03 */
        {0x5du, 0xfcu}, /* TLRA_OL_LOW  = 0xfc (1020 * 5 us = 5100 us = 196 Hz) */
    };
    for (unsigned i = 0; i < sizeof(cfg_seq) / sizeof(cfg_seq[0]); ++i) {
        ++out->writes_attempted;
        if (br_write8(chn, out->apid_cfg, cfg_seq[i][0], cfg_seq[i][1])) {
            out->status = ZHAP_ERR_WRITE;
            return out->status;
        }
    }

    /* Program PATTERN1 (8 samples @ 0x3f1 + 0x34, 3 bytes/sample) + brake [ff 3f 1f] @ 0x70 */
    ++out->writes_attempted;
    if (br_write8(chn, out->apid_ptn, 0x30u, 0x03u)) { out->status = ZHAP_ERR_WRITE; return out->status; }
    ++out->writes_attempted;
    if (br_write8(chn, out->apid_ptn, 0x31u, 0xfcu)) { out->status = ZHAP_ERR_WRITE; return out->status; }

    for (unsigned s = 0; s < 8u; ++s) {
        const uint16_t sa = (s < cycles) ? amp : 0u;
        const uint8_t b0 = (uint8_t)((sa >> 8) & 0x01u); /* F_SEL = 0 (1x T_LRA), sign = 0, amp[8] */
        const uint8_t b1 = (uint8_t)(sa & 0xffu);
        const uint8_t base = (uint8_t)(0x34u + s * 3u);
        out->writes_attempted += 3u;
        if (br_write8(chn, out->apid_ptn, base + 0u, b0) ||
            br_write8(chn, out->apid_ptn, base + 1u, b1) ||
            br_write8(chn, out->apid_ptn, base + 2u, 0x00u)) {
            out->status = ZHAP_ERR_WRITE;
            return out->status;
        }
    }
    static const uint8_t kBrake[4] = {0xffu, 0x3fu, 0x1fu, 0x00u};
    for (unsigned i = 0; i < 4u; ++i) {
        ++out->writes_attempted;
        if (br_write8(chn, out->apid_ptn, (uint8_t)(0x70u + i), kBrake[i])) {
            out->status = ZHAP_ERR_WRITE;
            return out->status;
        }
    }
    ++out->writes_attempted;
    (void)br_write8(chn, out->apid_ptn, 0x64u, 0x00u); /* PTRN_LOOP_CFG = single-shot (0 loops) */

    /* Enable module + fire single-shot PATTERN1 + active brake, wait 26 ms, then disarm cleanly */
    ++out->writes_attempted;
    if (br_write8(chn, out->apid_cfg, 0x46u, 0x80u)) { out->status = ZHAP_ERR_WRITE; return out->status; }
    ++out->writes_attempted;
    if (br_write8(chn, out->apid_cfg, 0x4cu, 0x8au)) { /* PLAY_EN | BRAKE_EN | SRC=PATTERN1(2) */
        ++out->writes_attempted;
        (void)br_write8(chn, out->apid_cfg, 0x46u, 0x00u);
        out->status = ZHAP_ERR_WRITE;
        return out->status;
    }
    if (pause_us) pause_us(pause_ctx, 26000u);
    out->writes_attempted += 2u;
    (void)br_write8(chn, out->apid_cfg, 0x4cu, 0x00u);
    (void)br_write8(chn, out->apid_cfg, 0x46u, 0x00u);
    out->status = ZHAP_OK_PLAYED;
    return ZHAP_OK_PLAYED;
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_HAPTICS_CORE_H */
