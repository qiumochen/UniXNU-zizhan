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

inline uint32_t isqrt64(uint64_t x) {
  if (x == 0ull) {
    return 0u;
  }
  uint64_t op = x, res = 0ull, one = 1ull << 62;
  while (one > op) {
    one >>= 2;
  }
  while (one != 0ull) {
    if (op >= res + one) {
      op -= res + one;
      res = (res >> 1) + one;
    } else {
      res >>= 1;
    }
    one >>= 2;
  }
  return uint32_t(res);
}

/* 3-axis magnetic field magnitude in milli-uT from milli-uT components. */
inline uint32_t mag_norm_milli_ut(int32_t mx_m, int32_t my_m, int32_t mz_m) {
  const int64_t x = mx_m, y = my_m, z = mz_m;
  const uint64_t sum2 = uint64_t(x * x) + uint64_t(y * y) + uint64_t(z * z);
  return isqrt64(sum2);
}

inline bool mag_plausible(int32_t mx_m, int32_t my_m, int32_t mz_m) {
  const uint32_t norm = mag_norm_milli_ut(mx_m, my_m, mz_m);
  return norm >= 5000u && norm <= 2000000u;  /* 5 .. 2000 uT (includes foldable clasp magnet bias) */
}

/* Convert SSC pressure reading (milli-hPa ~1,013,250 or milli-Pa ~101,325,000) to Pascals (Pa). */
inline uint32_t pressure_to_pa(int32_t raw_milli) {
  if (raw_milli <= 0) {
    return 0u;
  }
  const uint64_t v = uint64_t(uint32_t(raw_milli));
  if (v >= 300000ull && v <= 1250000ull) {
    return uint32_t((v + 5ull) / 10ull);       /* hPa * 1000 -> Pa (hPa * 100) */
  }
  if (v >= 30000000ull && v <= 125000000ull) {
    return uint32_t((v + 500ull) / 1000ull);   /* Pa * 1000 -> Pa */
  }
  return 0u;
}

inline bool pressure_plausible_pa(uint32_t pa) {
  return pa >= 30000u && pa <= 125000u;        /* 300 .. 1250 hPa */
}

/* Continuous hinge angle posture classification from milli-degrees (0..180000).
 * Returns: 0 = folded (< 30 deg), 1 = half-open/tent (30..150 deg), 2 = flat/open (> 150 deg). */
inline int32_t hinge_posture(int32_t deg_milli) {
  if (deg_milli < 30000) {
    return 0;
  }
  if (deg_milli <= 150000) {
    return 1;
  }
  return 2;
}

inline bool hinge_angle_plausible(int32_t deg_milli) {
  return deg_milli >= -5000 && deg_milli <= 190000;
}

/* Decode SSC proximity near/far state:
 * - Native sns_proximity_event (msg_id 769 or 1031): varint event_type 1 = NEAR, 0 = FAR.
 * - Fallback sns_std_sensor_event (msg_id 1025/1026): distance_cm * 1000 < 3000 (< 3 cm) = NEAR. */
inline bool proximity_is_near(uint32_t msg_id, uint32_t varint_event_type, int32_t distance_milli) {
  if (msg_id == 769u || msg_id == 1031u) {
    return varint_event_type == 1u;
  }
  if (msg_id == 1025u || msg_id == 1026u) {
    return distance_milli >= 0 && distance_milli < 3000;
  }
  return false;
}

/* Keep on-change proximity active while folded (lid_closed != 0) even when the display is off
 * so pocket guard blocks accidental double-taps in Cover Clock standby; pause when unfolded & screen off. */
inline bool proximity_keep_on_in_standby(int lid_closed) {
  return lid_closed != 0;
}

/* ALS-linked SWAR pixel brightness scale (Q8: 256 = 100%, 144 = 56%) for the 6.56" cover OLED,
 * avoiding unowned DSI1 DCS writes while softening dark-room glare. */
inline uint32_t outer_cover_scale_q8(int32_t lux_milli) {
  if (lux_milli < 0 || lux_milli >= 30000) {
    return 256u;   /* unknown/off or >= 30 lux indoor/daylight: bit-exact 100% fast path */
  }
  if (lux_milli >= 5000) {
    return 208u;   /* 5..30 lux dim indoor: ~81% */
  }
  return 144u;     /* < 5 lux dark room: ~56% night comfort */
}

}  // namespace zizhan_slpi_sensors
