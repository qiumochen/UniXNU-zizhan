/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_VOLUME_INJECT_PLAN_H
#define ZIZHAN_VOLUME_INJECT_PLAN_H
#include <stdint.h>

/* IO-01 physical volume-key injection schedule (default off).
 * Mode 1: 3x Vol+ then 2x Vol- (net +1 step), starting delay_s (default 24 s)
 * after the first zero-copy desktop frame so SpringBoard's volume18 bridge is
 * ready and pong_shot=23..28 can capture the native iPadOS Volume HUD.
 * Mode 2: 4x Vol+ only. */
enum {
 ZizhanVolInjectOff = 0,
 ZizhanVolInjectUpDown = 1,
 ZizhanVolInjectUpOnly = 2,
 ZizhanVolInjectSkipMode = 3
};

enum {
 ZizhanVolDelayDefaultS = 24u,
 ZizhanVolDelayMinS = 5u,
 ZizhanVolDelayMaxS = 120u,
 ZizhanVolHoldMs = 80u,
 ZizhanVolClickGapMs = 600u,
 ZizhanVolDirGapMs = 1500u
};

enum {
 ZizhanVolKeyUp = 1u,
 ZizhanVolKeyDown = 2u
};

static inline int zizhan_vol_inject_mode(uint32_t inject) {
 if (!inject) { return ZizhanVolInjectOff; }
 if (inject == 1u) { return ZizhanVolInjectUpDown; }
 if (inject == 2u) { return ZizhanVolInjectUpOnly; }
 return ZizhanVolInjectSkipMode;
}

static inline uint32_t zizhan_vol_delay_s(uint32_t requested) {
 if (!requested) { return ZizhanVolDelayDefaultS; }
 if (requested < ZizhanVolDelayMinS) { return ZizhanVolDelayMinS; }
 if (requested > ZizhanVolDelayMaxS) { return ZizhanVolDelayMaxS; }
 return requested;
}

static inline unsigned zizhan_vol_up_clicks(uint32_t inject) {
 if (inject == 1u) { return 3u; }
 if (inject == 2u) { return 4u; }
 return 0u;
}

static inline unsigned zizhan_vol_down_clicks(uint32_t inject) {
 if (inject == 1u) { return 2u; }
 return 0u;
}

static inline unsigned zizhan_vol_total_clicks(uint32_t inject) {
 return zizhan_vol_up_clicks(inject) + zizhan_vol_down_clicks(inject);
}

static inline uint32_t zizhan_vol_click_key(uint32_t inject, unsigned index) {
 const unsigned up = zizhan_vol_up_clicks(inject);
 if (index < up) { return ZizhanVolKeyUp; }
 if (index < up + zizhan_vol_down_clicks(inject)) { return ZizhanVolKeyDown; }
 return 0u;
}

static inline uint32_t zizhan_vol_after_gap_ms(uint32_t inject, unsigned index) {
 const unsigned up = zizhan_vol_up_clicks(inject);
 const unsigned total = zizhan_vol_total_clicks(inject);
 if (index + 1u >= total) { return 0u; }
 if (index + 1u == up) { return ZizhanVolDirGapMs; }
 return ZizhanVolClickGapMs;
}

#if defined(__cplusplus)
static_assert(ZizhanVolHoldMs <= 200u, "volume click must stay below repeat hold threshold");
#else
_Static_assert(ZizhanVolHoldMs <= 200u, "volume click must stay below repeat hold threshold");
#endif

#endif
