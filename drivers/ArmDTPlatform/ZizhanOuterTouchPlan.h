/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Pure-C++17 outer touch (ST FTM5 SALIXP 0x3640 on QUP2 SE19 SPI) event decoder,
 * multi-finger viewport mapper, UTC+8 calendar helper, and 2:1 SWAR BGRA downscaler.
 * Kept free of kernel headers so tools/zizhan/tests/outer-touch.cpp tests it directly. */
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace zizhan_outer_touch {

enum class Kind : uint8_t {
  Empty = 0,
  ControllerReady,
  PointerEnter,
  PointerMotion,
  PointerLeave,
  Status,
  UserReport,
  Error,
  Other,
};

enum class MapMode : uint8_t {
  ClockWakeOnly = 1,
  DesktopViewport = 2,
  DesktopFull1to1 = 3,
};

enum class BarAction : uint8_t {
  None = 0,
  VolDown = 1,
  Home = 2,
  ClockLock = 3,
  VolUp = 4,
  ZoomToggle = 5,
};

enum : uint32_t {
  kMaxFingers = 10,
  kPanelW = 1080,
  kPanelH = 2520,
  kInnerW = 2160,
  kInnerH = 1916,
  kDesktopW = 1080,
  kDesktopH = 958,
  kDesktopY0 = 781,
  kFullY0 = 220,
  kFullH = 1916,
  kFullDefaultCropX = 540,
  kEdgeBezel = 96,
  kBarY0 = 1872,
  kBarY1 = 1992,
  kZoomPillY0 = 2024,
  kZoomPillY1 = 2124,
  kZoomPillX0 = 340,
  kZoomPillX1 = 740,
  kFullBarY0 = 2180,
  kFullBarY1 = 2300,
  kVolDownX0 = 48,
  kVolDownX1 = 264,
  kHomePillX0 = 276,
  kHomePillX1 = 504,
  kLockPillX0 = 516,
  kLockPillX1 = 764,
  kLockPillY0 = 1872,
  kLockPillY1 = 1992,
  kVolUpX0 = 776,
  kVolUpX1 = 1032,
  kDoubleTapMinMs = 40,
  kDoubleTapMaxMs = 500,
  kTapHoldMaxMs = 350,
  kSwipeMaxMs = 900,
  kNormMax = 65535,
};

struct Decoded {
  Kind kind;
  uint8_t finger;
  uint8_t touch_type;
  bool release_all;
  bool valid_contact;
  uint32_t raw_x;
  uint32_t raw_y;
  uint8_t left;
};

struct PostOp {
  uint8_t type;    /* 1 = PONG_TOUCH_DOWN, 2 = PONG_TOUCH_MOVE, 3 = PONG_TOUCH_UP */
  uint8_t finger;
  uint32_t x;
  uint32_t y;
};

struct StepResult {
  Kind kind;
  bool refresh_hud;
  bool rearm_scan;
  bool toggle_desktop;
  bool toggle_clock;
  bool pocket_blocked;
  BarAction bar_action;
  uint8_t count;
  PostOp ops[kMaxFingers];
};

struct Tracker {
  uint16_t down_mask = 0;
  uint16_t desktop_mask = 0;
  uint32_t last_x[kMaxFingers] = {};
  uint32_t last_y[kMaxFingers] = {};
  uint32_t max_x = 10800;
  uint32_t max_y = 25200;
  bool super_res = true;
  uint64_t last_tap_ms = 0;
  uint64_t tap_down_ms = 0;
  uint32_t last_tap_x = 0;
  uint32_t last_tap_y = 0;
  uint32_t tap_down_x = 0;
  uint32_t tap_down_y = 0;
  uint32_t cur_raw_x = 0;
  uint32_t cur_raw_y = 0;
  bool tap_candidate = false;
  bool swipe_candidate = false;
  bool pill_armed = false;
  BarAction armed_bar = BarAction::None;
  uint32_t events = 0;
  uint32_t posted = 0;
  uint32_t hud_taps = 0;
  uint32_t mode_toggles = 0;
  uint32_t pocket_blocks = 0;
  uint32_t bar_actions = 0;
  uint32_t swipe_wakes = 0;
  uint32_t swipe_homes = 0;
  uint32_t zoom_toggles = 0;
  uint32_t crop_x0 = kFullDefaultCropX;
};

inline bool contact_type_valid(uint8_t touch_type) {
  /* 1 = finger, 3 = stylus, 4 = glove; 2 = hover and 5 = palm/large are ignored. */
  return touch_type == 1u || touch_type == 3u || touch_type == 4u;
}

inline Decoded decode_event(const uint8_t ev[8], bool super_res, uint32_t max_x, uint32_t max_y) {
  Decoded d{};
  if (!ev || ev[0] == 0x00u) {
    d.kind = Kind::Empty;
    return d;
  }
  d.left = uint8_t(ev[7] & 0x1fu);
  if ((ev[0] & 0x0fu) != 0x03u) {
    d.kind = Kind::Other;
    return d;
  }
  switch (ev[0]) {
  case 0x03u:
    d.kind = Kind::ControllerReady;
    return d;
  case 0x43u:
    d.kind = Kind::Status;
    return d;
  case 0x83u:
    d.kind = Kind::UserReport;
    return d;
  case 0xf3u:
    d.kind = Kind::Error;
    return d;
  case 0x33u:
    d.kind = Kind::PointerLeave;
    if (ev[1] == 0xb5u) {
      d.release_all = true;
      return d;
    }
    d.finger = uint8_t((ev[1] >> 4) & 0x0fu);
    d.touch_type = uint8_t(ev[1] & 0x0fu);
    d.valid_contact = (d.finger < kMaxFingers) && contact_type_valid(d.touch_type);
    return d;
  case 0x13u:
  case 0x23u: {
    d.kind = (ev[0] == 0x13u) ? Kind::PointerEnter : Kind::PointerMotion;
    d.finger = uint8_t((ev[1] >> 4) & 0x0fu);
    d.touch_type = uint8_t(ev[1] & 0x0fu);
    d.valid_contact = (d.finger < kMaxFingers) && contact_type_valid(d.touch_type);
    uint32_t rx = 0, ry = 0;
    if (super_res) {
      rx = uint32_t(ev[2]) | (uint32_t(ev[3]) << 8);
      ry = uint32_t(ev[4]) | (uint32_t(ev[5]) << 8);
    } else {
      rx = uint32_t(ev[2]) | ((uint32_t(ev[3]) & 0x0fu) << 8);
      ry = (uint32_t(ev[3]) >> 4) | (uint32_t(ev[4]) << 4);
    }
    if (max_x && rx > max_x) {
      rx = max_x;
    }
    if (max_y && ry > max_y) {
      ry = max_y;
    }
    d.raw_x = rx;
    d.raw_y = ry;
    return d;
  }
  default:
    d.kind = Kind::Other;
    return d;
  }
}

inline void configure_sysinfo(Tracker &t, uint16_t scr_x_res, uint16_t scr_y_res) {
  if (scr_x_res >= 4096u && scr_y_res >= 4096u) {
    t.super_res = true;
    t.max_x = scr_x_res;
    t.max_y = scr_y_res;
  } else if (scr_x_res >= 720u && scr_x_res < 4096u && scr_y_res >= 1280u && scr_y_res < 4096u) {
    t.super_res = false;
    t.max_x = scr_x_res;
    t.max_y = scr_y_res;
  } else {
    t.super_res = true;
    t.max_x = 10800u;
    t.max_y = 25200u;
  }
}

inline bool map_mode_point(const Tracker &t, MapMode mode, uint32_t raw_x, uint32_t raw_y,
                           bool check_band, uint32_t *out_x, uint32_t *out_y) {
  const uint32_t rx_max = t.max_x ? t.max_x : 10800u;
  const uint32_t ry_max = t.max_y ? t.max_y : 25200u;
  const bool full = (mode == MapMode::DesktopFull1to1);
  const uint32_t dst_y0 = full ? kFullY0 : kDesktopY0;
  const uint32_t dst_h = full ? kFullH : kDesktopH;
  const uint32_t vp_y0 = uint32_t((uint64_t(dst_y0) * ry_max) / kPanelH);
  const uint32_t vp_h = uint32_t((uint64_t(dst_h) * ry_max) / kPanelH);
  const uint32_t bezel = uint32_t((uint64_t(full ? 36u : kEdgeBezel) * ry_max) / kPanelH);
  const uint32_t band_lo = vp_y0 > bezel ? (vp_y0 - bezel) : 0u;
  const uint32_t band_hi = vp_y0 + vp_h + bezel;
  if (check_band && (raw_y < band_lo || raw_y > band_hi)) {
    return false;
  }
  uint32_t cx = raw_x > rx_max ? rx_max : raw_x;
  uint32_t cy = raw_y;
  if (cy < vp_y0) {
    cy = vp_y0;
  } else if (cy > vp_y0 + vp_h) {
    cy = vp_y0 + vp_h;
  }
  cy -= vp_y0;
  uint32_t xn = 0;
  if (full) {
    const uint32_t crop = t.crop_x0 <= (kInnerW - kPanelW) ? t.crop_x0 : kFullDefaultCropX;
    const uint64_t inner_px_num = uint64_t(crop) * rx_max + uint64_t(cx) * kPanelW;
    const uint64_t inner_den = uint64_t(kInnerW) * rx_max;
    xn = inner_den ? uint32_t((inner_px_num * kNormMax + inner_den / 2u) / inner_den) : 0u;
  } else {
    xn = uint32_t((uint64_t(cx) * kNormMax + rx_max / 2u) / rx_max);
  }
  uint32_t yn = vp_h ? uint32_t((uint64_t(cy) * kNormMax + vp_h / 2u) / vp_h) : 0u;
  if (xn > kNormMax) {
    xn = kNormMax;
  }
  if (yn > kNormMax) {
    yn = kNormMax;
  }
  *out_x = xn;
  *out_y = yn;
  return true;
}

inline bool map_desktop_point(const Tracker &t, uint32_t raw_x, uint32_t raw_y,
                              bool check_band, uint32_t *out_x, uint32_t *out_y) {
  return map_mode_point(t, MapMode::DesktopViewport, raw_x, raw_y, check_band, out_x, out_y);
}

inline uint32_t abs_diff32(uint32_t a, uint32_t b) {
  return a >= b ? (a - b) : (b - a);
}

inline BarAction bar_hit_test_mode(const Tracker &t, MapMode mode, uint32_t raw_x, uint32_t raw_y) {
  const uint32_t rx_max = t.max_x ? t.max_x : 10800u;
  const uint32_t ry_max = t.max_y ? t.max_y : 25200u;
  const uint32_t px = uint32_t((uint64_t(raw_x) * kPanelW + rx_max / 2u) / rx_max);
  const uint32_t py = uint32_t((uint64_t(raw_y) * kPanelH + ry_max / 2u) / ry_max);
  if (mode == MapMode::DesktopFull1to1) {
    /* 5-pill bottom bar at y=2180..2300: VOL- (36..224), HOME (240..428), 2:1 ZOOM (444..636), CLOCK (652..840), VOL+ (856..1044) */
    if (py < kFullBarY0 || py > kFullBarY1) {
      return BarAction::None;
    }
    if (px >= 36u && px <= 224u) return BarAction::VolDown;
    if (px >= 240u && px <= 428u) return BarAction::Home;
    if (px >= 444u && px <= 636u) return BarAction::ZoomToggle;
    if (px >= 652u && px <= 840u) return BarAction::ClockLock;
    if (px >= 856u && px <= 1044u) return BarAction::VolUp;
    return BarAction::None;
  }
  if (py >= kZoomPillY0 && py <= kZoomPillY1 && px >= kZoomPillX0 && px <= kZoomPillX1) {
    return BarAction::ZoomToggle;
  }
  const uint32_t y0 = uint32_t((uint64_t(kBarY0) * ry_max) / kPanelH);
  const uint32_t y1 = uint32_t((uint64_t(kBarY1) * ry_max) / kPanelH);
  if (raw_y < y0 || raw_y > y1) {
    return BarAction::None;
  }
  if (px >= kVolDownX0 && px <= kVolDownX1) {
    return BarAction::VolDown;
  }
  if (px >= kHomePillX0 && px <= kHomePillX1) {
    return BarAction::Home;
  }
  if (px >= kLockPillX0 && px <= kLockPillX1) {
    return BarAction::ClockLock;
  }
  if (px >= kVolUpX0 && px <= kVolUpX1) {
    return BarAction::VolUp;
  }
  return BarAction::None;
}

inline BarAction bar_hit_test(const Tracker &t, uint32_t raw_x, uint32_t raw_y) {
  return bar_hit_test_mode(t, MapMode::DesktopViewport, raw_x, raw_y);
}

inline bool in_lock_pill(const Tracker &t, uint32_t raw_x, uint32_t raw_y) {
  return bar_hit_test(t, raw_x, raw_y) == BarAction::ClockLock;
}

inline void release_all(Tracker &t, StepResult &out) {
  for (uint8_t f = 0; f < kMaxFingers; ++f) {
    const uint16_t bit = uint16_t(1u << f);
    if ((t.desktop_mask & bit) != 0u && out.count < kMaxFingers) {
      out.ops[out.count++] = PostOp{3u, f, t.last_x[f], t.last_y[f]};
      ++t.posted;
    }
  }
  t.down_mask = 0;
  t.desktop_mask = 0;
  t.tap_candidate = false;
  t.swipe_candidate = false;
  t.pill_armed = false;
  t.armed_bar = BarAction::None;
}

inline StepResult step(Tracker &t, const uint8_t ev[8], MapMode mode,
                       uint64_t now_ms = 0, bool allow_toggle = true, bool pocket_near = false) {
  StepResult r{};
  const Decoded d = decode_event(ev, t.super_res, t.max_x, t.max_y);
  r.kind = d.kind;
  if (d.kind == Kind::Empty) {
    return r;
  }
  ++t.events;
  if (d.kind == Kind::ControllerReady) {
    release_all(t, r);
    r.rearm_scan = true;
    return r;
  }
  if (d.kind == Kind::PointerLeave) {
    if (d.release_all) {
      release_all(t, r);
      return r;
    }
    if (!d.valid_contact) {
      return r;
    }
    const uint16_t bit = uint16_t(1u << d.finger);
    const bool was_down = (t.down_mask & bit) != 0u;
    if ((t.desktop_mask & bit) != 0u) {
      r.ops[r.count++] = PostOp{3u, d.finger, t.last_x[d.finger], t.last_y[d.finger]};
      ++t.posted;
    }
    t.down_mask = uint16_t(t.down_mask & ~bit);
    t.desktop_mask = uint16_t(t.desktop_mask & ~bit);
    if (d.finger == 0u) {
      if (was_down && t.down_mask == 0u && allow_toggle) {
        const uint32_t rx_max = t.max_x ? t.max_x : 10800u;
        const uint32_t ry_max = t.max_y ? t.max_y : 25200u;
        const uint64_t held = (now_ms > 0 && now_ms >= t.tap_down_ms) ? (now_ms - t.tap_down_ms) : 0ull;
        const uint32_t dy_up = (t.swipe_candidate && t.tap_down_y > t.cur_raw_y)
            ? (t.tap_down_y - t.cur_raw_y)
            : 0u;
        const uint32_t dx_abs = t.swipe_candidate ? abs_diff32(t.cur_raw_x, t.tap_down_x) : 0u;
        const bool valid_swipe_up = t.swipe_candidate &&
            (now_ms == 0 || held <= kSwipeMaxMs) &&
            dy_up >= ry_max / 14u &&
            dy_up > dx_abs;
        if (mode == MapMode::ClockWakeOnly) {
          if (t.tap_candidate && now_ms > 0 && held <= kTapHoldMaxMs) {
            if (t.last_tap_ms != 0 &&
                now_ms >= t.last_tap_ms + kDoubleTapMinMs &&
                now_ms - t.last_tap_ms <= kDoubleTapMaxMs &&
                abs_diff32(t.tap_down_x, t.last_tap_x) <= rx_max / 6u &&
                abs_diff32(t.tap_down_y, t.last_tap_y) <= ry_max / 6u) {
              if (pocket_near) {
                r.pocket_blocked = true;
                ++t.pocket_blocks;
              } else {
                r.toggle_desktop = true;
                ++t.mode_toggles;
              }
              t.last_tap_ms = 0;
            } else {
              t.last_tap_ms = now_ms;
              t.last_tap_x = t.tap_down_x;
              t.last_tap_y = t.tap_down_y;
            }
          } else if (valid_swipe_up) {
            if (pocket_near) {
              r.pocket_blocked = true;
              ++t.pocket_blocks;
            } else {
              r.toggle_desktop = true;
              ++t.mode_toggles;
              ++t.swipe_wakes;
            }
            t.last_tap_ms = 0;
          }
        } else if (mode == MapMode::DesktopViewport || mode == MapMode::DesktopFull1to1) {
          if (t.armed_bar != BarAction::None && (now_ms == 0 || held <= 600ull)) {
            r.bar_action = t.armed_bar;
            ++t.bar_actions;
            if (t.armed_bar == BarAction::ClockLock) {
              r.toggle_clock = true;
              ++t.mode_toggles;
            } else if (t.armed_bar == BarAction::ZoomToggle) {
              ++t.zoom_toggles;
            }
          } else if (valid_swipe_up) {
            r.bar_action = BarAction::Home;
            ++t.bar_actions;
            ++t.swipe_homes;
          }
        }
      }
      t.tap_candidate = false;
      t.swipe_candidate = false;
      t.pill_armed = false;
      t.armed_bar = BarAction::None;
    }
    return r;
  }
  if ((d.kind == Kind::PointerEnter || d.kind == Kind::PointerMotion) && d.valid_contact) {
    const uint16_t bit = uint16_t(1u << d.finger);
    const bool already_down = (t.down_mask & bit) != 0u;
    if (!already_down && d.finger != 0u) {
      t.tap_candidate = false;
      t.swipe_candidate = false;
      t.pill_armed = false;
      t.armed_bar = BarAction::None;
    }
    if (mode == MapMode::ClockWakeOnly) {
      if (!already_down) {
        t.down_mask = uint16_t(t.down_mask | bit);
        ++t.hud_taps;
        r.refresh_hud = !pocket_near;
        if (d.finger == 0u && t.down_mask == 1u && allow_toggle) {
          t.tap_candidate = (now_ms > 0);
          t.swipe_candidate = true;
          t.tap_down_ms = now_ms;
          t.tap_down_x = d.raw_x;
          t.tap_down_y = d.raw_y;
          t.cur_raw_x = d.raw_x;
          t.cur_raw_y = d.raw_y;
        }
      } else if (d.finger == 0u) {
        t.cur_raw_x = d.raw_x;
        t.cur_raw_y = d.raw_y;
        if (t.tap_candidate) {
          const uint32_t rx_max = t.max_x ? t.max_x : 10800u;
          const uint32_t ry_max = t.max_y ? t.max_y : 25200u;
          if (abs_diff32(d.raw_x, t.tap_down_x) > rx_max / 12u ||
              abs_diff32(d.raw_y, t.tap_down_y) > ry_max / 12u) {
            t.tap_candidate = false;
          }
        }
      }
      return r;
    }
    if (!already_down) {
      t.down_mask = uint16_t(t.down_mask | bit);
      const BarAction hit_first = (allow_toggle && d.finger == 0u && t.down_mask == 1u)
          ? bar_hit_test_mode(t, mode, d.raw_x, d.raw_y)
          : BarAction::None;
      uint32_t xn = 0, yn = 0;
      if (hit_first == BarAction::None && map_mode_point(t, mode, d.raw_x, d.raw_y, true, &xn, &yn)) {
        t.desktop_mask = uint16_t(t.desktop_mask | bit);
        t.last_x[d.finger] = xn;
        t.last_y[d.finger] = yn;
        r.ops[r.count++] = PostOp{1u, d.finger, xn, yn};
        ++t.posted;
        if (d.finger == 0u) {
          t.swipe_candidate = false;
          t.pill_armed = false;
          t.armed_bar = BarAction::None;
        }
      } else {
        const uint32_t ry_max = t.max_y ? t.max_y : 25200u;
        const uint32_t dst_end = (mode == MapMode::DesktopFull1to1) ? (kFullY0 + kFullH) : (kDesktopY0 + kDesktopH);
        const uint32_t vp_bottom = uint32_t((uint64_t(dst_end) * ry_max) / kPanelH);
        const bool in_lower_chin = (allow_toggle && d.finger == 0u && t.down_mask == 1u && d.raw_y >= vp_bottom);
        if (in_lower_chin) {
          t.swipe_candidate = true;
          t.tap_down_ms = now_ms;
          t.tap_down_x = d.raw_x;
          t.tap_down_y = d.raw_y;
          t.cur_raw_x = d.raw_x;
          t.cur_raw_y = d.raw_y;
        }
        const BarAction hit = hit_first;
        if (hit != BarAction::None) {
          t.armed_bar = hit;
          t.pill_armed = (hit == BarAction::ClockLock);
          t.tap_down_ms = now_ms;
          ++t.hud_taps;
        } else {
          ++t.hud_taps;
          r.refresh_hud = true;
        }
      }
      return r;
    }
    if ((t.desktop_mask & bit) != 0u) {
      uint32_t xn = 0, yn = 0;
      (void)map_mode_point(t, mode, d.raw_x, d.raw_y, false, &xn, &yn);
      t.last_x[d.finger] = xn;
      t.last_y[d.finger] = yn;
      r.ops[r.count++] = PostOp{2u, d.finger, xn, yn};
      ++t.posted;
    } else if (d.finger == 0u) {
      if (t.swipe_candidate) {
        t.cur_raw_x = d.raw_x;
        t.cur_raw_y = d.raw_y;
      }
      if (t.armed_bar != BarAction::None &&
          bar_hit_test_mode(t, mode, d.raw_x, d.raw_y) != t.armed_bar) {
        t.armed_bar = BarAction::None;
        t.pill_armed = false;
      }
    }
  }
  return r;
}

struct CalendarLocal {
  unsigned year;
  unsigned month;
  unsigned day;
  unsigned hour;
  unsigned minute;
};

/* Civil calendar in UTC+8 from Unix epoch seconds (Howard Hinnant's algorithm). */
inline CalendarLocal calendar_utc8(uint64_t epoch_sec) {
  const uint64_t local = epoch_sec + 8ull * 3600ull;
  const unsigned day_sec = unsigned(local % 86400ull);
  int64_t z = int64_t(local / 86400ull) + 719468ll;
  const int64_t era = (z >= 0 ? z : z - 146096ll) / 146097ll;
  const unsigned doe = unsigned(z - era * 146097ll);
  const unsigned yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
  int64_t y = int64_t(yoe) + era * 400ll;
  const unsigned doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
  const unsigned mp = (5u * doy + 2u) / 153u;
  const unsigned d = doy - (153u * mp + 2u) / 5u + 1u;
  const unsigned m = mp < 10u ? (mp + 3u) : (mp - 9u);
  if (m <= 2u) {
    ++y;
  }
  return CalendarLocal{unsigned(y > 0 ? y : 1970), m, d, day_sec / 3600u, (day_sec / 60u) % 60u};
}

inline uint32_t avg2_bgra(uint32_t a, uint32_t b) {
  return (((a & 0xfefefefeu) >> 1) + ((b & 0xfefefefeu) >> 1) + (a & b & 0x01010101u)) | 0xff000000u;
}

inline uint32_t scale_bgra_q8(uint32_t c, uint32_t scale_q8) {
  if (scale_q8 >= 256u) {
    return c | 0xff000000u;
  }
  const uint32_t rb = (((c & 0x00ff00ffu) * scale_q8) >> 8) & 0x00ff00ffu;
  const uint32_t g = (((c & 0x0000ff00u) * scale_q8) >> 8) & 0x0000ff00u;
  return 0xff000000u | rb | g;
}

/* 2:1 SWAR box-filtered downscale of a 2160x1916 BGRA8 frame into a 1080x958 region on the 1080x2520 cover FB. */
inline unsigned blit_2to1_bgra(const uint8_t *src, uint32_t src_stride, uint32_t src_w, uint32_t src_h,
                               volatile uint8_t *dst_fb, uint32_t dst_stride, uint32_t dst_y0,
                               uint32_t scale_q8 = 256u) {
  if (!src || !dst_fb || src_w < 2u || src_h < 2u || src_stride < src_w * 4u || dst_stride < kPanelW * 4u) {
    return 0;
  }
  uint32_t out_w = src_w / 2u;
  uint32_t out_h = src_h / 2u;
  if (out_w > kPanelW) {
    out_w = kPanelW;
  }
  if (dst_y0 >= kPanelH) {
    return 0;
  }
  if (out_h > kPanelH - dst_y0) {
    out_h = kPanelH - dst_y0;
  }
  const bool dim = scale_q8 < 256u;
  for (uint32_t y = 0; y < out_h; ++y) {
    const uint32_t *r0 = reinterpret_cast<const uint32_t *>(src + uint64_t(2u * y) * src_stride);
    const uint32_t *r1 = reinterpret_cast<const uint32_t *>(src + uint64_t(2u * y + 1u) * src_stride);
    volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(dst_fb + uint64_t(dst_y0 + y) * dst_stride);
    uint32_t x = 0;
    for (; x + 1u < out_w; x += 2u) {
      const uint32_t sx = 2u * x;
      const uint32_t top0 = avg2_bgra(r0[sx], r0[sx + 1u]);
      const uint32_t bot0 = avg2_bgra(r1[sx], r1[sx + 1u]);
      const uint32_t top1 = avg2_bgra(r0[sx + 2u], r0[sx + 3u]);
      const uint32_t bot1 = avg2_bgra(r1[sx + 2u], r1[sx + 3u]);
      uint32_t p0 = avg2_bgra(top0, bot0);
      uint32_t p1 = avg2_bgra(top1, bot1);
      if (dim) {
        p0 = scale_bgra_q8(p0, scale_q8);
        p1 = scale_bgra_q8(p1, scale_q8);
      }
      const uint64_t pair = uint64_t(p0) | (uint64_t(p1) << 32);
      *reinterpret_cast<volatile uint64_t *>(&dst[x]) = pair;
    }
    for (; x < out_w; ++x) {
      const uint32_t sx = 2u * x;
      uint32_t p = avg2_bgra(avg2_bgra(r0[sx], r0[sx + 1u]), avg2_bgra(r1[sx], r1[sx + 1u]));
      if (dim) {
        p = scale_bgra_q8(p, scale_q8);
      }
      dst[x] = p;
    }
  }
  return out_h;
}

/* OUTER-03: 1:1 Retina pixel-exact full-height crop blit (1080x1916 from [crop_x0..crop_x0+1080, 0..1916])
 * into dst_y0..dst_y0+1916 on the 1080x2520 cover FB (85%+ screen coverage, zero downscale blur). */
inline unsigned blit_1to1_crop_bgra(const uint8_t *src, uint32_t src_stride, uint32_t src_w, uint32_t src_h,
                                    uint32_t crop_x0, volatile uint8_t *dst_fb, uint32_t dst_stride,
                                    uint32_t dst_y0, uint32_t scale_q8 = 256u) {
  if (!src || !dst_fb || src_w < kPanelW || src_h < 1u || src_stride < src_w * 4u || dst_stride < kPanelW * 4u) {
    return 0;
  }
  if (dst_y0 >= kPanelH) {
    return 0;
  }
  const uint32_t x0 = (crop_x0 + kPanelW <= src_w) ? crop_x0 : ((src_w - kPanelW) / 2u);
  uint32_t out_h = src_h;
  if (out_h > kPanelH - dst_y0) {
    out_h = kPanelH - dst_y0;
  }
  const bool dim = scale_q8 < 256u;
  for (uint32_t y = 0; y < out_h; ++y) {
    const uint32_t *r0 = reinterpret_cast<const uint32_t *>(src + uint64_t(y) * src_stride) + x0;
    volatile uint32_t *dst = reinterpret_cast<volatile uint32_t *>(dst_fb + uint64_t(dst_y0 + y) * dst_stride);
    uint32_t x = 0;
    if (!dim) {
      for (; x + 1u < kPanelW; x += 2u) {
        const uint64_t pair = (uint64_t(r0[x] | 0xff000000u)) | (uint64_t(r0[x + 1u] | 0xff000000u) << 32);
        *reinterpret_cast<volatile uint64_t *>(&dst[x]) = pair;
      }
      for (; x < kPanelW; ++x) {
        dst[x] = r0[x] | 0xff000000u;
      }
    } else {
      for (; x + 1u < kPanelW; x += 2u) {
        const uint32_t p0 = scale_bgra_q8(r0[x], scale_q8);
        const uint32_t p1 = scale_bgra_q8(r0[x + 1u], scale_q8);
        *reinterpret_cast<volatile uint64_t *>(&dst[x]) = uint64_t(p0) | (uint64_t(p1) << 32);
      }
      for (; x < kPanelW; ++x) {
        dst[x] = scale_bgra_q8(r0[x], scale_q8);
      }
    }
  }
  return out_h;
}

}  // namespace zizhan_outer_touch
