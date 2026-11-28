/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdio.h>
#include "zizhan-cllocation18.h"

int main(void) {
    ZizhanGnssSnapshot snap;
    /* 1. Round-trip pack/unpack with signed negative coordinates & altitude. */
    uint64_t sv = zizhan_gnss_pack_sv(1u, 2u, 14u, 8u, 423u, 77u);
    uint64_t ll = zizhan_gnss_pack_latlon(-338688197, 1512092955);
    uint64_t aa = zizhan_gnss_pack_accalt(38, -125);
    uint64_t vl = zizhan_gnss_pack_vel(142, 2745, 7u);
    assert(zizhan_gnss_unpack(sv, ll, aa, vl, &snap) == 1);
    assert(snap.engine_state == 1u && snap.fix_status == 2u);
    assert(snap.sv_visible == 14u && snap.sv_tracking == 8u && snap.sv_used == 7u);
    assert(snap.max_cn0_tenths == 423u && snap.seq == 77u);
    assert(snap.lat_e7 == -338688197 && snap.lon_e7 == 1512092955);
    assert(snap.hor_unc_dm == 38 && snap.alt_dm == -125);
    assert(snap.speed_cms == 142 && snap.heading_ddeg == 2745);

    /* 2. Unknown heading (< 0) round-trips as -1. */
    vl = zizhan_gnss_pack_vel(0, -1, 4u);
    assert(zizhan_gnss_unpack(sv, ll, aa, vl, &snap) == 1);
    assert(snap.heading_ddeg == -1 && snap.speed_cms == 0);

    /* 3. No fix (fix_status == 0), (0, 0), or out-of-range coordinates return 0 so native locationd is preserved. */
    assert(zizhan_gnss_unpack(zizhan_gnss_pack_sv(1u, 0u, 9u, 2u, 310u, 1u), ll, aa, vl, &snap) == 0);
    assert(snap.sv_visible == 9u && snap.sv_tracking == 2u);
    assert(zizhan_gnss_unpack(sv, zizhan_gnss_pack_latlon(0, 0), aa, vl, &snap) == 0);
    assert(zizhan_gnss_unpack(sv, zizhan_gnss_pack_latlon(950000000, 1200000000), aa, vl, &snap) == 0);

    printf("PASS cllocation18\n");
    return 0;
}
