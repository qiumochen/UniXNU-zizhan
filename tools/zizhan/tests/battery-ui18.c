/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "zizhan-battery-ui18.h"

int main(void)
{
    /* 1. Empty / missing telemetry must never invent numbers. */
    ZizhanBatteryHwTelemetry empty = {0};
    char buf[128];
    assert(zizhan_battery_has_hw_health(NULL) == 0);
    assert(zizhan_battery_has_hw_health(&empty) == 0);
    assert(zizhan_battery_format_capacity(&empty, buf, sizeof(buf)) == 0);
    assert(zizhan_battery_format_cycles(&empty, buf, sizeof(buf)) == 0);
    assert(zizhan_battery_format_soh_live(&empty, buf, sizeof(buf)) == 0);

    /* 2. Real Mix Fold 2 BM4V ADSP snapshot: 3502 / 4500 mAh (78%), 916 cycles, SOH 80%, 4444 mV, 34.40 C. */
    ZizhanBatteryHwTelemetry hw = {
        .nominal_mah = 3502,
        .design_mah = 4500,
        .max_percent = 77,
        .cycle_count = 916,
        .soh_percent = 80,
        .voltage_mv = 4444,
        .current_ma = 426,
        .temp_centi_c = 3440,
        .has_temp = 1,
        .has_current = 1,
    };
    assert(zizhan_battery_has_hw_health(&hw) == 1);
    assert(zizhan_battery_format_capacity(&hw, buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "78% (3502 / 4500 mAh)") == 0);

    assert(zizhan_battery_format_cycles(&hw, buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "916 \xe6\xac\xa1") == 0);

    assert(zizhan_battery_format_soh_live(&hw, buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "SOH 80% \xc2\xb7 4444 mV \xc2\xb7 34.4 \xc2\xb0" "C") == 0);

    /* 3. Negative sub-zero temperature rounding (-5.67 C -> -5.7 C) and percent-only fallback. */
    ZizhanBatteryHwTelemetry cold = {
        .max_percent = 81,
        .temp_centi_c = -567,
        .has_temp = 1,
    };
    assert(zizhan_battery_has_hw_health(&cold) == 1);
    assert(zizhan_battery_format_capacity(&cold, buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "81%") == 0);
    assert(zizhan_battery_format_soh_live(&cold, buf, sizeof(buf)) == 1);
    assert(strcmp(buf, "-5.7 \xc2\xb0" "C") == 0);

    /* 4. Small buffer bounds check + two-switch clean filter IDs. */
    assert(zizhan_battery_format_capacity(&hw, buf, 8) == 0);
    assert(zizhan_battery_is_kept_switch_id("BATTERY_PERCENTAGE_IDENTIFIER") == 1);
    assert(zizhan_battery_is_kept_switch_id("LOW_POWER_MODE_IDENTIFIER") == 1);
    assert(zizhan_battery_is_kept_switch_id("BATTERY_HEALTH_TITLE") == 0);
    assert(zizhan_battery_is_kept_switch_id("PLBatteryUIGraphLast24Hours") == 0);
    assert(zizhan_battery_is_kept_switch_id("") == 0);
    assert(zizhan_battery_is_kept_switch_id(NULL) == 0);

    printf("PASS battery-ui18\n");
    return 0;
}
