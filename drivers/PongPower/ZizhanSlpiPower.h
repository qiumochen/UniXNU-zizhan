/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SLPI_POWER_H
#define ZIZHAN_SLPI_POWER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define ZIZHAN_SLPI_POWER_COMMANDS 9u
enum ZizhanPowerKind { ZizhanPowerArc, ZizhanPowerBcm, ZizhanPowerMask };
enum ZizhanPowerResult {
    ZizhanPowerOk = 0, ZizhanPowerArgument = -1, ZizhanPowerCorrupt = -2,
    ZizhanPowerMissing = -3, ZizhanPowerDuplicate = -4,
    ZizhanPowerCorner = -5, ZizhanPowerUnknownBaseline = -6,
    ZizhanPowerVoteFailed = -7, ZizhanPowerAlreadyAttempted = -8,
    ZizhanPowerCapacity = -9
};
typedef struct {
    uint32_t address, value;
    uint8_t kind, vcd;
    char name[9];
} ZizhanPowerCommand;
typedef struct {
    ZizhanPowerCommand commands[ZIZHAN_SLPI_POWER_COMMANDS];
    unsigned count;
} ZizhanSlpiPowerPlan;

/* Pure parser/serializer. Board inputs: saved zizhan DT plus Xiaomi waipio
 * source 7d10d738b2c4299f3b8549c61a01ece39f19c788. XO=3, lcx/lmx>=384,
 * crypto_ddr peak UINT_MAX kB/s, average zero. BCM aux is NOT ARC levels.
 * Actual addresses, units and VCD grouping must come from cmd-db.
 * No MMIO, vote, QMP, sleep/wake command, PAS call or DSP start here. */
int zizhan_slpi_power_plan(const uint8_t *db, size_t bytes, ZizhanSlpiPowerPlan *out);

enum ZizhanPowerLeaseState { ZizhanPowerUnused, ZizhanPowerRetained, ZizhanPowerUncertain };
typedef struct {
    unsigned state, acknowledged;
    uint32_t attempted_address;
    unsigned attempted_count;
    int error;
} ZizhanPowerLease;
typedef struct {
    void *context;
    unsigned capacity; /* observed active TCS command capacity, not a guess */
    /* Called for ALL resources before any write. Return true only when the
     * existing shared owner knows the actual retained baseline. A guessed
     * zero or an idle TCS is not evidence of an AOP resource's current vote. */
    int (*baseline)(void *, uint32_t address, uint32_t *value);
    /* Synchronous active-only transaction, under that owner's single lock.
     * A BCM VCD is passed as one indivisible payload; never split the group
     * into rawVote's single-command transactions. Reject insufficient TCS
     * capacity before writing. True requires fresh hardware completion. */
    int (*vote)(void *, const ZizhanPowerCommand *, const uint32_t *values, unsigned count);
} ZizhanPowerOwner;

/* Caller holds the shared owner lock across baseline collection and votes.
 * Preserve larger baseline components (OR for ACV masks). On any attempted
 * write failure retain the partial state and refuse retry/release: the AOP
 * may have accepted a timed-out command. This boot-only lease ends at reset.
 * No runtime owner is wired to this candidate yet; unknown inherited votes
 * must be resolved before it can authorize SLPI startup. */
int zizhan_slpi_power_acquire(const ZizhanSlpiPowerPlan *, ZizhanPowerLease *,
                             const ZizhanPowerOwner *);
#ifdef __cplusplus
}
#endif
#endif
