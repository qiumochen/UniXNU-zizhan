/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Platform sleep (suspend to idle) for SM8475 zizhan without Apple's sleep hardware, adapted from UniXNU ArmCpuSleep.h:
 * secondary cores turn off via PSCI CPU_OFF; the boot core waits in WFI with interrupts masked, keeping only owned
 * wake SPIs and the virtual timer (PPI 27) active. Host-tested by tools/zizhan/tests/zizhan-sleep.c. */
#ifndef ARMCPU_SLEEP_H
#define ARMCPU_SLEEP_H
#include <stdint.h>
#include <string.h>

#define ARMCPU_SLEEP_SPI_WORDS 32u
#define ARMCPU_SLEEP_NO_TIMER 0xffffffffu
#define ARMCPU_SLEEP_NEVER UINT64_MAX
#define ARMCPU_SLEEP_SPURIOUS 1023u
#define ARMCPU_SLEEP_STORM_LIMIT 1000u

struct armcpu_sleep_board {
    void (*prepare)(void);
    int (*should_wake)(uint32_t intid);
    void (*restore)(void);
    const uint32_t *wake_spis;
    uint32_t wake_spi_count;
    int (*tick)(void);
    uint32_t tick_ms;
};
#define ARMCPU_SLEEP_BOARD_SIZE 56u

enum armcpu_wake_reason {
    ArmcpuWakeNone = 0,
    ArmcpuWakeSource = 1,
    ArmcpuWakeTimer = 2,
    ArmcpuWakeStorm = 3
};

struct armcpu_sleep_stats {
    uint32_t phase, entries, spurious, last_intid, last_reason, cpu_off_failures;
    uint64_t asleep_ticks;
    uint32_t masked, ticks, unarmed;
};
#define ARMCPU_SLEEP_STATS_SIZE 48u

/* Build the GICD_ISENABLER words active while asleep: any wake_spis bit present in owned_mask is enabled
 * during sleep (even if kept masked while awake), and saved restores the awake mask before returning. */
static inline void armcpu_sleep_keep_owned(const uint32_t owned_mask[ARMCPU_SLEEP_SPI_WORDS],
                                           const uint32_t *wake_spis, uint32_t count,
                                           uint32_t keep[ARMCPU_SLEEP_SPI_WORDS])
{
    memset(keep, 0, ARMCPU_SLEEP_SPI_WORDS * sizeof *keep);
    if (!owned_mask || !wake_spis) return;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t intid = wake_spis[i];
        if (intid < 32u || intid > 1019u) continue;
        const uint32_t bit = 1u << (intid % 32u);
        keep[intid / 32u] |= owned_mask[intid / 32u] & bit;
    }
}

static inline enum armcpu_wake_reason armcpu_sleep_decide(uint32_t intid, int board_says_wake, uint32_t timer_intid)
{
    if (board_says_wake) return ArmcpuWakeSource;
    if (timer_intid != ARMCPU_SLEEP_NO_TIMER && intid == timer_intid) return ArmcpuWakeTimer;
    return ArmcpuWakeNone;
}

static inline int armcpu_sleep_armed(const uint32_t keep[ARMCPU_SLEEP_SPI_WORDS])
{
    for (uint32_t w = 1; w < ARMCPU_SLEEP_SPI_WORDS; ++w) {
        if (keep[w]) return 1;
    }
    return 0;
}

static inline uint64_t armcpu_sleep_deadline(uint64_t start, uint32_t seconds, uint64_t hz)
{
    return seconds ? start + (uint64_t)seconds * hz : ARMCPU_SLEEP_NEVER;
}

static inline uint64_t armcpu_sleep_tick_interval(uint32_t tick_ms, uint64_t hz)
{
    return (tick_ms && hz) ? ((uint64_t)tick_ms * hz) / 1000ull : 0ull;
}

static inline uint64_t armcpu_sleep_next(uint64_t wake_at, uint64_t tick_at)
{
    return wake_at < tick_at ? wake_at : tick_at;
}

enum armcpu_timer_event {
    ArmcpuTimerNothing = 0,
    ArmcpuTimerTick = 1,
    ArmcpuTimerWake = 2
};

static inline enum armcpu_timer_event armcpu_sleep_timer(uint64_t now, uint64_t wake_at,
                                                         uint64_t *tick_at, uint64_t tick_ticks)
{
    if (now >= wake_at) return ArmcpuTimerWake;
    if (*tick_at != ARMCPU_SLEEP_NEVER && now >= *tick_at) {
        *tick_at = tick_ticks ? (now + tick_ticks) : ARMCPU_SLEEP_NEVER;
        return ArmcpuTimerTick;
    }
    return ArmcpuTimerNothing;
}

static inline uint32_t armcpu_sleep_pending_source(const uint32_t pending[ARMCPU_SLEEP_SPI_WORDS],
                                                   const uint32_t keep[ARMCPU_SLEEP_SPI_WORDS])
{
    for (uint32_t w = 1; w < ARMCPU_SLEEP_SPI_WORDS; ++w) {
        const uint32_t both = pending[w] & keep[w];
        if (both) return w * 32u + (uint32_t)__builtin_ctz(both);
    }
    return ARMCPU_SLEEP_SPURIOUS;
}

struct armcpu_sleep_storm {
    uint32_t intid, count;
    uint64_t last;
};

static inline int armcpu_sleep_storm(struct armcpu_sleep_storm *s, uint32_t intid, uint64_t now, uint64_t window)
{
    if (s->count == 0 || intid != s->intid || now - s->last > window) {
        s->intid = intid;
        s->count = 0;
    }
    s->last = now;
    if (++s->count < ARMCPU_SLEEP_STORM_LIMIT) return 0;
    s->count = 0;
    return 1;
}

#endif /* ARMCPU_SLEEP_H */
