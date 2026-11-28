/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_VOLUME_INJECT_PLAN_H
#define ZIZHAN_VOLUME_INJECT_PLAN_H
#include <stdint.h>

/* IO-01 physical volume-key injection schedule (default off).
 * Mode 1: 3x Vol+ then 2x Vol- (net +1 step), starting delay_s (default 24 s)
 * after the first zero-copy desktop frame so SpringBoard's volume18 bridge is
 * ready and pong_shot=23..28 can capture the native iPadOS Volume HUD.
 * Mode 2: 4x Vol+ only.
 * Mode 3: exercises pong_buttons single-step click pacer (2x 192 ms human-duration
 * Vol- clicks -> 1 step each, then 1x 560 ms Vol+ hold -> 3 steps: press + 2 repeats). */
enum {
 ZizhanVolInjectOff = 0,
 ZizhanVolInjectUpDown = 1,
 ZizhanVolInjectUpOnly = 2,
 ZizhanVolInjectPaced = 3,
 ZizhanVolInjectSkipMode = 4
};

enum {
 ZizhanVolDelayDefaultS = 24u,
 ZizhanVolDelayMinS = 5u,
 ZizhanVolDelayMaxS = 120u,
 ZizhanVolHoldMs = 80u,
 ZizhanVolHumanClickMs = 192u,
 ZizhanVolLongHoldMs = 560u,
 ZizhanVolPaceDelayMs = 400u,
 ZizhanVolPaceRepeatMs = 112u,
 ZizhanVolClickGapMs = 600u,
 ZizhanVolDirGapMs = 1500u
};

typedef struct {
 uint32_t held_ms;
 uint8_t down;
} ZizhanVolPace;

/* Returns 1 when an atomic DOWN+UP volume step should be emitted, 0 otherwise.
 * Short presses (<400 ms, including 150-250 ms human clicks) emit strictly 1 step
 * on press; holding >=400 ms emits 1 repeat step at 400 ms and every 112 ms after. */
static inline unsigned zizhan_vol_pace_step(ZizhanVolPace *p, int down, uint32_t dt_ms) {
 if (!p) { return 0u; }
 if (!down) {
  p->down = 0u;
  p->held_ms = 0u;
  return 0u;
 }
 if (!p->down) {
  p->down = 1u;
  p->held_ms = 0u;
  return 1u;
 }
 const uint32_t prev = p->held_ms;
 const uint32_t next = (dt_ms > 0xffffu - prev) ? 0xffffu : (prev + dt_ms);
 p->held_ms = next;
 if (next < ZizhanVolPaceDelayMs) { return 0u; }
 if (prev < ZizhanVolPaceDelayMs) { return 1u; }
 const uint32_t prev_slot = (prev - ZizhanVolPaceDelayMs) / ZizhanVolPaceRepeatMs;
 const uint32_t next_slot = (next - ZizhanVolPaceDelayMs) / ZizhanVolPaceRepeatMs;
 return next_slot > prev_slot ? 1u : 0u;
}

enum {
 ZizhanVolKeyUp = 1u,
 ZizhanVolKeyDown = 2u
};

static inline int zizhan_vol_inject_mode(uint32_t inject) {
 if (!inject) { return ZizhanVolInjectOff; }
 if (inject == 1u) { return ZizhanVolInjectUpDown; }
 if (inject == 2u) { return ZizhanVolInjectUpOnly; }
 if (inject == 3u) { return ZizhanVolInjectPaced; }
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
