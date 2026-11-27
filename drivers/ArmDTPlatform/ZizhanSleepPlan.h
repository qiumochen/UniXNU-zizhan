/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure C decision, TLMM pin mask, watchdog/autoreset guard, and coulomb-counter power math for SM8475 zizhan
 * platform sleep (POWER-02). Host-tested with ASan/UBSan by tools/zizhan/tests/zizhan-sleep.c. */
#ifndef ZIZHAN_SLEEP_PLAN_H
#define ZIZHAN_SLEEP_PLAN_H
#include <stdint.h>

/* SM8475 cape-pinctrl (audio-dma-live-dt-v1.dts): 210 GPIOs (0..209), TLMM summary SPI 208 (INTID 240),
 * qcom,gpios-reserved = <0x1c..0x1f, 0x24..0x27> (28..31 and 36..39), Hall lid sensor = GPIO 62 (active low). */
#define ZIZHAN_SLEEP_GPIO_COUNT 210u
#define ZIZHAN_SLEEP_LID_GPIO 62u
#define ZIZHAN_SLEEP_LID_INTID (32u + 208u)

#define ZIZHAN_SLEEP_INTR_ENABLE 1u
#define ZIZHAN_SLEEP_INTR_POLARITY (1u << 1)
#define ZIZHAN_SLEEP_INTR_DETECTION (3u << 2)
#define ZIZHAN_SLEEP_INTR_TARGET(cfg) (((cfg) >> 5) & 7u)
#define ZIZHAN_SLEEP_TARGET_AP 3u

enum zizhan_wake_source {
    ZizhanWakeNone = 0,
    ZizhanWakeLid = 1,
    ZizhanWakePower = 2,
    ZizhanWakeDeadline = 3
};

static inline int zizhan_sleep_pin_usable(uint32_t gpio)
{
    if (gpio >= ZIZHAN_SLEEP_GPIO_COUNT) return 0;
    if (gpio >= 28u && gpio <= 31u) return 0;
    if (gpio >= 36u && gpio <= 39u) return 0;
    return 1;
}

static inline void zizhan_sleep_tlmm_keep(const uint32_t cfg[ZIZHAN_SLEEP_GPIO_COUNT],
                                          uint32_t out[ZIZHAN_SLEEP_GPIO_COUNT])
{
    for (uint32_t i = 0; i < ZIZHAN_SLEEP_GPIO_COUNT; ++i) {
        if (!zizhan_sleep_pin_usable(i)) {
            out[i] = 0u;
            continue;
        }
        const int other_ap = (i != ZIZHAN_SLEEP_LID_GPIO) && (ZIZHAN_SLEEP_INTR_TARGET(cfg[i]) == ZIZHAN_SLEEP_TARGET_AP);
        out[i] = other_ap ? (cfg[i] & ~ZIZHAN_SLEEP_INTR_ENABLE) : cfg[i];
    }
}

/* Program GPIO 62 (active-low Hall lid: 0 = closed, 1 = open) to level-trigger on the opposite state
 * routed to AP (target 3) so EITHER unfolding from closed OR folding from open raises INTID 240. */
static inline uint32_t zizhan_sleep_lid_cfg(uint32_t cfg, int closed_at_sleep)
{
    uint32_t base = cfg & ~(ZIZHAN_SLEEP_INTR_DETECTION | ZIZHAN_SLEEP_INTR_POLARITY | (7u << 5));
    base |= (ZIZHAN_SLEEP_TARGET_AP << 5) | ZIZHAN_SLEEP_INTR_ENABLE;
    if (closed_at_sleep) {
        base |= ZIZHAN_SLEEP_INTR_POLARITY; /* level high = lid opened */
    }
    return base;
}

static inline int zizhan_sleep_lid_closed(uint32_t lid_io)
{
    return (lid_io & 1u) == 0u ? 1 : 0;
}

static inline int zizhan_sleep_lid_changed(uint32_t lid_io, int closed_at_sleep)
{
    return zizhan_sleep_lid_closed(lid_io) != (closed_at_sleep ? 1 : 0);
}

/* Gunyah watchdog is armed for 60 s; ping every 10 s (every 100 ticks at 100 ms/tick), and wake 8 s before
 * pong_autoreset deadline so the pinger thread flushes UFS klog cleanly before warm reset. */
static inline int zizhan_sleep_tick_plan(uint32_t tick_index, uint32_t tick_ms,
                                         int deadline_due_soon, int power_pressed, int lid_changed,
                                         int *do_wdt_ping)
{
    const uint32_t ticks_per_10s = (tick_ms && tick_ms <= 10000u) ? (10000u / tick_ms) : 1u;
    if (do_wdt_ping) {
        *do_wdt_ping = (ticks_per_10s > 0u && (tick_index % ticks_per_10s) == 0u) ? 1 : 0;
    }
    if (deadline_due_soon) return ZizhanWakeDeadline;
    if (power_pressed) return ZizhanWakePower;
    if (lid_changed) return ZizhanWakeLid;
    return ZizhanWakeNone;
}

static inline const char *zizhan_sleep_wake_name(uint32_t armcpu_reason, int board_source)
{
    if (board_source == ZizhanWakePower) return "power-key";
    if (board_source == ZizhanWakeLid) return "hall-lid";
    if (board_source == ZizhanWakeDeadline) return "autoreset-guard";
    if (armcpu_reason == 2u) return "timer";
    if (armcpu_reason == 3u) return "storm";
    return "none";
}

typedef struct {
    int valid;
    int32_t delta_uah;     /* positive = net discharge (q0_uah - q1_uah) */
    int32_t avg_ma;        /* negative = discharge, matching PongBattery convention */
    int32_t avg_mw;        /* positive = discharge power in mW */
} ZizhanSleepPowerSummary;

static inline ZizhanSleepPowerSummary zizhan_sleep_avg_power(int32_t q0_uah, int32_t q1_uah,
                                                             int32_t v0_mv, int32_t v1_mv,
                                                             uint64_t elapsed_ms)
{
    ZizhanSleepPowerSummary out = {0, 0, 0, 0};
    if (q0_uah <= 0 || q1_uah <= 0 || v0_mv < 2500 || v1_mv < 2500 || elapsed_ms < 5000ull) {
        return out;
    }
    const int64_t dq = (int64_t)q0_uah - (int64_t)q1_uah; /* uAh discharged */
    if (dq < -500000ll || dq > 500000ll) {
        return out;
    }
    /* uAh * 3600 s/h / (ms / 1000) = uAh * 3600000 / ms -> uA; divide by 1000 -> mA: uAh * 3600 / ms */
    const int64_t discharge_ma = (dq * 3600ll) / (int64_t)elapsed_ms;
    const int64_t avg_mv = ((int64_t)v0_mv + (int64_t)v1_mv) / 2ll;
    out.valid = 1;
    out.delta_uah = (int32_t)dq;
    out.avg_ma = (int32_t)(-discharge_ma);
    out.avg_mw = (int32_t)((discharge_ma * avg_mv) / 1000ll);
    return out;
}

static inline int zizhan_autosleep_due(int display_off, int sleep_in_progress, int hold_legacy,
                                       int audio_busy, int cam_busy, int deadline_soon,
                                       uint32_t off_seconds, uint32_t autosleep_s)
{
    if (!autosleep_s || !display_off || sleep_in_progress || hold_legacy || audio_busy || cam_busy || deadline_soon) {
        return 0;
    }
    return off_seconds >= autosleep_s ? 1 : 0;
}

/* POWER-08: Outer L18 secondary 6.56" AMOLED (DSI1 0xae96000 + INTF_2 0xae37000) power policy.
 * When outer_power_enabled is 0 or a diagnostic outer probe is active, keep UEFI's always-on state (1).
 * Otherwise, power the outer panel ON only when the phone is folded (lid_closed != 0) AND not in system sleep. */
static inline int zizhan_outer_panel_wanted(int outer_power_enabled, int lid_closed,
                                            int system_asleep, int probe_active)
{
    if (!outer_power_enabled || probe_active) {
        return 1;
    }
    if (system_asleep) {
        return 0;
    }
    return lid_closed ? 1 : 0;
}

/* Encode a 1-byte DCS short write (0x05) into an even pair of Qualcomm DSI FIFO words. */
static inline void zizhan_dsi_dcs_short0_words(uint8_t dcs_cmd, uint32_t out_words[2])
{
    out_words[0] = (1u << 31) | (0x05u << 16) | (uint32_t)dcs_cmd;
    out_words[1] = 0u;
}

/* Encode a 12-bit swapped-DBV DCS brightness write (0x51, big-endian payload on L18 prim/sec panels)
 * matching PongBacklight.cpp's verified 0xc0390003u / 0xff000051u | (wire << 8) format. */
static inline void zizhan_dsi_dcs_dbv_words(uint32_t dbv_12bit, int swapped_dbv, uint32_t out_words[2])
{
    const uint32_t clamped = dbv_12bit > 0x0fffu ? 0x0fffu : dbv_12bit;
    const uint32_t wire = swapped_dbv ? (((clamped & 0xffu) << 8) | ((clamped >> 8) & 0xffu)) : clamped;
    out_words[0] = 0xc0390003u;
    out_words[1] = 0xff000051u | (wire << 8);
}

#endif /* ZIZHAN_SLEEP_PLAN_H */
