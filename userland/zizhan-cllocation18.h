/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-C 64-bit notifyd RAM state packing/unpacking for Zizhan GNSS -> CoreLocation bridge.
 * All coordinates remain in RAM only (never written to disk or diagnostic logs). */
#ifndef ZIZHAN_CLLOCATION18_H
#define ZIZHAN_CLLOCATION18_H

#include <stdint.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ZIZHAN_GNSS_NOTIFY_SV      "com.zizhan.gnss.sv"
#define ZIZHAN_GNSS_NOTIFY_LATLON  "com.zizhan.gnss.latlon"
#define ZIZHAN_GNSS_NOTIFY_ACCALT  "com.zizhan.gnss.accalt"
#define ZIZHAN_GNSS_NOTIFY_VEL     "com.zizhan.gnss.vel"
#define ZIZHAN_GNSS_NOTIFY_UPDATED "com.zizhan.gnss.updated"

typedef struct {
    uint8_t engine_state;     /* 0 unknown, 1 ON, 2 OFF */
    uint8_t fix_status;       /* 0 none, 1 intermediate, 2 valid final fix */
    uint8_t sv_visible;
    uint8_t sv_tracking;
    uint8_t sv_used;
    uint16_t max_cn0_tenths;  /* 0.1 dB-Hz */
    uint16_t seq;
    int32_t lat_e7;           /* [-900000000 .. +900000000] */
    int32_t lon_e7;           /* [-1800000000 .. +1800000000] */
    int32_t hor_unc_dm;       /* 0.1 m */
    int32_t alt_dm;           /* 0.1 m */
    int32_t speed_cms;        /* cm/s */
    int32_t heading_ddeg;     /* 0.1 deg, < 0 unknown */
} ZizhanGnssSnapshot;

static inline uint64_t zizhan_gnss_pack_sv(uint8_t engine, uint8_t fix, uint8_t vis, uint8_t trk,
                                           uint16_t cn0_tenths, uint16_t seq) {
    return (uint64_t)engine |
           ((uint64_t)fix << 8) |
           ((uint64_t)vis << 16) |
           ((uint64_t)trk << 24) |
           ((uint64_t)cn0_tenths << 32) |
           ((uint64_t)seq << 48);
}

static inline uint64_t zizhan_gnss_pack_latlon(int32_t lat_e7, int32_t lon_e7) {
    return ((uint64_t)(uint32_t)lat_e7 << 32) | (uint64_t)(uint32_t)lon_e7;
}

static inline uint64_t zizhan_gnss_pack_accalt(int32_t hor_unc_dm, int32_t alt_dm) {
    return ((uint64_t)(uint32_t)hor_unc_dm << 32) | (uint64_t)(uint32_t)alt_dm;
}

static inline uint64_t zizhan_gnss_pack_vel(int32_t speed_cms, int32_t heading_ddeg, uint8_t sv_used) {
    const uint32_t spd = speed_cms > 0 ? (speed_cms > 65535 ? 65535u : (uint32_t)speed_cms) : 0u;
    const uint32_t hdg = (heading_ddeg >= 0 && heading_ddeg <= 3600) ? (uint32_t)(heading_ddeg + 1) : 0u;
    return ((uint64_t)spd << 32) | ((uint64_t)hdg << 16) | (uint64_t)sv_used;
}

static inline int zizhan_gnss_unpack(uint64_t sv, uint64_t latlon, uint64_t accalt, uint64_t vel,
                                     ZizhanGnssSnapshot *out) {
    if (!out) return 0;
    memset(out, 0, sizeof(*out));
    out->engine_state = (uint8_t)(sv & 0xffu);
    out->fix_status = (uint8_t)((sv >> 8) & 0xffu);
    out->sv_visible = (uint8_t)((sv >> 16) & 0xffu);
    out->sv_tracking = (uint8_t)((sv >> 24) & 0xffu);
    out->max_cn0_tenths = (uint16_t)((sv >> 32) & 0xffffu);
    out->seq = (uint16_t)((sv >> 48) & 0xffffu);
    out->lat_e7 = (int32_t)(uint32_t)(latlon >> 32);
    out->lon_e7 = (int32_t)(uint32_t)(latlon & 0xffffffffull);
    out->hor_unc_dm = (int32_t)(uint32_t)(accalt >> 32);
    out->alt_dm = (int32_t)(uint32_t)(accalt & 0xffffffffull);
    out->speed_cms = (int32_t)((vel >> 32) & 0xffffu);
    const uint32_t hdg = (uint32_t)((vel >> 16) & 0xffffu);
    out->heading_ddeg = hdg > 0u ? (int32_t)(hdg - 1u) : -1;
    out->sv_used = (uint8_t)(vel & 0xffu);
    if (out->fix_status == 0u) return 0;
    if (out->lat_e7 < -900000000 || out->lat_e7 > 900000000) return 0;
    if (out->lon_e7 < -1800000000 || out->lon_e7 > 1800000000) return 0;
    if (out->lat_e7 == 0 && out->lon_e7 == 0) return 0;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* ZIZHAN_CLLOCATION18_H */
