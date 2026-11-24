/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-C++17 helpers for SENS-03/04: soft-float IEEE-754 milli-unit decoding,
 * hysteresis-guarded ambient-light auto-brightness curve (L18 primary OLED DBV 4..2047),
 * and screen-off SLPI sensor stream quiesce state machine. No kernel dependencies. */
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace zizhan_slpi_sensors {

/* value * 1000 from raw IEEE-754 binary32 bits using integer math only. */
inline int32_t milli_from_f32(uint32_t bits) {
  const uint32_t exp_raw = (bits >> 23) & 0xffu;
  if (exp_raw == 0u || exp_raw == 0xffu) {
    return 0;
  }
  const int32_t e = int32_t(exp_raw) - 127;
  if (e < -20) {
    return 0;
  }
  if (e > 20) {
    return (bits >> 31) ? -2000000000 : 2000000000;
  }
  const uint64_t m = uint64_t(bits & 0x7fffffu) | 0x800000ull;
  const uint64_t scaled = m * 1000ull;
  const uint64_t v = (e >= 23) ? (scaled << (e - 23)) : (scaled >> (23 - e));
  return (bits >> 31) ? -int32_t(v) : int32_t(v);
}

/* Piecewise perceptual curve mapping millilux (0 .. 20,000,000+) to L18 DBV (4 .. 2047). */
inline uint32_t lux_to_dbv(int32_t lux_milli) {
  if (lux_milli <= 0) {
    return 56u;
  }
  const uint64_t ml = uint64_t(uint32_t(lux_milli));
  struct Knot { uint64_t ml; uint32_t dbv; };
  static const Knot kCurve[] = {
      {0ull, 56u},
      {2000ull, 120u},        /* 2 lux: dark room */
      {30000ull, 340u},       /* 30 lux: dim indoor */
      {200000ull, 700u},      /* 200 lux: normal indoor */
      {1000000ull, 1180u},    /* 1000 lux: bright office */
      {10000000ull, 2047u},   /* 10000+ lux: daylight */
  };
  constexpr unsigned kN = sizeof(kCurve) / sizeof(kCurve[0]);
  if (ml >= kCurve[kN - 1u].ml) {
    return kCurve[kN - 1u].dbv;
  }
  for (unsigned i = 0; i + 1u < kN; ++i) {
    if (ml >= kCurve[i].ml && ml <= kCurve[i + 1u].ml) {
      const uint64_t span = kCurve[i + 1u].ml - kCurve[i].ml;
      const uint64_t pos = ml - kCurve[i].ml;
      const uint64_t d_dbv = kCurve[i + 1u].dbv - kCurve[i].dbv;
      return kCurve[i].dbv + uint32_t((pos * d_dbv + span / 2u) / span);
    }
  }
  return 700u;
}

struct AutoBrightness {
  bool seeded = false;
  bool user_override = false;
  uint32_t last_set_bl = 0;
  int32_t anchor_lux_milli = 0;
  uint32_t candidate_bl = 0;
  uint64_t candidate_since_ms = 0;
  uint32_t updates = 0;
};

inline uint32_t abs_diff_u32(uint32_t a, uint32_t b) {
  return a >= b ? (a - b) : (b - a);
}

inline bool lux_shifted_pct(int32_t old_ml, int32_t new_ml, uint32_t pct) {
  const uint64_t a = old_ml > 0 ? uint64_t(uint32_t(old_ml)) : 1000ull;
  const uint64_t b = new_ml > 0 ? uint64_t(uint32_t(new_ml)) : 0ull;
  const uint64_t diff = a >= b ? (a - b) : (b - a);
  return diff * 100ull >= a * uint64_t(pct);
}

inline bool step_autobright(AutoBrightness &ab, int32_t lux_milli, uint32_t current_bl,
                            uint64_t now_ms, uint32_t *target_out) {
  if (current_bl == 0u) {
    return false;
  }
  if (ab.seeded && ab.last_set_bl != 0u && abs_diff_u32(current_bl, ab.last_set_bl) > 48u) {
    ab.user_override = true;
    ab.last_set_bl = current_bl;
    ab.anchor_lux_milli = lux_milli;
  }
  if (ab.user_override) {
    if (!lux_shifted_pct(ab.anchor_lux_milli, lux_milli, 300u)) {
      return false;
    }
    ab.user_override = false;
  }
  const uint32_t want = lux_to_dbv(lux_milli);
  if (!ab.seeded) {
    ab.seeded = true;
    ab.last_set_bl = want;
    ab.anchor_lux_milli = lux_milli;
    ab.candidate_bl = 0;
    ++ab.updates;
    if (target_out) {
      *target_out = want;
    }
    return abs_diff_u32(want, current_bl) >= 24u;
  }
  if (abs_diff_u32(want, ab.last_set_bl) < 36u || !lux_shifted_pct(ab.anchor_lux_milli, lux_milli, 25u)) {
    ab.candidate_bl = 0;
    ab.candidate_since_ms = 0;
    return false;
  }
  if (ab.candidate_bl == 0u || abs_diff_u32(want, ab.candidate_bl) > 64u) {
    ab.candidate_bl = want;
    ab.candidate_since_ms = now_ms;
    return false;
  }
  if (now_ms - ab.candidate_since_ms < 2500ull) {
    return false;
  }
  ab.last_set_bl = want;
  ab.anchor_lux_milli = lux_milli;
  ab.candidate_bl = 0;
  ab.candidate_since_ms = 0;
  ++ab.updates;
  if (target_out) {
    *target_out = want;
  }
  return true;
}

enum class QuiesceAction : uint8_t {
  None = 0,
  Pause,
  Resume,
};

struct QuiesceState {
  bool quiesced = false;
  bool pending_off = false;
  uint64_t off_since_ms = 0;
  uint32_t pauses = 0;
  uint32_t resumes = 0;
};

inline QuiesceAction step_quiesce(QuiesceState &q, bool enabled, int display_state, uint64_t now_ms) {
  if (!enabled) {
    if (q.quiesced) {
      q.quiesced = false;
      q.pending_off = false;
      ++q.resumes;
      return QuiesceAction::Resume;
    }
    return QuiesceAction::None;
  }
  if (display_state == 0) {
    if (q.quiesced) {
      return QuiesceAction::None;
    }
    if (!q.pending_off) {
      q.pending_off = true;
      q.off_since_ms = now_ms;
      return QuiesceAction::None;
    }
    if (now_ms - q.off_since_ms >= 500ull) {
      q.pending_off = false;
      q.quiesced = true;
      ++q.pauses;
      return QuiesceAction::Pause;
    }
    return QuiesceAction::None;
  }
  q.pending_off = false;
  if (display_state == 1 && q.quiesced) {
    q.quiesced = false;
    ++q.resumes;
    return QuiesceAction::Resume;
  }
  return QuiesceAction::None;
}

inline uint32_t poll_sleep_ms(const QuiesceState &q, bool fast_bringup) {
  if (fast_bringup) {
    return 1u;
  }
  return q.quiesced ? 250u : 10u;
}

}  // namespace zizhan_slpi_sensors
