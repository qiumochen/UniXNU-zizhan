/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_BATTERY_UI18_H
#define ZIZHAN_BATTERY_UI18_H

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* User-confirmed clean Battery pane policy: keep ONLY the two functional switches
 * (Battery Percentage + Low Power Mode) and drop spinning usage charts / unavailable health rows. */
static inline int zizhan_battery_is_kept_switch_id(const char *ident)
{
    if (!ident) {
        return 0;
    }
    return strcmp(ident, "BATTERY_PERCENTAGE_IDENTIFIER") == 0 ||
           strcmp(ident, "LOW_POWER_MODE_IDENTIFIER") == 0;
}

/* Pure-C formatting of genuine Qualcomm ADSP battmgr telemetry read via IOPS
 * ("InternalBattery"). Never fabricates Apple Battery Service State, Battery
 * Service Flags, UPOCount, WRa, or ageAwareMitigationState. */
typedef struct {
    int nominal_mah;
    int design_mah;
    int max_percent;
    int cycle_count;
    int soh_percent;
    int voltage_mv;
    int current_ma;
    int temp_centi_c;
    int has_temp;
    int has_current;
} ZizhanBatteryHwTelemetry;

static inline int zizhan_battery_has_hw_health(const ZizhanBatteryHwTelemetry *t)
{
    if (!t) {
        return 0;
    }
    if (t->nominal_mah > 0 && t->design_mah > 0) {
        return 1;
    }
    if (t->max_percent > 0 && t->max_percent <= 150) {
        return 1;
    }
    if (t->cycle_count > 0 || (t->soh_percent > 0 && t->soh_percent <= 150)) {
        return 1;
    }
    return 0;
}

static inline int zizhan_battery_format_capacity(const ZizhanBatteryHwTelemetry *t,
                                                 char *out, size_t cap)
{
    if (!t || !out || cap < 16) {
        return 0;
    }
    out[0] = '\0';
    if (t->nominal_mah > 0 && t->design_mah > 0) {
        int pct = (int)(((long long)t->nominal_mah * 100LL + t->design_mah / 2) / t->design_mah);
        if (pct < 1) {
            pct = 1;
        } else if (pct > 150) {
            pct = 150;
        }
        int n = snprintf(out, cap, "%d%% (%d / %d mAh)", pct, t->nominal_mah, t->design_mah);
        return (n > 0 && (size_t)n < cap) ? 1 : 0;
    }
    if (t->max_percent > 0 && t->max_percent <= 150) {
        int n = snprintf(out, cap, "%d%%", t->max_percent);
        return (n > 0 && (size_t)n < cap) ? 1 : 0;
    }
    return 0;
}

static inline int zizhan_battery_format_cycles(const ZizhanBatteryHwTelemetry *t,
                                               char *out, size_t cap)
{
    if (!t || !out || cap < 12 || t->cycle_count <= 0) {
        return 0;
    }
    /* "\xe6\xac\xa1" = "次" */
    int n = snprintf(out, cap, "%d \xe6\xac\xa1", t->cycle_count);
    return (n > 0 && (size_t)n < cap) ? 1 : 0;
}

static inline int zizhan_battery_format_soh_live(const ZizhanBatteryHwTelemetry *t,
                                                 char *out, size_t cap)
{
    if (!t || !out || cap < 24) {
        return 0;
    }
    out[0] = '\0';
    size_t k = 0;
    if (t->soh_percent > 0 && t->soh_percent <= 150) {
        int n = snprintf(out + k, cap - k, "SOH %d%%", t->soh_percent);
        if (n <= 0 || (size_t)n >= cap - k) {
            return 0;
        }
        k += (size_t)n;
    }
    if (t->voltage_mv > 0) {
        int n = snprintf(out + k, cap - k, "%s%d mV", k ? " \xc2\xb7 " : "", t->voltage_mv);
        if (n <= 0 || (size_t)n >= cap - k) {
            return 0;
        }
        k += (size_t)n;
    }
    if (t->has_temp && t->temp_centi_c >= -4000 && t->temp_centi_c <= 9000) {
        int neg = t->temp_centi_c < 0;
        int abs_deci = ((neg ? -t->temp_centi_c : t->temp_centi_c) + 5) / 10;
        int n = snprintf(out + k, cap - k, "%s%s%d.%d \xc2\xb0" "C",
                         k ? " \xc2\xb7 " : "", neg ? "-" : "", abs_deci / 10, abs_deci % 10);
        if (n <= 0 || (size_t)n >= cap - k) {
            return 0;
        }
        k += (size_t)n;
    }
    return k > 0 ? 1 : 0;
}

#endif
