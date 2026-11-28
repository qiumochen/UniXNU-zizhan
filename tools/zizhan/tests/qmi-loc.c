/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanQmiLoc.h"

static uint64_t f64_bits(double d) {
    uint64_t u = 0;
    memcpy(&u, &d, sizeof(u));
    return u;
}

static uint32_t f32_bits(float f) {
    uint32_t u = 0;
    memcpy(&u, &f, sizeof(u));
    return u;
}

int main(void) {
    /* 1. IEEE-754 soft-float conversion accuracy (1e-7 deg = 1.1 cm) without FP instructions. */
    static const double coords[] = {0.0, 39.9042000, -33.8688197, 116.4073963, -122.4194155, 89.9999999, -179.9999999};
    for (unsigned i = 0; i < sizeof(coords) / sizeof(coords[0]); ++i) {
        int ok = 0;
        const int32_t max_deg = fabs(coords[i]) <= 90.0 ? 90 : 180;
        const int32_t got = zizhan_qmi_loc_f64_deg_e7(f64_bits(coords[i]), max_deg, &ok);
        const int32_t want = (int32_t)llround(coords[i] * 1e7);
        assert(ok == 1);
        assert(got >= want - 1 && got <= want + 1);
    }
    int ok = 1;
    assert(zizhan_qmi_loc_f64_deg_e7(f64_bits(95.0), 90, &ok) == 0 && ok == 0);
    assert(zizhan_qmi_loc_f64_deg_e7(0x7ff8000000000000ull, 90, &ok) == 0 && ok == 0);
    assert(zizhan_qmi_loc_f32_scaled(f32_bits(38.5f), 10u, &ok) == 385 && ok == 1);

    /* 2. Handshake state machine: AFW REG_EVENTS (0x0021, 29B) -> SET_ENGINE_LOCK (0x003E) -> SET_NFW_ACCESS (0x00DC) -> START (0x0022, 46B). */
    ZizhanQmiLoc s;
    zizhan_qmi_loc_init(&s);
    uint8_t tx[64];
    uint32_t tx_len = 0;
    assert(zizhan_qmi_loc_step(&s, 1000ull, tx, sizeof(tx), &tx_len) == 1 && tx_len == 29u);
    assert(s.stage == ZIZHAN_QMI_LOC_STAGE_WAIT_REG);
    assert(zizhan_qmi_loc_get_u16(tx + 3) == ZIZHAN_QMI_LOC_REG_EVENTS);
    assert(zizhan_qmi_loc_get_u16(tx + 5) == 22u);
    assert(zizhan_qmi_loc_get_u64(tx + 10) == ZIZHAN_QMI_LOC_EVENT_MASK_DEFAULT);
    assert(tx[18] == 0x11u && zizhan_qmi_loc_get_u32(tx + 21) == 1u);
    assert(tx[25] == 0x12u && tx[28] == 1u);

    /* Also verify RLOC 36B fallback when AFW is not answered yet */
    assert(zizhan_qmi_loc_step(&s, 2600ull, tx, sizeof(tx), &tx_len) == 1 && tx_len == 36u);
    assert(tx[18] == 0x10u && memcmp(tx + 21, "RLOC", 4) == 0);

    const uint8_t reg_resp[] = {0x02, 0x02, 0x00, 0x21, 0x00, 0x07, 0x00, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(zizhan_qmi_loc_on_packet(&s, reg_resp, sizeof(reg_resp)) == 1u && s.reg_ok == 1u);

    assert(zizhan_qmi_loc_step(&s, 2700ull, tx, sizeof(tx), &tx_len) == 1 && tx_len == 14u);
    assert(zizhan_qmi_loc_get_u16(tx + 3) == ZIZHAN_QMI_LOC_SET_ENGINE_LOCK);
    assert(zizhan_qmi_loc_get_u32(tx + 10) == ZIZHAN_QMI_LOC_LOCK_NONE);

    const uint8_t lock_ind[] = {0x04, 0x03, 0x00, 0x3E, 0x00, 0x07, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(zizhan_qmi_loc_on_packet(&s, lock_ind, sizeof(lock_ind)) == 1u && s.mode_ok == 2u);

    assert(zizhan_qmi_loc_step(&s, 2750ull, tx, sizeof(tx), &tx_len) == 1 && tx_len == 46u);
    assert(s.stage == ZIZHAN_QMI_LOC_STAGE_WAIT_START);
    assert(zizhan_qmi_loc_get_u16(tx + 3) == ZIZHAN_QMI_LOC_START);
    assert(zizhan_qmi_loc_get_u16(tx + 5) == 39u);
    assert(tx[7] == 0x01u && tx[10] == 0u);
    assert(tx[18] == 0x11u && zizhan_qmi_loc_get_u32(tx + 21) == 3u);
    assert(tx[39] == 0x17u && zizhan_qmi_loc_get_u32(tx + 42) == 20000u);

    /* Simulate PHONE_OFFLINE (5) on START ind -> retry_after_lpm recovers cleanly from full handshake. */
    const uint8_t start_off[] = {0x04, 0x04, 0x00, 0x22, 0x00, 0x07, 0x00, 0x01, 0x04, 0x00, 0x05, 0x00, 0x00, 0x00};
    assert(zizhan_qmi_loc_on_packet(&s, start_off, sizeof(start_off)) == 1u && s.phone_offline == 1u);
    zizhan_qmi_loc_retry_after_lpm(&s, 4000ull);
    assert(s.phone_offline == 0u && s.stage == ZIZHAN_QMI_LOC_STAGE_IDLE);
    assert(zizhan_qmi_loc_step(&s, 4010ull, tx, sizeof(tx), &tx_len) == 1 && zizhan_qmi_loc_get_u16(tx + 3) == ZIZHAN_QMI_LOC_REG_EVENTS);
    const uint8_t reg_ind[] = {0x04, 0x05, 0x00, 0x21, 0x00, 0x07, 0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert(zizhan_qmi_loc_on_packet(&s, reg_ind, sizeof(reg_ind)) == 1u && s.reg_ok == 2u);
    assert(zizhan_qmi_loc_step(&s, 4100ull, tx, sizeof(tx), &tx_len) == 1 && zizhan_qmi_loc_get_u16(tx + 3) == ZIZHAN_QMI_LOC_SET_ENGINE_LOCK);
    assert(zizhan_qmi_loc_step(&s, 4500ull, tx, sizeof(tx), &tx_len) == 1 && zizhan_qmi_loc_get_u16(tx + 3) == ZIZHAN_QMI_LOC_START);

    /* 3. Engine state + SV info (2 satellites: GPS tracking 41.2 dB-Hz, BDS searching 26.4 dB-Hz). */
    const uint8_t eng_on[] = {0x04, 0x00, 0x00, 0x2B, 0x00, 0x07, 0x00, 0x01, 0x04, 0x00, 0x01, 0x00, 0x00, 0x00};
    assert(zizhan_qmi_loc_on_packet(&s, eng_on, sizeof(eng_on)) == 1u && s.engine_state == 1u);

    uint8_t sv_pkt[7 + 3 + 1 + 2 * 28] = {0};
    sv_pkt[0] = 0x04;
    zizhan_qmi_loc_put_u16(sv_pkt + 3, ZIZHAN_QMI_LOC_SV_INFO_IND);
    zizhan_qmi_loc_put_u16(sv_pkt + 5, 3 + 1 + 2 * 28);
    sv_pkt[7] = 0x10;
    zizhan_qmi_loc_put_u16(sv_pkt + 8, 1 + 2 * 28);
    sv_pkt[10] = 2;
    uint8_t *e0 = sv_pkt + 11, *e1 = sv_pkt + 11 + 28;
    zizhan_qmi_loc_put_u32(e0 + 0, 0x89u);
    zizhan_qmi_loc_put_u32(e0 + 4, 1u);    /* GPS */
    zizhan_qmi_loc_put_u32(e0 + 11, 2u);   /* TRACKING */
    zizhan_qmi_loc_put_u32(e0 + 24, f32_bits(41.2f));
    zizhan_qmi_loc_put_u32(e1 + 0, 0x89u);
    zizhan_qmi_loc_put_u32(e1 + 4, 4u);    /* BDS */
    zizhan_qmi_loc_put_u32(e1 + 11, 1u);   /* SEARCHING */
    zizhan_qmi_loc_put_u32(e1 + 24, f32_bits(26.4f));
    assert(zizhan_qmi_loc_on_packet(&s, sv_pkt, sizeof(sv_pkt)) == 2u);
    assert(s.sv_visible == 2u && s.sv_tracking == 1u && s.sv_searching == 1u && s.max_cn0_tenths == 412u);
    assert(s.constellation_mask == ((1u << 1) | (1u << 4)));

    /* 4. Position report indication (synthetic test point 37.3349000, -122.0090200, h_unc 4.5 m, alt 52.3 m). */
    uint8_t pos_pkt[128] = {0};
    uint32_t o = 7;
    pos_pkt[0] = 0x04;
    zizhan_qmi_loc_put_u16(pos_pkt + 3, ZIZHAN_QMI_LOC_POS_REPORT_IND);
    pos_pkt[o++] = 0x01; zizhan_qmi_loc_put_u16(pos_pkt + o, 4); o += 2; zizhan_qmi_loc_put_u32(pos_pkt + o, 0); o += 4;
    uint64_t lb = f64_bits(37.3349000), lob = f64_bits(-122.0090200);
    pos_pkt[o++] = 0x10; zizhan_qmi_loc_put_u16(pos_pkt + o, 8); o += 2; memcpy(pos_pkt + o, &lb, 8); o += 8;
    pos_pkt[o++] = 0x11; zizhan_qmi_loc_put_u16(pos_pkt + o, 8); o += 2; memcpy(pos_pkt + o, &lob, 8); o += 8;
    pos_pkt[o++] = 0x12; zizhan_qmi_loc_put_u16(pos_pkt + o, 4); o += 2; zizhan_qmi_loc_put_u32(pos_pkt + o, f32_bits(4.5f)); o += 4;
    pos_pkt[o++] = 0x1B; zizhan_qmi_loc_put_u16(pos_pkt + o, 4); o += 2; zizhan_qmi_loc_put_u32(pos_pkt + o, f32_bits(52.3f)); o += 4;
    pos_pkt[o++] = 0x2C; zizhan_qmi_loc_put_u16(pos_pkt + o, 7); o += 2; pos_pkt[o++] = 3; o += 6;
    zizhan_qmi_loc_put_u16(pos_pkt + 5, (uint16_t)(o - 7));
    assert(zizhan_qmi_loc_on_packet(&s, pos_pkt, o) == 4u);
    assert(s.fix_status == 2u && s.valid_fix_count == 1u && s.sv_used == 3u);
    assert(s.lat_e7 >= 373348999 && s.lat_e7 <= 373349001);
    assert(s.lon_e7 >= -1220090201 && s.lon_e7 <= -1220090199);
    assert(s.hor_unc_dm == 45 && s.alt_dm == 523);

    /* 5. Stop builder & malformed input guards. */
    assert(zizhan_qmi_loc_build_stop(9u, 1u, tx, sizeof(tx)) == 11u);
    assert(zizhan_qmi_loc_on_packet(&s, pos_pkt, 6u) == 0u);

    printf("PASS qmi-loc\n");
    return 0;
}
