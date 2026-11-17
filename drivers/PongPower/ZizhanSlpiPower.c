/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "ZizhanSlpiPower.h"
#include <string.h>

#define DB_BYTES 0x20000u
#define BCM_VALID (1u << 29)
#define BCM_COMMIT (1u << 30)
#define BCM_ALLOWED (BCM_VALID | BCM_COMMIT | 0x0fffffffu)
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static uint32_t le32(const uint8_t *p) { return le16(p) | (uint32_t)le16(p + 2) << 16; }
static int fits(size_t at, size_t n, size_t size) { return at <= size && n <= size - at; }
static uint64_t bcm_div(uint64_t value, uint32_t base) { return value && value < base ? 1 : value / base; }

typedef struct { uint32_t address; const uint8_t *aux; size_t bytes; } Resource;
static const char *const names[] = {"xo.lvl", "lcx.lvl", "lmx.lvl", "CE0", "SN3", "SN0", "SH0", "MC0", "ACV"};

static int parse(const uint8_t *db, size_t bytes, Resource *r) {
    if (!db || bytes != DB_BYTES || !le32(db) || le32(db + 4) != 0x0c0330db) return ZizhanPowerCorrupt;
    size_t heads[8], lengths[8], data[8]; unsigned rows = 0, seen = 0;
    for (unsigned i = 0; i < 8; ++i) {
        const uint8_t *h = db + 8 + 16 * i;
        if (!le16(h)) break;
        heads[i] = 144u + le16(h + 2); lengths[i] = (size_t)le16(h + 6) * 24;
        data[i] = 144u + le16(h + 4);
        if (!fits(heads[i], lengths[i], bytes) || !fits(data[i], 0, bytes)) return ZizhanPowerCorrupt;
        for (unsigned j = 0; j < i; ++j)
            if (heads[i] < heads[j] + lengths[j] && heads[j] < heads[i] + lengths[i]) return ZizhanPowerCorrupt;
        ++rows;
    }
    for (unsigned i = 0; i < rows; ++i) for (size_t at = heads[i]; at < heads[i] + lengths[i]; at += 24) {
        const uint8_t *e = db + at;
        for (unsigned n = 0; n < ZIZHAN_SLPI_POWER_COMMANDS; ++n) {
            char key[8] = {0}; memcpy(key, names[n], strlen(names[n]));
            if (memcmp(e, key, sizeof key)) continue;
            if (seen & (1u << n)) return ZizhanPowerDuplicate;
            const unsigned slave = le16(db + 8 + 16 * i);
            const uint32_t address = le32(e + 16);
            const size_t start = data[i] + le16(e + 22), length = le16(e + 20);
            if (!address || (address & 3) || ((address >> 16) & 7) != slave || slave != (n < 3 ? 3u : 5u) ||
                !fits(start, length, bytes)) return ZizhanPowerCorrupt;
            for (unsigned j = 0; j < rows; ++j)
                if (length && start < heads[j] + lengths[j] && heads[j] < start + length) return ZizhanPowerCorrupt;
            for (unsigned j = 0; j < ZIZHAN_SLPI_POWER_COMMANDS; ++j)
                if ((seen & (1u << j)) && r[j].address == address) return ZizhanPowerDuplicate;
            r[n] = (Resource){address, db + start, length}; seen |= 1u << n;
        }
    }
    return seen == (1u << ZIZHAN_SLPI_POWER_COMMANDS) - 1 ? 0 : ZizhanPowerMissing;
}

static int corner(const Resource *r, uint32_t *index) {
    if (r->bytes < 4 || r->bytes > 32 || (r->bytes & 1) || le16(r->aux)) return ZizhanPowerCorrupt;
    uint16_t previous = 0; int padded = 0, found = 0;
    for (unsigned i = 1; i < r->bytes / 2; ++i) {
        const uint16_t level = le16(r->aux + 2 * i);
        if (!level) padded = 1;
        else {
            if (padded || level < previous) return ZizhanPowerCorrupt;
            if (!found && level >= 384) { *index = i; found = 1; }
            previous = level;
        }
    }
    return found ? 0 : ZizhanPowerCorner;
}

int zizhan_slpi_power_plan(const uint8_t *db, size_t bytes, ZizhanSlpiPowerPlan *out) {
    if (!out) return ZizhanPowerArgument;
    memset(out, 0, sizeof *out);
    Resource r[ZIZHAN_SLPI_POWER_COMMANDS] = {{0}};
    int rc = parse(db, bytes, r); if (rc) return rc;
    ZizhanSlpiPowerPlan plan = {0};
    /* Matched waipio node buswidths: CE0 crypto8, SN3 aggre2-16,
     * SN0 gemnoc-sf16, SH0 llcc16, MC0 ebi4; ACV is an enable mask.
     * Peak ignores channel count; average would divide by it as well.
     * Keep the vendor's two-stage min-one/floor division and clamp. */
    static const unsigned node_width[] = {8, 16, 16, 16, 4, 4};
    for (unsigned n = 0; n < ZIZHAN_SLPI_POWER_COMMANDS; ++n) {
        ZizhanPowerCommand *c = &plan.commands[n];
        c->address = r[n].address; memcpy(c->name, names[n], strlen(names[n]) + 1);
        if (n < 3) {
            c->kind = ZizhanPowerArc; c->value = 3;
            if (n && (rc = corner(&r[n], &c->value))) return rc;
        } else {
            if (r[n].bytes != 8 || !le32(r[n].aux) || (n != 8 && !le16(r[n].aux + 4)) || r[n].aux[6] >= 16 || r[n].aux[7])
                return ZizhanPowerCorrupt;
            c->kind = n == 8 ? ZizhanPowerMask : ZizhanPowerBcm;
            c->vcd = r[n].aux[6];
            // Actual zizhan ACV aux has width0: it is an enable mask,
            // not a bandwidth quantity. Never run bandwidth scaling for it.
            if (n == 8) { c->value = BCM_VALID | 8u; continue; }
            uint64_t peak = bcm_div((uint64_t)UINT32_MAX * le16(r[n].aux + 4), node_width[n - 3]);
            peak = bcm_div(peak * 1000, le32(r[n].aux));
            if (peak > 0x3fff) peak = 0x3fff;
            c->value = BCM_VALID | (uint32_t)peak;
        }
    }
    /* A VCD's final command commits its whole group. Stable within VCD. */
    for (unsigned i = 4; i < ZIZHAN_SLPI_POWER_COMMANDS; ++i) {
        ZizhanPowerCommand c = plan.commands[i]; unsigned j = i;
        while (j > 3 && plan.commands[j - 1].vcd > c.vcd) { plan.commands[j] = plan.commands[j - 1]; --j; }
        plan.commands[j] = c;
    }
    for (unsigned i = 3; i < ZIZHAN_SLPI_POWER_COMMANDS; ++i)
        if (i + 1 == ZIZHAN_SLPI_POWER_COMMANDS || plan.commands[i].vcd != plan.commands[i + 1].vcd)
            plan.commands[i].value |= BCM_COMMIT;
    plan.count = ZIZHAN_SLPI_POWER_COMMANDS; *out = plan; return 0;
}

static int merge(const ZizhanPowerCommand *c, uint32_t old, uint32_t *out) {
    if (c->kind == ZizhanPowerArc) {
        if (old > 0xffff || c->value > 0xffff) return ZizhanPowerCorrupt;
        *out = old > c->value ? old : c->value; return 0;
    }
    if ((old | c->value) & ~BCM_ALLOWED) return ZizhanPowerCorrupt;
    const uint32_t ax = (old >> 14) & 0x3fff, ay = old & 0x3fff;
    const uint32_t bx = (c->value >> 14) & 0x3fff, by = c->value & 0x3fff;
    const uint32_t y = c->kind == ZizhanPowerMask ? (ay | by) : (ay > by ? ay : by);
    *out = (c->value & BCM_COMMIT) | ((old | c->value) & BCM_VALID) |
           ((ax > bx ? ax : bx) << 14) | y;
    return 0;
}

int zizhan_slpi_power_acquire(const ZizhanSlpiPowerPlan *p, ZizhanPowerLease *lease, const ZizhanPowerOwner *owner) {
    if (!p || !lease || !owner || !owner->baseline || !owner->vote || p->count != ZIZHAN_SLPI_POWER_COMMANDS)
        return ZizhanPowerArgument;
    if (lease->state != ZizhanPowerUnused) return ZizhanPowerAlreadyAttempted;
    if (!owner->capacity || owner->capacity > 16) return ZizhanPowerCapacity;
    uint32_t values[ZIZHAN_SLPI_POWER_COMMANDS];
    /* Validate the entire supplied plan before callbacks, including a plan
     * modified after parsing. No vote is sent for an incomplete identity. */
    for (unsigned i = 0; i < p->count; ++i) {
        const ZizhanPowerCommand *c = &p->commands[i];
        if (c->name[8]) return ZizhanPowerCorrupt;
        unsigned identity = ZIZHAN_SLPI_POWER_COMMANDS;
        for (unsigned n = 0; n < ZIZHAN_SLPI_POWER_COMMANDS; ++n)
            if (!memcmp(c->name, names[n], strlen(names[n]) + 1)) { identity = n; break; }
        if (!c->address || (c->address & 3) || c->kind > ZizhanPowerMask ||
            identity == ZIZHAN_SLPI_POWER_COMMANDS || (i < 3 ? identity != i : identity < 3) ||
            c->kind != (identity < 3 ? ZizhanPowerArc : identity == 8 ? ZizhanPowerMask : ZizhanPowerBcm) ||
            ((c->address >> 16) & 7) != (c->kind == ZizhanPowerArc ? 3u : 5u)) return ZizhanPowerCorrupt;
        if (i < 3) {
            if ((i == 0 && c->value != 3) || (i && (!c->value || c->value > 15))) return ZizhanPowerCorrupt;
        } else {
            const int commit = i + 1 == p->count || c->vcd != p->commands[i + 1].vcd;
            if (c->vcd >= 16 || (i > 3 && c->vcd < p->commands[i - 1].vcd) ||
                !(c->value & BCM_VALID) || (c->value & ~(BCM_VALID | BCM_COMMIT | 0x3fffu)) ||
                (!!(c->value & BCM_COMMIT) != commit) || !(c->value & 0x3fff) ||
                (c->kind == ZizhanPowerMask && (c->value & 0x3fff) != 8)) return ZizhanPowerCorrupt;
        }
        for (unsigned j = 0; j < i; ++j)
            if (p->commands[j].address == c->address || !strcmp(p->commands[j].name, c->name)) return ZizhanPowerDuplicate;
    }
    for (unsigned i = 3; i < p->count;) {
        unsigned count = 1;
        while (i + count < p->count && p->commands[i + count].vcd == p->commands[i].vcd) ++count;
        if (count > owner->capacity) return ZizhanPowerCapacity;
        i += count;
    }
    for (unsigned i = 0; i < p->count; ++i) {
        uint32_t baseline = 0;
        if (!owner->baseline(owner->context, p->commands[i].address, &baseline)) return ZizhanPowerUnknownBaseline;
        int rc = merge(&p->commands[i], baseline, &values[i]); if (rc) return rc;
    }
    for (unsigned i = 0; i < p->count;) {
        unsigned count = 1;
        if (i >= 3) while (i + count < p->count && p->commands[i + count].vcd == p->commands[i].vcd) ++count;
        lease->state = ZizhanPowerUncertain; lease->attempted_address = p->commands[i].address;
        lease->attempted_count = count;
        if (!owner->vote(owner->context, p->commands + i, values + i, count)) {
            lease->error = ZizhanPowerVoteFailed; return lease->error;
        }
        lease->acknowledged += count; i += count;
    }
    lease->state = ZizhanPowerRetained; lease->error = 0; return 0;
}
