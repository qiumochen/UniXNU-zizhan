/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-C QMI_LOC (service 16, v2) receive-only GNSS state machine and TLV parser.
 * Zero floating-point types or OS dependencies: IEEE-754 binary32/binary64 fields
 * are decoded with bounded integer bit arithmetic safe for arm64 XNU kernel threads.
 * Cellular RF is never enabled here; only passive GNSS receiver messages are built. */
#ifndef ZIZHAN_QMI_LOC_H
#define ZIZHAN_QMI_LOC_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    ZIZHAN_QMI_LOC_SERVICE               = 16u,
    ZIZHAN_QMI_LOC_GET_SUPPORTED_MSGS    = 0x001Eu,
    ZIZHAN_QMI_LOC_INFORM_CLIENT_REV     = 0x0020u,
    ZIZHAN_QMI_LOC_REG_EVENTS            = 0x0021u,
    ZIZHAN_QMI_LOC_START                 = 0x0022u,
    ZIZHAN_QMI_LOC_STOP                  = 0x0023u,
    ZIZHAN_QMI_LOC_POS_REPORT_IND        = 0x0024u,
    ZIZHAN_QMI_LOC_SV_INFO_IND           = 0x0025u,
    ZIZHAN_QMI_LOC_GET_SERVICE_REV       = 0x0026u,
    ZIZHAN_QMI_LOC_ENGINE_STATE_IND      = 0x002Bu,
    ZIZHAN_QMI_LOC_FIX_SESSION_STATE_IND = 0x002Cu,
    ZIZHAN_QMI_LOC_SET_ENGINE_LOCK       = 0x003Eu,
    ZIZHAN_QMI_LOC_SET_OP_MODE           = 0x004Au,
    ZIZHAN_QMI_LOC_REG_MASTER_CLIENT     = 0x00BAu,
    ZIZHAN_QMI_LOC_SET_NFW_ACCESS        = 0x00DCu,

    /* Event registration mask (qmi-flags64-loc.h):
     * bit 0 POSITION_REPORT | bit 1 GNSS_SATELLITE_INFO | bit 7 ENGINE_STATE | bit 8 FIX_SESSION_STATE */
    ZIZHAN_QMI_LOC_EVENT_MASK_DEFAULT    = 0x0183u,
    ZIZHAN_QMI_LOC_LOCK_NONE             = 1u,
    ZIZHAN_QMI_LOC_OP_MODE_STANDALONE    = 4u,
    ZIZHAN_QMI_LOC_RECURRENCE_PERIODIC   = 1u,
    ZIZHAN_QMI_LOC_INTERMEDIATE_ENABLE   = 1u,
    ZIZHAN_QMI_LOC_IND_PHONE_OFFLINE     = 5u,
    ZIZHAN_QMI_LOC_SESSION_PHONE_OFFLINE = 6u,
    ZIZHAN_QMI_LOC_SESSION_ENGINE_LOCKED = 7u
};

enum {
    ZIZHAN_QMI_LOC_STAGE_IDLE       = 0,
    ZIZHAN_QMI_LOC_STAGE_WAIT_REV   = 1,
    ZIZHAN_QMI_LOC_STAGE_WAIT_REG   = 2,
    ZIZHAN_QMI_LOC_STAGE_WAIT_MODE  = 3,
    ZIZHAN_QMI_LOC_STAGE_WAIT_START = 4,
    ZIZHAN_QMI_LOC_STAGE_RUNNING    = 5
};

typedef struct {
    uint8_t stage;
    uint8_t session_id;
    uint16_t next_txn;
    uint64_t stage_since_ms;

    uint8_t rev_done;
    uint8_t reg_ok;
    uint8_t mode_ok;
    uint8_t start_ok;
    uint8_t reg_retries;
    uint8_t start_retries;
    uint8_t phone_offline;
    uint8_t engine_state;      /* 0 unknown, 1 ON, 2 OFF */
    uint8_t fix_session_state; /* 0 unknown, 1 STARTED, 2 STOPPED */
    uint8_t fix_status;        /* 0 none, 1 intermediate, 2 valid final fix */
    uint8_t sv_visible;
    uint8_t sv_searching;
    uint8_t sv_tracking;
    uint8_t sv_used;
    uint16_t max_cn0_tenths;   /* 0.1 dB-Hz, e.g. 385 = 38.5 dB-Hz */
    uint32_t constellation_mask;
    uint32_t session_status;
    uint32_t last_ind_status;
    uint16_t last_resp_msg;
    uint16_t last_ind_msg;
    uint16_t last_resp_result;
    uint16_t last_resp_error;

    /* Decoded geodetic fix (kept in RAM only; callers MUST NOT write lat_e7/lon_e7 to logs). */
    int32_t lat_e7;            /* degrees * 1e7, [-900000000 .. +900000000] */
    int32_t lon_e7;            /* degrees * 1e7, [-1800000000 .. +1800000000] */
    int32_t hor_unc_dm;        /* horizontal circular uncertainty in 0.1 m */
    int32_t alt_dm;            /* altitude above sea level (or ellipsoid) in 0.1 m */
    int32_t speed_cms;         /* horizontal speed in cm/s */
    int32_t heading_ddeg;      /* heading in 0.1 deg [0..3600] */
    uint32_t tech_mask;

    uint32_t pos_ind_count;
    uint32_t sv_ind_count;
    uint32_t valid_fix_count;
} ZizhanQmiLoc;

static inline void zizhan_qmi_loc_init(ZizhanQmiLoc *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
    s->session_id = 0u;
    s->next_txn = 1u;
}

/* Soft-float IEEE-754 binary32 -> signed round(val * scale), clamped to [-2e9, +2e9]. */
static inline int32_t zizhan_qmi_loc_f32_scaled(uint32_t bits, uint32_t scale, int *valid_out) {
    const uint32_t sign = bits >> 31;
    const uint32_t exp_raw = (bits >> 23) & 0xffu;
    const uint32_t frac = bits & 0x7fffffu;
    if (exp_raw == 0xffu) {
        if (valid_out) *valid_out = 0;
        return 0;
    }
    if (valid_out) *valid_out = 1;
    if (exp_raw == 0u || scale == 0u) return 0;
    const int32_t e = (int32_t)exp_raw - 127;
    if (e < -20) return 0;
    if (e > 24) {
        if (valid_out) *valid_out = 0;
        return sign ? -2000000000 : 2000000000;
    }
    const uint64_t mant = (uint64_t)frac | 0x800000ull;
    const uint64_t prod = mant * (uint64_t)scale;
    uint64_t v = 0;
    if (e >= 23) {
        v = prod << (uint32_t)(e - 23);
    } else {
        const uint32_t r = (uint32_t)(23 - e);
        v = (prod + (1ull << (r - 1u))) >> r;
    }
    if (v > 2000000000ull) v = 2000000000ull;
    return sign ? -(int32_t)v : (int32_t)v;
}

/* Soft-float IEEE-754 binary64 -> signed round(deg * 1e7) for |deg| <= max_abs_deg (90 or 180). */
static inline int32_t zizhan_qmi_loc_f64_deg_e7(uint64_t bits, int32_t max_abs_deg, int *valid_out) {
    const uint32_t sign = (uint32_t)(bits >> 63);
    const uint32_t exp_raw = (uint32_t)((bits >> 52) & 0x7ffu);
    const uint64_t frac = bits & 0x000fffffffffffffull;
    if (exp_raw == 0x7ffu || max_abs_deg <= 0 || max_abs_deg > 180) {
        if (valid_out) *valid_out = 0;
        return 0;
    }
    if (valid_out) *valid_out = 1;
    if (exp_raw == 0u) return 0;
    const int32_t e = (int32_t)exp_raw - 1023;
    if (e < -26) return 0;
    if (e > 8) {
        if (valid_out) *valid_out = 0;
        return 0;
    }
    /* Keep 33 mantissa bits: m33 * 1e7 <= 8.59e16 < 2^57, zero 64-bit overflow. */
    const uint64_t m33 = ((frac | 0x0010000000000000ull) + (1ull << 19)) >> 20;
    const uint64_t prod = m33 * 10000000ull;
    const uint32_t rshift = (uint32_t)(32 - e);
    const uint64_t rounded = (rshift >= 63u) ? 0ull : ((prod + (1ull << (rshift - 1u))) >> rshift);
    const uint64_t limit = (uint64_t)(uint32_t)max_abs_deg * 10000000ull;
    if (rounded > limit) {
        if (valid_out) *valid_out = 0;
        return 0;
    }
    return sign ? -(int32_t)rounded : (int32_t)rounded;
}

static inline void zizhan_qmi_loc_put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static inline void zizhan_qmi_loc_put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static inline uint16_t zizhan_qmi_loc_get_u16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t zizhan_qmi_loc_get_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static inline uint64_t zizhan_qmi_loc_get_u64(const uint8_t *p) {
    return (uint64_t)zizhan_qmi_loc_get_u32(p) | ((uint64_t)zizhan_qmi_loc_get_u32(p + 4) << 32);
}

static inline uint32_t zizhan_qmi_loc_build_inform_client_rev(uint16_t txn, uint32_t rev, uint8_t *out, uint32_t cap) {
    if (!out || cap < 14u) return 0;
    out[0] = 0x00u;
    zizhan_qmi_loc_put_u16(out + 1, txn);
    zizhan_qmi_loc_put_u16(out + 3, ZIZHAN_QMI_LOC_INFORM_CLIENT_REV);
    zizhan_qmi_loc_put_u16(out + 5, 7u);
    out[7] = 0x01u;
    zizhan_qmi_loc_put_u16(out + 8, 4u);
    zizhan_qmi_loc_put_u32(out + 10, rev ? rev : 1u);
    return 14u;
}

/* Build QMI_LOC_REG_EVENTS_REQ_V02 (0x0021) matching MPSS.DE.2.0 LOC v2.152 IDL (max_msg_len=29):
 * TLV 0x01: eventRegMask (u64, 8B)
 * TLV 0x10: optional clientStrId (char[4], 4B; NULL omits TLV 0x10 for AFW/Framework client so CreateNFW is not triggered)
 * TLV 0x11: clientType (u32, 4B, 1 = AFW/HLOS, 2 = RLOC/MPSS)
 * TLV 0x12: regType (u8, 1B, 1 = REGISTER) */
static inline uint32_t zizhan_qmi_loc_build_reg_events_ex(uint16_t txn, uint64_t mask, const char *str_id4,
                                                          uint32_t client_type, uint8_t reg_type,
                                                          uint8_t *out, uint32_t cap) {
    const uint16_t body = str_id4 ? 29u : 22u;
    if (!out || cap < 7u + body) return 0;
    out[0] = 0x00u;
    zizhan_qmi_loc_put_u16(out + 1, txn);
    zizhan_qmi_loc_put_u16(out + 3, ZIZHAN_QMI_LOC_REG_EVENTS);
    zizhan_qmi_loc_put_u16(out + 5, body);
    uint32_t o = 7u;
    /* TLV 0x01: eventRegMask (uint64) */
    out[o++] = 0x01u;
    zizhan_qmi_loc_put_u16(out + o, 8u); o += 2u;
    zizhan_qmi_loc_put_u32(out + o, (uint32_t)mask); o += 4u;
    zizhan_qmi_loc_put_u32(out + o, (uint32_t)(mask >> 32)); o += 4u;
    /* TLV 0x10: optional clientStrId (4 chars, no NUL on wire) */
    if (str_id4) {
        out[o++] = 0x10u;
        zizhan_qmi_loc_put_u16(out + o, 4u); o += 2u;
        memcpy(out + o, str_id4, 4u); o += 4u;
    }
    /* TLV 0x11: clientType (uint32) */
    out[o++] = 0x11u;
    zizhan_qmi_loc_put_u16(out + o, 4u); o += 2u;
    zizhan_qmi_loc_put_u32(out + o, client_type); o += 4u;
    /* TLV 0x12: regType (uint8) */
    out[o++] = 0x12u;
    zizhan_qmi_loc_put_u16(out + o, 1u); o += 2u;
    out[o++] = reg_type;
    return o;
}

static inline uint32_t zizhan_qmi_loc_build_reg_events(uint16_t txn, uint64_t mask, uint8_t *out, uint32_t cap) {
    /* Primary: AFW Framework client (clientType=1, no CreateNFW clientStrId) */
    return zizhan_qmi_loc_build_reg_events_ex(txn, mask, NULL, 1u, 1u, out, cap);
}

static inline uint32_t zizhan_qmi_loc_build_u32_req(uint16_t txn, uint16_t msg_id, uint8_t tlv_id, uint32_t val,
                                                    uint8_t *out, uint32_t cap) {
    if (!out || cap < 14u) return 0;
    out[0] = 0x00u;
    zizhan_qmi_loc_put_u16(out + 1, txn);
    zizhan_qmi_loc_put_u16(out + 3, msg_id);
    zizhan_qmi_loc_put_u16(out + 5, 7u);
    out[7] = tlv_id;
    zizhan_qmi_loc_put_u16(out + 8, 4u);
    zizhan_qmi_loc_put_u32(out + 10, val);
    return 14u;
}

static inline uint32_t zizhan_qmi_loc_build_set_op_mode(uint16_t txn, uint32_t mode, uint8_t *out, uint32_t cap) {
    if (!out || cap < 14u) return 0;
    out[0] = 0x00u;
    zizhan_qmi_loc_put_u16(out + 1, txn);
    zizhan_qmi_loc_put_u16(out + 3, ZIZHAN_QMI_LOC_SET_OP_MODE);
    zizhan_qmi_loc_put_u16(out + 5, 7u);
    out[7] = 0x01u;
    zizhan_qmi_loc_put_u16(out + 8, 4u);
    zizhan_qmi_loc_put_u32(out + 10, mode);
    return 14u;
}

static inline uint32_t zizhan_qmi_loc_build_start(uint16_t txn, uint8_t session_id, uint32_t interval_ms,
                                                  uint8_t *out, uint32_t cap) {
    if (!out || cap < 46u) return 0;
    out[0] = 0x00u;
    zizhan_qmi_loc_put_u16(out + 1, txn);
    zizhan_qmi_loc_put_u16(out + 3, ZIZHAN_QMI_LOC_START);
    zizhan_qmi_loc_put_u16(out + 5, 39u);
    /* TLV 0x01: Session ID (uint8) */
    out[7] = 0x01u;
    zizhan_qmi_loc_put_u16(out + 8, 1u);
    out[10] = session_id;
    /* TLV 0x10: Fix Recurrence Type = PERIODIC (1) */
    out[11] = 0x10u;
    zizhan_qmi_loc_put_u16(out + 12, 4u);
    zizhan_qmi_loc_put_u32(out + 14, ZIZHAN_QMI_LOC_RECURRENCE_PERIODIC);
    /* TLV 0x11: Horizontal Accuracy Level = HIGH (3) */
    out[18] = 0x11u;
    zizhan_qmi_loc_put_u16(out + 19, 4u);
    zizhan_qmi_loc_put_u32(out + 21, 3u);
    /* TLV 0x12: Intermediate Report State = ENABLE (1) */
    out[25] = 0x12u;
    zizhan_qmi_loc_put_u16(out + 26, 4u);
    zizhan_qmi_loc_put_u32(out + 28, ZIZHAN_QMI_LOC_INTERMEDIATE_ENABLE);
    /* TLV 0x13: Minimum Interval between Position Reports (ms) */
    out[32] = 0x13u;
    zizhan_qmi_loc_put_u16(out + 33, 4u);
    zizhan_qmi_loc_put_u32(out + 35, interval_ms ? interval_ms : 1000u);
    /* TLV 0x17: Max Response Time (ms) = 20000 */
    out[39] = 0x17u;
    zizhan_qmi_loc_put_u16(out + 40, 4u);
    zizhan_qmi_loc_put_u32(out + 42, 20000u);
    return 46u;
}

static inline uint32_t zizhan_qmi_loc_build_stop(uint16_t txn, uint8_t session_id, uint8_t *out, uint32_t cap) {
    if (!out || cap < 11u) return 0;
    out[0] = 0x00u;
    zizhan_qmi_loc_put_u16(out + 1, txn);
    zizhan_qmi_loc_put_u16(out + 3, ZIZHAN_QMI_LOC_STOP);
    zizhan_qmi_loc_put_u16(out + 5, 4u);
    out[7] = 0x01u;
    zizhan_qmi_loc_put_u16(out + 8, 1u);
    out[10] = session_id;
    return 11u;
}

/* Advance handshake state machine; returns 1 and populates out/out_len when a QMI request should be sent.
 * Do not send legacy INFORM_CLIENT_REV (0x0020, rev=1) or SET_OP_MODE (0x004A): MPSS.DE.2.0 RLOC clients
 * register directly via 0x0021 (TLV 0x01/0x10/0x11/0x12) and immediately start fixing via 0x0022. */
static inline int zizhan_qmi_loc_step(ZizhanQmiLoc *s, uint64_t now_ms, uint8_t *out, uint32_t cap, uint32_t *out_len) {
    if (!s || !out || !out_len) return 0;
    *out_len = 0;
    if (s->stage == ZIZHAN_QMI_LOC_STAGE_IDLE) {
        if (now_ms < s->stage_since_ms) return 0;
        s->rev_done = 1u;
        s->stage = ZIZHAN_QMI_LOC_STAGE_WAIT_REG;
        s->stage_since_ms = now_ms;
        *out_len = zizhan_qmi_loc_build_reg_events(s->next_txn++, (uint64_t)ZIZHAN_QMI_LOC_EVENT_MASK_DEFAULT, out, cap);
        return *out_len > 0 ? 1 : 0;
    }
    if (s->stage == ZIZHAN_QMI_LOC_STAGE_WAIT_REG) {
        if (s->reg_ok >= 1u) {
            s->stage = ZIZHAN_QMI_LOC_STAGE_WAIT_MODE;
            s->stage_since_ms = now_ms;
            /* Unlock GNSS engine (SET_ENGINE_LOCK 0x003E, lockType = NONE = 1) so SESS_STATUS_ENGINE_LOCKED (7) clears */
            *out_len = zizhan_qmi_loc_build_u32_req(s->next_txn++, ZIZHAN_QMI_LOC_SET_ENGINE_LOCK, 0x01u, ZIZHAN_QMI_LOC_LOCK_NONE, out, cap);
            return *out_len > 0 ? 1 : 0;
        }
        if (s->reg_ok == 0u && s->reg_retries < 3u && now_ms - s->stage_since_ms >= 1500ull) {
            ++s->reg_retries;
            s->stage_since_ms = now_ms;
            if (s->reg_retries == 1u) {
                /* Fallback 1: RLOC profile (proven res=0 err=0 on MPSS.DE.2.0) */
                *out_len = zizhan_qmi_loc_build_reg_events_ex(s->next_txn++, (uint64_t)ZIZHAN_QMI_LOC_EVENT_MASK_DEFAULT, "RLOC", 2u, 1u, out, cap);
            } else if (s->reg_retries == 2u) {
                /* Fallback 2: TLOC profile */
                *out_len = zizhan_qmi_loc_build_reg_events_ex(s->next_txn++, 0x0003ull, "TLOC", 1u, 1u, out, cap);
            } else {
                /* Fallback 3: minimal RLOC profile */
                *out_len = zizhan_qmi_loc_build_reg_events_ex(s->next_txn++, 0x0001ull, "RLOC", 2u, 1u, out, cap);
            }
            return *out_len > 0 ? 1 : 0;
        }
        return 0;
    }
    if (s->stage == ZIZHAN_QMI_LOC_STAGE_WAIT_MODE) {
        if (s->mode_ok >= 2u || now_ms - s->stage_since_ms >= 350ull) {
            s->mode_ok = 2u;
            s->stage = ZIZHAN_QMI_LOC_STAGE_WAIT_START;
            s->stage_since_ms = now_ms;
            *out_len = zizhan_qmi_loc_build_start(s->next_txn++, s->session_id, 1000u, out, cap);
            return *out_len > 0 ? 1 : 0;
        }
        return 0;
    }
    if (s->stage == ZIZHAN_QMI_LOC_STAGE_WAIT_START) {
        if (s->start_ok >= 2u || (s->start_ok == 1u && now_ms - s->stage_since_ms >= 800ull)) {
            s->stage = ZIZHAN_QMI_LOC_STAGE_RUNNING;
            s->stage_since_ms = now_ms;
            return 0;
        }
        if (s->start_ok == 0u && s->start_retries < 3u && now_ms - s->stage_since_ms >= 2500ull) {
            ++s->start_retries;
            s->stage_since_ms = now_ms;
            *out_len = zizhan_qmi_loc_build_start(s->next_txn++, s->session_id, 1000u, out, cap);
            return *out_len > 0 ? 1 : 0;
        }
    }
    return 0;
}

/* Reset handshake state machine after an optional DMS LOW_POWER (Airplane Mode) transition. */
static inline void zizhan_qmi_loc_retry_after_lpm(ZizhanQmiLoc *s, uint64_t now_ms) {
    if (!s) return;
    s->phone_offline = 0;
    s->rev_done = 0;
    s->reg_ok = 0;
    s->mode_ok = 0;
    s->start_ok = 0;
    s->reg_retries = 0;
    s->start_retries = 0;
    s->stage = ZIZHAN_QMI_LOC_STAGE_IDLE;
    s->stage_since_ms = now_ms;
}

/* Parse a QMI response (ctl=0x02) or indication (ctl=0x04) from service 16.
 * Bitmask return: bit 0 = handshake/state changed, bit 1 = SV info updated, bit 2 = position report updated. */
static inline uint32_t zizhan_qmi_loc_on_packet(ZizhanQmiLoc *s, const uint8_t *q, uint32_t n) {
    if (!s || !q || n < 7u) return 0u;
    const uint8_t ctl = q[0];
    const uint16_t msg = zizhan_qmi_loc_get_u16(q + 3);
    const uint16_t body_len = zizhan_qmi_loc_get_u16(q + 5);
    if (7u + (uint32_t)body_len > n) return 0u;

    if (ctl == 0x02u) {   /* QMI Response */
        uint16_t res = 0xffffu, err = 0xffffu;
        for (uint32_t at = 7u; at + 3u <= 7u + (uint32_t)body_len;) {
            const uint8_t t = q[at];
            const uint16_t l = zizhan_qmi_loc_get_u16(q + at + 1u);
            if (at + 3u + (uint32_t)l > 7u + (uint32_t)body_len) break;
            if (t == 0x02u && l >= 4u) {
                res = zizhan_qmi_loc_get_u16(q + at + 3u);
                err = zizhan_qmi_loc_get_u16(q + at + 5u);
            }
            at += 3u + (uint32_t)l;
        }
        s->last_resp_msg = msg;
        s->last_resp_result = res;
        s->last_resp_error = err;
        if (msg == ZIZHAN_QMI_LOC_INFORM_CLIENT_REV) {
            s->rev_done = 1u;
        } else if (res == 0u) {
            if (msg == ZIZHAN_QMI_LOC_REG_EVENTS && s->reg_ok == 0u) s->reg_ok = 1u;
            else if ((msg == ZIZHAN_QMI_LOC_SET_OP_MODE || msg == ZIZHAN_QMI_LOC_SET_ENGINE_LOCK) && s->mode_ok == 0u) s->mode_ok = 1u;
            else if (msg == ZIZHAN_QMI_LOC_START && s->start_ok == 0u) s->start_ok = 1u;
        } else if (err == 0x0034u || err == 0x0046u) {
            s->phone_offline = 1u;
        }
        return 1u;
    }

    if (ctl != 0x04u) return 0u;   /* Indications only below */
    s->last_ind_msg = msg;

    if (msg == ZIZHAN_QMI_LOC_INFORM_CLIENT_REV || msg == ZIZHAN_QMI_LOC_REG_EVENTS ||
        msg == ZIZHAN_QMI_LOC_SET_ENGINE_LOCK || msg == ZIZHAN_QMI_LOC_SET_OP_MODE || msg == ZIZHAN_QMI_LOC_START) {
        for (uint32_t at = 7u; at + 3u <= 7u + (uint32_t)body_len;) {
            const uint8_t t = q[at];
            const uint16_t l = zizhan_qmi_loc_get_u16(q + at + 1u);
            if (at + 3u + (uint32_t)l > 7u + (uint32_t)body_len) break;
            if (t == 0x01u && l >= 4u) {
                const uint32_t st = zizhan_qmi_loc_get_u32(q + at + 3u);
                s->last_ind_status = st;
                if (st == ZIZHAN_QMI_LOC_IND_PHONE_OFFLINE) s->phone_offline = 1u;
                if (st == 0u) {
                    if (msg == ZIZHAN_QMI_LOC_INFORM_CLIENT_REV) s->rev_done = 2u;
                    else if (msg == ZIZHAN_QMI_LOC_REG_EVENTS) s->reg_ok = 2u;
                    else if (msg == ZIZHAN_QMI_LOC_SET_ENGINE_LOCK || msg == ZIZHAN_QMI_LOC_SET_OP_MODE) s->mode_ok = 2u;
                    else if (msg == ZIZHAN_QMI_LOC_START) s->start_ok = 2u;
                }
            }
            at += 3u + (uint32_t)l;
        }
        return 1u;
    }

    if (msg == ZIZHAN_QMI_LOC_FIX_SESSION_STATE_IND) {
        for (uint32_t at = 7u; at + 3u <= 7u + (uint32_t)body_len;) {
            const uint8_t t = q[at];
            const uint16_t l = zizhan_qmi_loc_get_u16(q + at + 1u);
            if (at + 3u + (uint32_t)l > 7u + (uint32_t)body_len) break;
            if (t == 0x01u && l >= 4u) {
                const uint32_t fs = zizhan_qmi_loc_get_u32(q + at + 3u);
                if (fs <= 255u) s->fix_session_state = (uint8_t)fs;
            }
            at += 3u + (uint32_t)l;
        }
        return 1u;
    }

    if (msg == ZIZHAN_QMI_LOC_ENGINE_STATE_IND) {
        for (uint32_t at = 7u; at + 3u <= 7u + (uint32_t)body_len;) {
            const uint8_t t = q[at];
            const uint16_t l = zizhan_qmi_loc_get_u16(q + at + 1u);
            if (at + 3u + (uint32_t)l > 7u + (uint32_t)body_len) break;
            if (t == 0x01u && l >= 4u) {
                const uint32_t es = zizhan_qmi_loc_get_u32(q + at + 3u);
                if (es <= 255u) s->engine_state = (uint8_t)es;
            }
            at += 3u + (uint32_t)l;
        }
        return 1u;
    }

    if (msg == ZIZHAN_QMI_LOC_SV_INFO_IND) {
        uint8_t vis = 0u, srch = 0u, trk = 0u;
        uint16_t max_cn0 = 0u;
        uint32_t sys_mask = 0u;
        for (uint32_t at = 7u; at + 3u <= 7u + (uint32_t)body_len;) {
            const uint8_t t = q[at];
            const uint16_t l = zizhan_qmi_loc_get_u16(q + at + 1u);
            if (at + 3u + (uint32_t)l > 7u + (uint32_t)body_len) break;
            const uint8_t *v = q + at + 3u;
            if (t == 0x10u && l >= 1u) {
                const uint8_t cnt = v[0];
                const uint32_t avail = (l - 1u) / 28u;
                const uint32_t n_sv = cnt < avail ? cnt : avail;
                vis = n_sv > 255u ? 255u : (uint8_t)n_sv;
                for (uint32_t i = 0u; i < n_sv; ++i) {
                    const uint8_t *e = v + 1u + i * 28u;
                    const uint32_t valid = zizhan_qmi_loc_get_u32(e + 0u);
                    const uint32_t sys = zizhan_qmi_loc_get_u32(e + 4u);
                    const uint32_t sv_st = zizhan_qmi_loc_get_u32(e + 11u);
                    const uint32_t snr_bits = zizhan_qmi_loc_get_u32(e + 24u);
                    if ((valid & 1u) && sys < 32u) sys_mask |= (1u << sys);
                    if (valid & (1u << 3)) {
                        if (sv_st == 2u && trk < 255u) ++trk;
                        else if (sv_st == 1u && srch < 255u) ++srch;
                    }
                    if (valid & (1u << 7)) {
                        int ok = 0;
                        const int32_t cn0 = zizhan_qmi_loc_f32_scaled(snr_bits, 10u, &ok);
                        if (ok && cn0 > 0 && cn0 <= 65000 && (uint16_t)cn0 > max_cn0) {
                            max_cn0 = (uint16_t)cn0;
                        }
                    }
                }
            }
            at += 3u + (uint32_t)l;
        }
        s->sv_visible = vis;
        s->sv_searching = srch;
        s->sv_tracking = trk;
        s->max_cn0_tenths = max_cn0;
        s->constellation_mask = sys_mask;
        ++s->sv_ind_count;
        return 2u;
    }

    if (msg == ZIZHAN_QMI_LOC_POS_REPORT_IND) {
        uint32_t sess_st = 0xffffffffu;
        int has_lat = 0, has_lon = 0, has_alt_sl = 0;
        int32_t lat = 0, lon = 0, h_unc = 0, alt_el = 0, alt_sl = 0, spd = 0, hdg = -10;
        uint8_t used = s->sv_used;
        uint32_t tech = s->tech_mask;
        for (uint32_t at = 7u; at + 3u <= 7u + (uint32_t)body_len;) {
            const uint8_t t = q[at];
            const uint16_t l = zizhan_qmi_loc_get_u16(q + at + 1u);
            if (at + 3u + (uint32_t)l > 7u + (uint32_t)body_len) break;
            const uint8_t *v = q + at + 3u;
            if (t == 0x01u && l >= 4u) {
                sess_st = zizhan_qmi_loc_get_u32(v);
            } else if (t == 0x10u && l >= 8u) {
                lat = zizhan_qmi_loc_f64_deg_e7(zizhan_qmi_loc_get_u64(v), 90, &has_lat);
            } else if (t == 0x11u && l >= 8u) {
                lon = zizhan_qmi_loc_f64_deg_e7(zizhan_qmi_loc_get_u64(v), 180, &has_lon);
            } else if (t == 0x12u && l >= 4u) {
                int ok = 0;
                const int32_t dm = zizhan_qmi_loc_f32_scaled(zizhan_qmi_loc_get_u32(v), 10u, &ok);
                if (ok && dm >= 0) h_unc = dm;
            } else if (t == 0x18u && l >= 4u) {
                int ok = 0;
                const int32_t cms = zizhan_qmi_loc_f32_scaled(zizhan_qmi_loc_get_u32(v), 100u, &ok);
                if (ok && cms >= 0) spd = cms;
            } else if (t == 0x1Au && l >= 4u) {
                int ok = 0;
                const int32_t dm = zizhan_qmi_loc_f32_scaled(zizhan_qmi_loc_get_u32(v), 10u, &ok);
                if (ok) alt_el = dm;
            } else if (t == 0x1Bu && l >= 4u) {
                int ok = 0;
                const int32_t dm = zizhan_qmi_loc_f32_scaled(zizhan_qmi_loc_get_u32(v), 10u, &ok);
                if (ok) { alt_sl = dm; has_alt_sl = 1; }
            } else if (t == 0x20u && l >= 4u) {
                int ok = 0;
                const int32_t dd = zizhan_qmi_loc_f32_scaled(zizhan_qmi_loc_get_u32(v), 10u, &ok);
                if (ok && dd >= 0 && dd <= 3600) hdg = dd;
            } else if (t == 0x23u && l >= 4u) {
                tech = zizhan_qmi_loc_get_u32(v);
            } else if (t == 0x2Cu && l >= 1u && 1u + 2u * (uint32_t)v[0] <= l) {
                used = v[0];
            }
            at += 3u + (uint32_t)l;
        }
        s->session_status = sess_st;
        if (sess_st == ZIZHAN_QMI_LOC_SESSION_PHONE_OFFLINE) {
            s->phone_offline = 1u;
        }
        ++s->pos_ind_count;
        if ((sess_st == 0u || sess_st == 1u) && has_lat && has_lon && !(lat == 0 && lon == 0)) {
            s->lat_e7 = lat;
            s->lon_e7 = lon;
            s->hor_unc_dm = h_unc > 0 ? h_unc : 500;
            s->alt_dm = has_alt_sl ? alt_sl : alt_el;
            s->speed_cms = spd;
            s->heading_ddeg = hdg;
            s->sv_used = used;
            s->tech_mask = tech;
            s->fix_status = (sess_st == 0u) ? 2u : 1u;
            if (sess_st == 0u) ++s->valid_fix_count;
        }
        return 4u;
    }

    return 0u;
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_QMI_LOC_H */
