/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host unit tests for ArmCpuSleep.h and ZizhanSleepPlan.h (Track 1 POWER-02). */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../../drivers/ArmCpu/ArmCpuSleep.h"
#include "../../../drivers/ArmDTPlatform/ZizhanSleepPlan.h"

static void set_bit(uint32_t words[ARMCPU_SLEEP_SPI_WORDS], uint32_t intid)
{
    words[intid / 32u] |= 1u << (intid % 32u);
}

static int has_bit(const uint32_t words[ARMCPU_SLEEP_SPI_WORDS], uint32_t intid)
{
    return (words[intid / 32u] >> (intid % 32u)) & 1u;
}

static void test_armcpu_sleep(void)
{
    assert(sizeof(struct armcpu_sleep_board) == ARMCPU_SLEEP_BOARD_SIZE);
    assert(sizeof(struct armcpu_sleep_stats) == ARMCPU_SLEEP_STATS_SIZE);

    uint32_t owned[ARMCPU_SLEEP_SPI_WORDS] = {0}, keep[ARMCPU_SLEEP_SPI_WORDS];
    set_bit(owned, 240u);
    set_bit(owned, 172u);
    const uint32_t wake[] = {240u, 513u, 15u, 2000u};
    armcpu_sleep_keep_owned(owned, wake, 4u, keep);
    assert(has_bit(keep, 240u));
    assert(!has_bit(keep, 172u));
    assert(!has_bit(keep, 513u));
    assert(keep[0] == 0u);
    assert(armcpu_sleep_armed(keep) == 1);

    assert(armcpu_sleep_decide(240u, 1, 27u) == ArmcpuWakeSource);
    assert(armcpu_sleep_decide(27u, 0, 27u) == ArmcpuWakeTimer);
    assert(armcpu_sleep_decide(240u, 0, 27u) == ArmcpuWakeNone);

    assert(armcpu_sleep_deadline(1000ull, 0u, 19200000ull) == ARMCPU_SLEEP_NEVER);
    assert(armcpu_sleep_deadline(1000ull, 3600u, 19200000ull) == 1000ull + 3600ull * 19200000ull);
    assert(armcpu_sleep_tick_interval(100u, 19200000ull) == 1920000ull);

    uint64_t tick_at = 500ull;
    assert(armcpu_sleep_timer(520ull, 1000ull, &tick_at, 100ull) == ArmcpuTimerTick);
    assert(tick_at == 620ull);
    assert(armcpu_sleep_timer(1000ull, 1000ull, &tick_at, 100ull) == ArmcpuTimerWake);

    uint32_t pending[ARMCPU_SLEEP_SPI_WORDS] = {0};
    set_bit(pending, 172u);
    assert(armcpu_sleep_pending_source(pending, keep) == ARMCPU_SLEEP_SPURIOUS);
    set_bit(pending, 240u);
    assert(armcpu_sleep_pending_source(pending, keep) == 240u);

    struct armcpu_sleep_storm storm = {0, 0, 0};
    for (unsigned i = 1; i < ARMCPU_SLEEP_STORM_LIMIT; ++i) {
        assert(armcpu_sleep_storm(&storm, 240u, i * 2ull, 10ull) == 0);
    }
    assert(armcpu_sleep_storm(&storm, 240u, ARMCPU_SLEEP_STORM_LIMIT * 2ull, 10ull) == 1);
}

static void test_zizhan_sleep_plan(void)
{
    /* Reserved SM8475 zizhan GPIOs: 28..31 and 36..39 must never be touched! */
    assert(zizhan_sleep_pin_usable(0u) == 1);
    assert(zizhan_sleep_pin_usable(27u) == 1);
    for (uint32_t g = 28u; g <= 31u; ++g) assert(zizhan_sleep_pin_usable(g) == 0);
    assert(zizhan_sleep_pin_usable(32u) == 1);
    for (uint32_t g = 36u; g <= 39u; ++g) assert(zizhan_sleep_pin_usable(g) == 0);
    assert(zizhan_sleep_pin_usable(62u) == 1);
    assert(zizhan_sleep_pin_usable(209u) == 1);
    assert(zizhan_sleep_pin_usable(210u) == 0);

    uint32_t saved[ZIZHAN_SLEEP_GPIO_COUNT] = {0}, asleep[ZIZHAN_SLEEP_GPIO_COUNT] = {0};
    saved[10] = (3u << 5) | 1u; /* AP-targeted enabled IRQ -> disabled while asleep */
    saved[12] = (2u << 5) | 1u; /* DSP-targeted enabled IRQ -> preserved */
    saved[28] = 0xffu;          /* reserved -> zeroed */
    saved[62] = (3u << 5) | (3u << 2);
    zizhan_sleep_tlmm_keep(saved, asleep);
    assert((asleep[10] & 1u) == 0u);
    assert(asleep[12] == saved[12]);
    assert(asleep[28] == 0u);

    /* Lid GPIO 62 polarity: closed (0) -> level high (polarity=1); open (1) -> level low (polarity=0) */
    const uint32_t cfg_closed = zizhan_sleep_lid_cfg(0u, 1);
    assert((cfg_closed & ZIZHAN_SLEEP_INTR_ENABLE) != 0u);
    assert((cfg_closed & ZIZHAN_SLEEP_INTR_POLARITY) != 0u);
    assert((cfg_closed & ZIZHAN_SLEEP_INTR_DETECTION) == 0u);

    const uint32_t cfg_open = zizhan_sleep_lid_cfg(0xffu, 0);
    assert((cfg_open & ZIZHAN_SLEEP_INTR_ENABLE) != 0u);
    assert((cfg_open & ZIZHAN_SLEEP_INTR_POLARITY) == 0u);
    assert((cfg_open & ZIZHAN_SLEEP_INTR_DETECTION) == 0u);

    assert(zizhan_sleep_lid_closed(0u) == 1);
    assert(zizhan_sleep_lid_closed(1u) == 0);
    assert(zizhan_sleep_lid_changed(1u, 1) == 1);
    assert(zizhan_sleep_lid_changed(0u, 1) == 0);
    assert(zizhan_sleep_lid_changed(0u, 0) == 1);

    /* Watchdog 10 s cadence across 100 ms ticks & autoreset deadline guard */
    int ping = 0;
    assert(zizhan_sleep_tick_plan(0u, 100u, 0, 0, 0, &ping) == ZizhanWakeNone && ping == 1);
    assert(zizhan_sleep_tick_plan(50u, 100u, 0, 0, 0, &ping) == ZizhanWakeNone && ping == 0);
    assert(zizhan_sleep_tick_plan(100u, 100u, 0, 0, 0, &ping) == ZizhanWakeNone && ping == 1);
    assert(zizhan_sleep_tick_plan(12u, 100u, 0, 1, 0, &ping) == ZizhanWakePower);
    assert(zizhan_sleep_tick_plan(12u, 100u, 0, 0, 1, &ping) == ZizhanWakeLid);
    assert(zizhan_sleep_tick_plan(12u, 100u, 1, 1, 1, &ping) == ZizhanWakeDeadline);

    /* Coulomb-counter sleep discharge math: 2000 uAh over 90000 ms (90 s) = 80 mA -> -80 mA, ~336 mW @ 4200 mV */
    const ZizhanSleepPowerSummary p = zizhan_sleep_avg_power(4000000, 3998000, 4200, 4200, 90000ull);
    assert(p.valid == 1);
    assert(p.delta_uah == 2000);
    assert(p.avg_ma == -80);
    assert(p.avg_mw == 336);

    /* Auto-sleep gating */
    assert(zizhan_autosleep_due(1, 0, 0, 0, 0, 0, 4u, 4u) == 1);
    assert(zizhan_autosleep_due(1, 0, 0, 1, 0, 0, 10u, 4u) == 0); /* audio busy holds off */
    assert(zizhan_autosleep_due(1, 0, 1, 0, 0, 0, 10u, 4u) == 0); /* legacy A/B hold */
    assert(zizhan_autosleep_due(0, 0, 0, 0, 0, 0, 10u, 4u) == 0); /* display on */

    /* POWER-08: 250 ms sleep tick cadence (40 ticks = 10 s watchdog keepalive) */
    assert(armcpu_sleep_tick_interval(250u, 19200000ull) == 4800000ull);
    assert(zizhan_sleep_tick_plan(0u, 250u, 0, 0, 0, &ping) == ZizhanWakeNone && ping == 1);
    assert(zizhan_sleep_tick_plan(20u, 250u, 0, 0, 0, &ping) == ZizhanWakeNone && ping == 0);
    assert(zizhan_sleep_tick_plan(40u, 250u, 0, 0, 0, &ping) == ZizhanWakeNone && ping == 1);

    /* POWER-08 & OUTER-05: Outer DSI1 panel target state, cover blanking, clock hold & DCS FIFO word encoding */
    assert(zizhan_outer_panel_wanted(0, 0, 1, 0) == 1); /* disabled -> keep UEFI state */
    assert(zizhan_outer_panel_wanted(1, 0, 0, 1) == 1); /* probe active -> keep on */
    assert(zizhan_outer_panel_wanted(1, 0, 0, 0) == 0); /* unfolded awake -> outer OFF */
    assert(zizhan_outer_panel_wanted(1, 0, 1, 0) == 0); /* unfolded asleep -> outer OFF */
    assert(zizhan_outer_panel_wanted(1, 1, 0, 0) == 1); /* folded awake -> outer ON */
    assert(zizhan_outer_panel_wanted(1, 1, 1, 0) == 0); /* folded deep sleep -> outer OFF */
    assert(zizhan_outer_panel_wanted_view(1, 1, 0, 0, 0) == 0); /* folded cover blank (active_view=0) -> outer OFF */
    assert(zizhan_outer_panel_wanted_view(1, 1, 1, 0, 0) == 1); /* folded cover clock/desktop -> outer ON */
    assert(zizhan_outer_panel_wanted_view(1, 1, 1, 1, 0) == 0); /* folded system sleep -> outer OFF */
    assert(zizhan_cover_autosleep_blocked(1, 1, 0) == 1);       /* cover clock window holds auto-sleep */
    assert(zizhan_cover_autosleep_blocked(1, 0, 0) == 0);       /* cover blank releases auto-sleep */
    assert(zizhan_cover_autosleep_blocked(1, 1, 1) == 0);       /* unattended benchmark never blocked */
    assert(zizhan_cover_autosleep_blocked(0, 1, 0) == 0);       /* unfolded never blocked */
    assert(zizhan_sleep_wake_cover_clock(ZizhanWakePower, 1) == 1);
    assert(zizhan_sleep_wake_cover_clock(ZizhanWakePower, 0) == 0);
    assert(zizhan_sleep_wake_cover_clock(ZizhanWakeLid, 1) == 0);

    /* CRITICAL-1 safety regression: waking from <1W deep sleep with physical power button held >= 3000 ms
     * MUST swallow the wake HID/cover edge while STILL setting key_down=1 so pong_button_reset fires! */
    {
        /* Case A: Held across sleep -> wake -> >=3s continuous hold */
        int swallow = 1, last = 0, down = 0;
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 1) == 0); /* debounce poll 1 */
        assert(down == 0 && swallow == 1);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 1) == 0); /* steady poll 2: swallowed! */
        assert(down == 1 && swallow == 1); /* key_down IS 1 -> 3000 ms pong_button_reset timer runs! */
        /* Release after short wake press: UP is also swallowed and clears swallow=0 */
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 0) == 0);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 0) == 0);
        assert(down == 0 && swallow == 0);
        /* Subsequent awake short press dispatches normally (returns 1 on DOWN and 1 on UP) */
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 1) == 0);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 1) == 1 && down == 1);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 0) == 0);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 0) == 1 && down == 0);

        /* Case B: Sub-16ms tap during wake where button is already released before keys_thread's first poll */
        swallow = 1; last = 0; down = 0;
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 0) == 0);
        assert(swallow == 0 && down == 0);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 1) == 0);
        assert(zizhan_sleep_power_key_step(&swallow, &last, &down, 1) == 1 && down == 1);
    }

    uint32_t w[2] = {0, 0};
    zizhan_dsi_dcs_short0_words(0x28u, w);
    assert(w[0] == 0x80050028u && w[1] == 0u);
    zizhan_dsi_dcs_short0_words(0x10u, w);
    assert(w[0] == 0x80050010u && w[1] == 0u);
    zizhan_dsi_dcs_dbv_words(0u, 1, w);
    assert(w[0] == 0xc0390003u && w[1] == 0xff000051u);
    zizhan_dsi_dcs_dbv_words(0x0133u, 1, w);
    assert(w[0] == 0xc0390003u && w[1] == 0xff330151u);
}

int main(void)
{
    test_armcpu_sleep();
    test_zizhan_sleep_plan();
    puts("PASS zizhan-sleep");
    return 0;
}
