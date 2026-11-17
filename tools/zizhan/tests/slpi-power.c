/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ZizhanSlpiPower.h"

static uint8_t db[0x20000], saved[0x20000];
static const char *const names[] = {"xo.lvl","lcx.lvl","lmx.lvl","CE0","SN3","SN0","SH0","MC0","ACV"};
static void w16(unsigned at, unsigned v) { db[at] = v; db[at + 1] = v >> 8; }
static void w32(unsigned at, uint32_t v) { w16(at, v); w16(at + 2, v >> 16); }
static unsigned entry(unsigned n) { return 144 + 24 * n; }
static unsigned aux(unsigned n) { return 512 + 16 * n; }
static void fixture(void) {
    memset(db, 0, sizeof db); w32(0, 1); w32(4, 0x0c0330db);
    w16(8, 3); w16(10, 0); w16(12, 512 - 144); w16(14, 3);
    w16(24, 5); w16(26, 3 * 24); w16(28, 512 - 144); w16(30, 6);
    const unsigned vcd[] = {2,1,2,0,0,0};
    for (unsigned n = 0; n < 9; ++n) {
        memcpy(db + entry(n), names[n], strlen(names[n]));
        w32(entry(n) + 16, ((n < 3 ? 3u : 5u) << 16) + 0x100 + 4 * n);
        w16(entry(n) + 22, 16 * n);
        if (n && n < 3) {
            w16(entry(n) + 20, 12);
            w16(aux(n) + 2, 64); w16(aux(n) + 4, 128);
            w16(aux(n) + 6, 384); w16(aux(n) + 8, 512);
        } else if (n >= 3) {
            w16(entry(n) + 20, 8); w32(aux(n), 1000); w16(aux(n) + 4, 16);
            db[aux(n) + 6] = vcd[n - 3];
        }
    }
}
typedef struct {
    unsigned snapshots, calls, sent, reject_baseline, fail_call;
    uint32_t baseline_word;
    int corrupt_baseline;
    const ZizhanSlpiPowerPlan *plan;
} Owner;
static int baseline(void *context, uint32_t address, uint32_t *value) {
    Owner *o = context; assert(!o->calls); ++o->snapshots;
    if (o->snapshots == o->reject_baseline) return 0;
    if (((address >> 16) & 7) == 3) *value = 4;
    else *value = (1u << 29) | (100u << 14) | o->baseline_word;
    if (o->corrupt_baseline) *value = 1u << 31;
    return 1;
}
static int vote(void *context, const ZizhanPowerCommand *commands, const uint32_t *values, unsigned count) {
    Owner *o = context; assert(o->snapshots == 9); ++o->calls;
    assert(commands == o->plan->commands + o->sent && count);
    if (commands[0].kind == ZizhanPowerArc) {
        assert(count == 1 && values[0] == 4);
    } else {
        for (unsigned i = 0; i < count; ++i) {
            assert(commands[i].vcd == commands[0].vcd);
            assert(!!(values[i] & (1u << 30)) == (i + 1 == count));
            assert(((values[i] >> 14) & 0x3fff) == 100);
            const uint32_t requested = commands[i].value & 0x3fff;
            assert((values[i] & 0x3fff) == (commands[i].kind == ZizhanPowerMask ? (8 | o->baseline_word) :
                   (requested > o->baseline_word ? requested : o->baseline_word)));
        }
    }
    if (o->calls == o->fail_call) return 0;
    o->sent += count; return 1;
}
static int acquire(ZizhanSlpiPowerPlan *p, ZizhanPowerLease *lease, Owner *o) {
    o->plan = p; ZizhanPowerOwner callbacks = {o, 16, baseline, vote};
    return zizhan_slpi_power_acquire(p, lease, &callbacks);
}
static void rejected(int expected) {
    ZizhanSlpiPowerPlan p; memset(&p, 0x77, sizeof p);
    assert(zizhan_slpi_power_plan(db, sizeof db, &p) == expected);
    ZizhanSlpiPowerPlan empty = {0}; assert(!memcmp(&p, &empty, sizeof p));
}
int main(void) {
    fixture(); memcpy(saved, db, sizeof db);
    ZizhanSlpiPowerPlan p;
    assert(!zizhan_slpi_power_plan(db, sizeof db, &p) && p.count == 9);
    assert(p.commands[0].value == 3 && p.commands[1].value == 3 && p.commands[2].value == 3);
    assert(p.commands[3].vcd == 0 && p.commands[6].vcd == 1 && p.commands[7].vcd == 2);
    for (unsigned i = 3; i < 9; ++i)
        assert(!!(p.commands[i].value & (1u << 30)) == (i == 8 || p.commands[i].vcd != p.commands[i + 1].vcd));
    /* High real aux units must use vendor arithmetic, not unconditional saturation.
     * CE0: min-one-floor(UINT_MAX * 1 / 8) * 1000 / UINT_MAX = 124. */
    w32(aux(3), UINT32_MAX); w16(aux(3) + 4, 1);
    ZizhanSlpiPowerPlan scaled; assert(!zizhan_slpi_power_plan(db, sizeof db, &scaled));
    unsigned ce = 0; for (unsigned i = 3; i < 9; ++i) if (!strcmp(scaled.commands[i].name, "CE0")) ce = i;
    assert((scaled.commands[ce].value & 0x3fff) == 124);
    memcpy(db, saved, sizeof db);

    /* Actual-phone ACV is a mask resource with aux width0. It must not
     * enter the bandwidth arithmetic; all true bandwidth widths stay >0. */
    w16(aux(8) + 4, 0);
    assert(!zizhan_slpi_power_plan(db, sizeof db, &scaled));
    unsigned masks = 0;
    for (unsigned i = 3; i < 9; ++i) if (scaled.commands[i].kind == ZizhanPowerMask) {
        assert((scaled.commands[i].value & 0x3fff) == 8); ++masks;
    }
    assert(masks == 1);
    memcpy(db, saved, sizeof db);
    for (unsigned n = 3; n < 8; ++n) {
        w16(aux(n) + 4, 0); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    }
    w32(4, 0); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    db[entry(3)] = 'X'; rejected(ZizhanPowerMissing); memcpy(db, saved, sizeof db);
    memcpy(db + entry(4), db + entry(3), 8); rejected(ZizhanPowerDuplicate); memcpy(db, saved, sizeof db);
    w32(entry(4) + 16, 0x5010c); rejected(ZizhanPowerDuplicate); memcpy(db, saved, sizeof db);
    w32(entry(3) + 16, 0x3010c); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w16(entry(3) + 22, 0xffff); w16(entry(3) + 20, 0xffff); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w16(12, 0); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w32(aux(3), 0); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w16(aux(3) + 4, 0); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    db[aux(3) + 6] = 16; rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    db[aux(3) + 7] = 1; rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w16(entry(3) + 20, 6); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w16(aux(1) + 6, 0); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    w16(aux(1) + 6, 256); w16(aux(1) + 8, 320); rejected(ZizhanPowerCorner); memcpy(db, saved, sizeof db);
    w16(aux(1) + 4, 32); rejected(ZizhanPowerCorrupt); memcpy(db, saved, sizeof db);
    assert(zizhan_slpi_power_plan(db, sizeof db - 1, &scaled) == ZizhanPowerCorrupt && !scaled.count);
    assert(zizhan_slpi_power_plan(NULL, sizeof db, &scaled) == ZizhanPowerCorrupt && !scaled.count);

    ZizhanPowerLease lease = {0}; Owner o = {.baseline_word = 2};
    assert(!acquire(&p, &lease, &o));
    assert(lease.state == ZizhanPowerRetained && lease.acknowledged == 9 && o.sent == 9 && o.calls == 6);
    assert(acquire(&p, &lease, &o) == ZizhanPowerAlreadyAttempted && o.calls == 6);
    /* The low-request control below has CE0 requested124, existing2000.
     * Preserve the larger retained vote, including average100. */
    w32(aux(3), UINT32_MAX); w16(aux(3) + 4, 1);
    assert(!zizhan_slpi_power_plan(db, sizeof db, &scaled));
    lease = (ZizhanPowerLease){0}; o = (Owner){.baseline_word = 2000};
    assert(!acquire(&scaled, &lease, &o) && lease.acknowledged == 9);
    memcpy(db, saved, sizeof db);
    lease = (ZizhanPowerLease){0}; o = (Owner){0};
    ZizhanPowerOwner small = {&o, 2, baseline, vote};
    assert(zizhan_slpi_power_acquire(&p, &lease, &small) == ZizhanPowerCapacity && !o.calls && !o.snapshots);
    small.capacity = 0;
    assert(zizhan_slpi_power_acquire(&p, &lease, &small) == ZizhanPowerCapacity && !o.calls && !o.snapshots);
    for (unsigned i = 1; i <= 9; ++i) {
        lease = (ZizhanPowerLease){0}; o = (Owner){.reject_baseline = i};
        assert(acquire(&p, &lease, &o) == ZizhanPowerUnknownBaseline && !o.calls && lease.state == ZizhanPowerUnused);
    }
    for (unsigned fail = 1; fail <= 6; ++fail) {
        lease = (ZizhanPowerLease){0}; o = (Owner){.baseline_word = 2, .fail_call = fail};
        assert(acquire(&p, &lease, &o) == ZizhanPowerVoteFailed);
        assert(lease.state == ZizhanPowerUncertain && lease.acknowledged == o.sent && lease.attempted_count);
        assert(acquire(&p, &lease, &o) == ZizhanPowerAlreadyAttempted && o.calls == fail);
    }
    lease = (ZizhanPowerLease){0}; o = (Owner){.corrupt_baseline = 1};
    assert(acquire(&p, &lease, &o) == ZizhanPowerCorrupt && !o.calls);
    for (unsigned bad = 0; bad < 5; ++bad) {
        scaled = p; lease = (ZizhanPowerLease){0}; o = (Owner){0};
        if (bad == 0) scaled.commands[3].name[8] = 'X';
        if (bad == 1) scaled.commands[3].kind = ZizhanPowerArc;
        if (bad == 2) scaled.commands[3].value |= 1u << 30;
        if (bad == 3) scaled.commands[0].value = 0;
        if (bad == 4) scaled.commands[3].value |= 1u << 31;
        assert(acquire(&scaled, &lease, &o) == ZizhanPowerCorrupt && !o.calls && !o.snapshots);
    }
    puts("PASS SLPI power: parser/scaling/VCD grouping, preserved baselines and all partial-failure boundaries; no hardware votes");
}
