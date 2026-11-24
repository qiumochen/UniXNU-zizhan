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
};

enum : uint32_t {
  kMaxFingers = 10,
  kPanelW = 1080,
  kPanelH = 2520,
  kDesktopW = 1080,
  kDesktopH = 958,
  kDesktopY0 = 781,
  kEdgeBezel = 96,
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
  uint32_t events = 0;
  uint32_t posted = 0;
  uint32_t hud_taps = 0;
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

inline bool map_desktop_point(const Tracker &t, uint32_t raw_x, uint32_t raw_y,
                              bool check_band, uint32_t *out_x, uint32_t *out_y) {
  const uint32_t rx_max = t.max_x ? t.max_x : 10800u;
  const uint32_t ry_max = t.max_y ? t.max_y : 25200u;
  const uint32_t vp_y0 = uint32_t((uint64_t(kDesktopY0) * ry_max) / kPanelH);
  const uint32_t vp_h = uint32_t((uint64_t(kDesktopH) * ry_max) / kPanelH);
  const uint32_t bezel = uint32_t((uint64_t(kEdgeBezel) * ry_max) / kPanelH);
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
  uint32_t xn = uint32_t((uint64_t(cx) * kNormMax + rx_max / 2u) / rx_max);
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
}

inline StepResult step(Tracker &t, const uint8_t ev[8], MapMode mode) {
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
    if ((t.desktop_mask & bit) != 0u) {
      r.ops[r.count++] = PostOp{3u, d.finger, t.last_x[d.finger], t.last_y[d.finger]};
      ++t.posted;
    }
    t.down_mask = uint16_t(t.down_mask & ~bit);
    t.desktop_mask = uint16_t(t.desktop_mask & ~bit);
    return r;
  }
  if ((d.kind == Kind::PointerEnter || d.kind == Kind::PointerMotion) && d.valid_contact) {
    const uint16_t bit = uint16_t(1u << d.finger);
    const bool already_down = (t.down_mask & bit) != 0u;
    if (mode == MapMode::ClockWakeOnly) {
      if (!already_down) {
        t.down_mask = uint16_t(t.down_mask | bit);
        ++t.hud_taps;
        r.refresh_hud = true;
      }
      return r;
    }
    if (!already_down) {
      t.down_mask = uint16_t(t.down_mask | bit);
      uint32_t xn = 0, yn = 0;
      if (map_desktop_point(t, d.raw_x, d.raw_y, true, &xn, &yn)) {
        t.desktop_mask = uint16_t(t.desktop_mask | bit);
        t.last_x[d.finger] = xn;
        t.last_y[d.finger] = yn;
        r.ops[r.count++] = PostOp{1u, d.finger, xn, yn};
        ++t.posted;
      } else {
        ++t.hud_taps;
        r.refresh_hud = true;
      }
      return r;
    }
    if ((t.desktop_mask & bit) != 0u) {
      uint32_t xn = 0, yn = 0;
      (void)map_desktop_point(t, d.raw_x, d.raw_y, false, &xn, &yn);
      t.last_x[d.finger] = xn;
      t.last_y[d.finger] = yn;
      r.ops[r.count++] = PostOp{2u, d.finger, xn, yn};
      ++t.posted;
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

/* 2:1 SWAR box-filtered downscale of a 2160x1916 BGRA8 frame into a 1080x958 region on the 1080x2520 cover FB. */
inline unsigned blit_2to1_bgra(const uint8_t *src, uint32_t src_stride, uint32_t src_w, uint32_t src_h,
                               volatile uint8_t *dst_fb, uint32_t dst_stride, uint32_t dst_y0) {
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
      const uint64_t pair = uint64_t(avg2_bgra(top0, bot0)) | (uint64_t(avg2_bgra(top1, bot1)) << 32);
      *reinterpret_cast<volatile uint64_t *>(&dst[x]) = pair;
    }
    for (; x < out_w; ++x) {
      const uint32_t sx = 2u * x;
      dst[x] = avg2_bgra(avg2_bgra(r0[sx], r0[sx + 1u]), avg2_bgra(r1[sx], r1[sx + 1u]));
    }
  }
  return out_h;
}

}  // namespace zizhan_outer_touch
