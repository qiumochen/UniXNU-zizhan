/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "ShengBattmgr.h"

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static void make_reply(uint8_t out[24], uint32_t opcode, uint32_t prop, int32_t val, uint32_t result) {
    put32(out + 0, SHENG_BATTMGR_OWNER);
    put32(out + 4, SHENG_BATTMGR_TYPE_REQUEST);
    put32(out + 8, opcode);
    put32(out + 12, prop);
    put32(out + 16, (uint32_t)val);
    put32(out + 20, result);
}

static void answer_cycle(ShengBattmgr *b, int board_ok) {
    uint8_t req[SHENG_BATTMGR_REQUEST_BYTES], rep[24];
    unsigned steps = 0;
    for (;;) {
        uint32_t n = sheng_battmgr_next(b, req);
        assert(n == SHENG_BATTMGR_REQUEST_BYTES);
        uint32_t op = le32(req + 8), prop = le32(req + 16);
        int32_t val = 0;
        uint32_t res = 0;
        if (op == SHENG_BATTMGR_OP_BATTERY_GET) {
            switch (prop) {
            case 0: val = 4; break;          /* status: full */
            case 1: val = 1; break;          /* health: Good */
            case 2: val = 1; break;          /* present */
            case 3: val = 3; break;          /* charge_type */
            case 4: val = 9900; break;       /* capacity 99.00% */
            case 5: val = 78; break;         /* SOH 78% */
            case 7: val = 4428000; break;    /* 4428 mV */
            case 9: val = -250000; break;    /* charging +250 mA */
            case 10: val = 0; break;         /* thermal level */
            case 12: val = 500000; break;    /* charge current */
            case 13: val = 3540; break;      /* 35.40 C */
            case 15: val = 3400000; break;   /* 3400 mAh */
            case 16: val = 918; break;       /* cycles */
            case 17: val = 4500000; break;   /* design 4500 mAh */
            case 18: val = 3510000; break;   /* full 3510 mAh */
            case 20: val = -1; break;
            case 21: val = 7200; break;      /* 120 min */
            default: res = 1; break;
            }
        } else if (op == SHENG_BATTMGR_OP_USB_GET) {
            switch (prop) {
            case 0: val = 1; break;          /* online */
            case 1: val = 5100000; break;    /* 5100 mV */
            case 3: val = 480000; break;     /* 480 mA */
            case 4: val = 1500000; break;
            case 5: val = 900000; break;
            case 6: val = 6; break;          /* PD */
            case 7: val = 6; break;
            default: res = 1; break;
            }
        } else if (op == SHENG_BATTMGR_OP_BOARD_GET) {
            if (!board_ok) {
                res = 5;
            } else {
                switch (prop) {
                case 0x8a: val = 1710; break; /* cell 1 rem mAh */
                case 0x8b: val = 1755; break; /* cell 1 full mAh */
                case 0xa6: val = 1695; break; /* cell 2 rem mAh */
                case 0xa7: val = 1755; break; /* cell 2 full mAh */
                case 0x03: val = 1; break;    /* cell 1 auth */
                case 0xa5: val = 1; break;    /* cell 2 auth */
                case 0x18: val = 1; break;    /* PD verified */
                case 0x33: val = 67; break;   /* 67 W */
                default: val = 0; break;
                }
            }
        }
        make_reply(rep, op, prop, val, res);
        int r = sheng_battmgr_receive(b, rep, sizeof rep);
        ++steps;
        assert(steps <= 64);
        if (r == BmCycleDone) break;
        assert(r == BmAnswer);
    }
}

int main(void) {
    /* 1. Default mode (board_get = 0): skips BOARD_GET, reads BmHealth + BmSoh on first cycle. */
    ShengBattmgr b;
    sheng_battmgr_init(&b);
    sheng_battmgr_refresh(&b, 0);
    answer_cycle(&b, 0);
    assert(b.cycles_done == 1 && b.errors == 0 && b.timeouts == 0);
    ShengBatterySnapshot s;
    sheng_battmgr_snapshot(&b, &s);
    assert(s.present == 1 && s.external == 1 && s.fully_charged == 1);
    assert(s.percent == 99 && s.gauge_cells == 0);
    assert(s.voltage_mv == 4428 && s.current_ma == 250);
    assert(s.temperature_valid == 1 && s.temperature == 3540);
    assert(s.full_mah == 3510 && s.design_mah == 4500 && s.cycles == 918);
    assert(s.health == 1 && s.soh == 78);
    assert(s.adapter_family == 0xe000400au && s.adapter_watts == 2);

    /* Fast follow-up cycle skips slow fields while preserving valid BmFull/BmHealth/BmSoh. */
    uint32_t resp0 = b.responses;
    sheng_battmgr_refresh(&b, 0);
    answer_cycle(&b, 0);
    assert(b.cycles_done == 2);
    assert((b.responses - resp0) < resp0);
    sheng_battmgr_snapshot(&b, &s);
    assert(s.full_mah == 3510 && s.health == 1 && s.soh == 78);

    /* 2. Xiaomi dual-cell mode (board_get = 1): reads both cell gauges and sums remaining/full. */
    sheng_battmgr_init(&b);
    b.board_get = 1;
    sheng_battmgr_refresh(&b, 1);
    answer_cycle(&b, 1);
    sheng_battmgr_snapshot(&b, &s);
    assert(s.gauge_cells == 2);
    assert(s.gauge1_rem_mah == 1710 && s.gauge2_rem_mah == 1695);
    assert(s.gauge1_full_mah == 1755 && s.gauge2_full_mah == 1755);
    /* (1710 + 1695) / (1755 + 1755) = 3405 / 3510 = 97.008% -> 97% */
    assert(s.percent == 97);
    assert(s.adapter_watts == 67);

    /* 2b. SDP/CDP host current limit opt-in (DCP/PD remain firmware-owned). */
    ShengBattmgr bl;
    sheng_battmgr_init(&bl);
    bl.sdp_limit_ua = 900000u;
    bl.valid = (1ull << BmUsbOnline) | (1ull << BmUsbType) | (1ull << BmUsbAdapter) | (1ull << BmUsbLimit);
    bl.value[BmUsbOnline] = 1;
    bl.value[BmUsbType] = 6; /* PD: must NEVER touch firmware charging limit */
    bl.value[BmUsbAdapter] = 6;
    bl.value[BmUsbLimit] = 500000;
    sheng_battmgr_refresh(&bl, 0);
    answer_cycle(&bl, 0);
    assert(bl.limit_due == 0);
    /* SDP port (1) with 500 mA limit < wanted 900 mA -> schedules USB_SET 900000 uA */
    sheng_battmgr_init(&bl);
    bl.sdp_limit_ua = 900000u;
    sheng_battmgr_refresh(&bl, 0);
    {
        uint8_t q[SHENG_BATTMGR_REQUEST_BYTES], rp[24];
        for (;;) {
            assert(sheng_battmgr_next(&bl, q) == SHENG_BATTMGR_REQUEST_BYTES);
            uint32_t op = le32(q + 8), pr = le32(q + 16);
            int32_t v = (op == SHENG_BATTMGR_OP_USB_GET && (pr == 6 || pr == 7)) ? 1
                        : (op == SHENG_BATTMGR_OP_USB_GET && pr == 5) ? 500000 : 1;
            make_reply(rp, op, pr, v, 0);
            if (sheng_battmgr_receive(&bl, rp, sizeof rp) == BmCycleDone) break;
        }
        assert(bl.limit_due == 900000u);
    }

    /* 3. Timeout on an unanswered request skips cleanly and finishes cycle. */
    sheng_battmgr_refresh(&b, 0);
    uint8_t req[SHENG_BATTMGR_REQUEST_BYTES];
    assert(sheng_battmgr_next(&b, req) == SHENG_BATTMGR_REQUEST_BYTES);
    sheng_battmgr_timeout(&b);
    assert(b.timeouts == 1 && b.last_timeout == 0);
    answer_cycle(&b, 1);
    assert(b.cycles_done == 2);

    /* 4. Out-of-range BOARD_GET values (e.g. SM8475 100/30 + 4570/9090) fall back to BmCapacity (99%). */
    b.value[BmGauge1Remaining] = 100; b.value[BmGauge1Full] = 30;
    b.value[BmGauge2Remaining] = 4570; b.value[BmGauge2Full] = 9090;
    sheng_battmgr_snapshot(&b, &s);
    assert(s.gauge_cells == 0 && s.percent == 99);

    printf("PASS sheng-battmgr: Xiaomi property shift, BmHealth/BmSoh, dual-cell BOARD_GET, and timeout recovery\n");
    return 0;
}
