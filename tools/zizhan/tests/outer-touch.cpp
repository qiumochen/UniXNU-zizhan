/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>
#include "ZizhanOuterTouchPlan.h"

using namespace zizhan_outer_touch;

static void test_decode_super_and_standard() {
  /* 16-bit super-res: x = 5400 (0x1518), y = 12600 (0x3138), finger 2, type 1 (finger), left 3 */
  const uint8_t ev_super[8] = {0x13, 0x21, 0x18, 0x15, 0x38, 0x31, 0x40, 0x03};
  const Decoded ds = decode_event(ev_super, true, 10800, 25200);
  assert(ds.kind == Kind::PointerEnter);
  assert(ds.valid_contact);
  assert(ds.finger == 2);
  assert(ds.touch_type == 1);
  assert(ds.raw_x == 5400);
  assert(ds.raw_y == 12600);
  assert(ds.left == 3);

  /* 12-bit standard: x = 540 (0x21c), y = 1260 (0x4ec -> low4=0xc, high8=0x4e), finger 0, type 1 */
  const uint8_t ev_std[8] = {0x23, 0x01, 0x1c, 0xc2, 0x4e, 0x00, 0x20, 0x00};
  const Decoded d12 = decode_event(ev_std, false, 1080, 2520);
  assert(d12.kind == Kind::PointerMotion);
  assert(d12.valid_contact);
  assert(d12.finger == 0);
  assert(d12.raw_x == 540);
  assert(d12.raw_y == 1260);

  /* Palm (type 5) and hover (type 2) rejected as contacts */
  const uint8_t palm[8] = {0x13, 0x05, 0x18, 0x15, 0x38, 0x31, 0x40, 0x00};
  assert(!decode_event(palm, true, 10800, 25200).valid_contact);
  const uint8_t hover[8] = {0x13, 0x02, 0x18, 0x15, 0x38, 0x31, 0x40, 0x00};
  assert(!decode_event(hover, true, 10800, 25200).valid_contact);

  /* Release-all 0x33 0xb5 */
  const uint8_t rel_all[8] = {0x33, 0xb5, 0, 0, 0, 0, 0, 0};
  const Decoded dr = decode_event(rel_all, true, 10800, 25200);
  assert(dr.kind == Kind::PointerLeave && dr.release_all);
}

static void test_viewport_and_edge_swipes() {
  Tracker t{};
  configure_sysinfo(t, 10800, 25200);
  assert(t.super_res && t.max_x == 10800 && t.max_y == 25200);

  /* Exact center of desktop viewport: x = 5400, y = (781 + 479) * 10 = 12600 -> ~(32767, 32767) */
  const uint8_t down_mid[8] = {0x13, 0x01, 0x18, 0x15, 0x38, 0x31, 0x30, 0x00};
  const StepResult s1 = step(t, down_mid, MapMode::DesktopViewport);
  assert(s1.count == 1 && s1.ops[0].type == 1 && s1.ops[0].finger == 0);
  assert(s1.ops[0].x >= 32760 && s1.ops[0].x <= 32775);
  assert(s1.ops[0].y >= 32760 && s1.ops[0].y <= 32775);

  /* Bottom bezel Home-bar swipe-up: start at y = 17900 (inside +960 bezel below 17390), clamps to y_norm = 65535 */
  const uint8_t down_bezel[8] = {0x13, 0x11, 0x18, 0x15, uint8_t(17900 & 0xff), uint8_t(17900 >> 8), 0x30, 0x00};
  const StepResult s2 = step(t, down_bezel, MapMode::DesktopViewport);
  assert(s2.count == 1 && s2.ops[0].type == 1 && s2.ops[0].finger == 1);
  assert(s2.ops[0].y == 65535);

  /* Tap far above viewport (y = 2000, status HUD area): refreshes HUD, does NOT post desktop touch */
  const uint8_t down_hud[8] = {0x13, 0x21, 0x18, 0x15, uint8_t(2000 & 0xff), uint8_t(2000 >> 8), 0x30, 0x00};
  const StepResult s3 = step(t, down_hud, MapMode::DesktopViewport);
  assert(s3.count == 0 && s3.refresh_hud && t.hud_taps == 1);

  /* Controller reset 0x03 releases fingers 0 and 1 cleanly and rearms active scan */
  const uint8_t boot_ev[8] = {0x03, 0, 0, 0, 0, 0, 0, 0};
  const StepResult s4 = step(t, boot_ev, MapMode::DesktopViewport);
  assert(s4.rearm_scan && s4.count == 2);
  assert(s4.ops[0].type == 3 && s4.ops[0].finger == 0);
  assert(s4.ops[1].type == 3 && s4.ops[1].finger == 1);
  assert(t.down_mask == 0 && t.desktop_mask == 0);
}

static void test_clock_mode_and_calendar_and_blit() {
  Tracker t{};
  const uint8_t tap[8] = {0x13, 0x01, 0x18, 0x15, 0x38, 0x31, 0x30, 0x00};
  const StepResult s = step(t, tap, MapMode::ClockWakeOnly);
  assert(s.count == 0 && s.refresh_hud && t.posted == 0 && t.hud_taps == 1);

  /* 2026-11-24 16:30:00 UTC -> 2026-11-25 00:30 UTC+8 (1795537800) */
  const CalendarLocal c = calendar_utc8(1795537800ull);
  assert(c.year == 2026 && c.month == 11 && c.day == 25 && c.hour == 0 && c.minute == 30);

  /* 2:1 SWAR box-filter blit check: 4x4 -> 2x2 */
  std::vector<uint32_t> src(2160 * 4, 0xff204080u);
  src[0] = 0xff000000u; src[1] = 0xff400000u;
  src[2160] = 0xff000000u; src[2161] = 0xff400000u;
  std::vector<uint8_t> dst(2520 * 4320, 0);
  const unsigned rows = blit_2to1_bgra(reinterpret_cast<const uint8_t *>(src.data()), 2160 * 4, 2160, 4,
                                       dst.data(), 4320, kDesktopY0);
  assert(rows == 2);
  const uint32_t *row0 = reinterpret_cast<const uint32_t *>(dst.data() + kDesktopY0 * 4320);
  assert(row0[0] == 0xff200000u);
  assert(row0[1] == 0xff204080u);
}

static void test_double_tap_and_lock_pill_toggle() {
  Tracker t{};
  configure_sysinfo(t, 10800, 25200);

  /* Center tap 1: down at t=1000ms, up at t=1070ms -> refreshes HUD, does not toggle yet */
  const uint8_t down_mid[8] = {0x13, 0x01, 0x18, 0x15, 0x38, 0x31, 0x30, 0x00};
  const uint8_t up_f0[8] = {0x33, 0x01, 0, 0, 0, 0, 0, 0};
  StepResult s = step(t, down_mid, MapMode::ClockWakeOnly, 1000);
  assert(s.refresh_hud && !s.toggle_desktop && !s.toggle_clock);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 1070);
  assert(!s.toggle_desktop && t.down_mask == 0);

  /* Center tap 2 after 180ms (t=1250ms down, t=1310ms up) -> toggles Clock -> Desktop cleanly on UP! */
  s = step(t, down_mid, MapMode::ClockWakeOnly, 1250);
  assert(!s.toggle_desktop);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 1310);
  assert(s.toggle_desktop && !s.toggle_clock && t.mode_toggles == 1 && t.down_mask == 0);

  /* Slow second tap (> 500ms later) does NOT false-trigger */
  s = step(t, down_mid, MapMode::ClockWakeOnly, 2000);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2060);
  assert(!s.toggle_desktop);
  s = step(t, down_mid, MapMode::ClockWakeOnly, 2700);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2760);
  assert(!s.toggle_desktop && t.mode_toggles == 1);

  /* Distant second tap (far Y) does NOT false-trigger */
  const uint8_t down_far[8] = {0x13, 0x01, 0x18, 0x15, uint8_t(4000 & 0xff), uint8_t(4000 >> 8), 0x30, 0x00};
  s = step(t, down_far, MapMode::ClockWakeOnly, 2900);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2960);
  assert(!s.toggle_desktop && t.mode_toggles == 1);

  /* In DesktopViewport: tap inside Lock/Clock pill (x=5400, y=19300) toggles Desktop -> Clock on UP without posting touch */
  const uint8_t down_pill[8] = {0x13, 0x01, 0x18, 0x15, uint8_t(19300 & 0xff), uint8_t(19300 >> 8), 0x30, 0x00};
  s = step(t, down_pill, MapMode::DesktopViewport, 4000);
  assert(s.count == 0 && !s.toggle_clock);
  s = step(t, up_f0, MapMode::DesktopViewport, 4080);
  assert(s.count == 0 && s.toggle_clock && !s.toggle_desktop && t.mode_toggles == 2 && t.down_mask == 0);

  /* Dragging out of pill cancels pill tap */
  const uint8_t move_out[8] = {0x23, 0x01, 0x18, 0x15, uint8_t(21500 & 0xff), uint8_t(21500 >> 8), 0x30, 0x00};
  s = step(t, down_pill, MapMode::DesktopViewport, 5000);
  s = step(t, move_out, MapMode::DesktopViewport, 5050);
  s = step(t, up_f0, MapMode::DesktopViewport, 5100);
  assert(!s.toggle_clock && t.mode_toggles == 2);
}

static void test_pocket_guard_quick_bar_and_dim_blit() {
  Tracker t{};
  configure_sysinfo(t, 10800, 25200);
  const uint8_t down_mid[8] = {0x13, 0x01, 0x18, 0x15, 0x38, 0x31, 0x30, 0x00};
  const uint8_t up_f0[8] = {0x33, 0x01, 0, 0, 0, 0, 0, 0};

  /* 1. Pocket guard (pocket_near=true) blocks double-tap wake and increments pocket_blocks */
  StepResult s = step(t, down_mid, MapMode::ClockWakeOnly, 1000, true, true);
  assert(!s.refresh_hud && !s.toggle_desktop);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 1060, true, true);
  s = step(t, down_mid, MapMode::ClockWakeOnly, 1220, true, true);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 1280, true, true);
  assert(!s.toggle_desktop && s.pocket_blocked && t.pocket_blocks == 1 && t.mode_toggles == 0);

  /* Once out of pocket (pocket_near=false), double-tap wakes cleanly */
  s = step(t, down_mid, MapMode::ClockWakeOnly, 2000, true, false);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2060, true, false);
  s = step(t, down_mid, MapMode::ClockWakeOnly, 2220, true, false);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2280, true, false);
  assert(s.toggle_desktop && !s.pocket_blocked && t.mode_toggles == 1);

  /* 2. Four bottom quick-bar pills: VOL- (x=1500), HOME (x=3900), CLOCK (x=6400), VOL+ (x=9000) at y=19300 */
  auto tap_bar = [&](uint32_t rx, uint64_t t0) {
    const uint8_t dn[8] = {0x13, 0x01, uint8_t(rx), uint8_t(rx >> 8), uint8_t(19300 & 0xff), uint8_t(19300 >> 8), 0x30, 0x00};
    const StepResult r_dn = step(t, dn, MapMode::DesktopViewport, t0);
    assert(r_dn.count == 0 && r_dn.bar_action == BarAction::None);
    return step(t, up_f0, MapMode::DesktopViewport, t0 + 60);
  };
  s = tap_bar(1500, 3000);
  assert(s.bar_action == BarAction::VolDown && !s.toggle_clock && s.count == 0);
  s = tap_bar(3900, 3200);
  assert(s.bar_action == BarAction::Home && !s.toggle_clock && s.count == 0);
  s = tap_bar(9000, 3400);
  assert(s.bar_action == BarAction::VolUp && !s.toggle_clock && s.count == 0);
  s = tap_bar(6400, 3600);
  assert(s.bar_action == BarAction::ClockLock && s.toggle_clock && s.count == 0);
  assert(t.bar_actions == 4 && t.mode_toggles == 2);

  /* 3. Dimmed 2:1 SWAR blit at 50% (scale_q8 = 128) halves RGB channels while keeping alpha 0xff */
  std::vector<uint32_t> src(2160 * 4, 0xff4080c0u);
  std::vector<uint8_t> dst(2520 * 4320, 0);
  const unsigned rows = blit_2to1_bgra(reinterpret_cast<const uint8_t *>(src.data()), 2160 * 4, 2160, 4,
                                       dst.data(), 4320, kDesktopY0, 128u);
  assert(rows == 2);
  const uint32_t *row0 = reinterpret_cast<const uint32_t *>(dst.data() + kDesktopY0 * 4320);
  assert(row0[0] == 0xff204060u && row0[1] == 0xff204060u);
}

static void test_swipe_up_wake_and_chin_home() {
  Tracker t{};
  configure_sysinfo(t, 10800, 25200);
  const uint8_t up_f0[8] = {0x33, 0x01, 0, 0, 0, 0, 0, 0};

  auto mk = [](uint8_t op, uint32_t rx, uint32_t ry, uint8_t out[8]) {
    out[0] = op; out[1] = 0x01;
    out[2] = uint8_t(rx); out[3] = uint8_t(rx >> 8);
    out[4] = uint8_t(ry); out[5] = uint8_t(ry >> 8);
    out[6] = 0x30; out[7] = 0x00;
  };

  /* 1. Pocket-guarded upward swipe on Cover Clock is blocked */
  uint8_t dn[8], mv[8];
  mk(0x13, 5400, 21000, dn);
  mk(0x23, 5450, 15000, mv);
  (void)step(t, dn, MapMode::ClockWakeOnly, 1000, true, true);
  (void)step(t, mv, MapMode::ClockWakeOnly, 1080, true, true);
  StepResult s = step(t, up_f0, MapMode::ClockWakeOnly, 1150, true, true);
  assert(!s.toggle_desktop && s.pocket_blocked && t.pocket_blocks == 1 && t.swipe_wakes == 0);

  /* 2. Downward or horizontal swipes on Cover Clock do NOT wake */
  mk(0x13, 5400, 15000, dn);
  mk(0x23, 5400, 21000, mv);
  (void)step(t, dn, MapMode::ClockWakeOnly, 2000, true, false);
  (void)step(t, mv, MapMode::ClockWakeOnly, 2080, true, false);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2150, true, false);
  assert(!s.toggle_desktop && t.swipe_wakes == 0);

  mk(0x13, 2000, 18000, dn);
  mk(0x23, 8500, 15500, mv);
  (void)step(t, dn, MapMode::ClockWakeOnly, 2500, true, false);
  (void)step(t, mv, MapMode::ClockWakeOnly, 2580, true, false);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 2650, true, false);
  assert(!s.toggle_desktop && t.swipe_wakes == 0);

  /* 3. Clean upward swipe on Cover Clock wakes directly into 2:1 Cover Desktop */
  mk(0x13, 5400, 21000, dn);
  mk(0x23, 5420, 15500, mv);
  (void)step(t, dn, MapMode::ClockWakeOnly, 3000, true, false);
  (void)step(t, mv, MapMode::ClockWakeOnly, 3090, true, false);
  s = step(t, up_f0, MapMode::ClockWakeOnly, 3160, true, false);
  assert(s.toggle_desktop && !s.pocket_blocked && t.swipe_wakes == 1 && t.mode_toggles == 1);

  /* 4. Upward swipe from lower chin (y=22000 -> 16000) in DesktopViewport triggers Home/Unlock */
  mk(0x13, 5400, 22000, dn);
  mk(0x23, 5380, 16000, mv);
  (void)step(t, dn, MapMode::DesktopViewport, 4000, true, false);
  (void)step(t, mv, MapMode::DesktopViewport, 4090, true, false);
  s = step(t, up_f0, MapMode::DesktopViewport, 4170, true, false);
  assert(s.bar_action == BarAction::Home && !s.toggle_clock && t.swipe_homes == 1 && t.bar_actions == 1);
}

static void test_full_height_1to1_mode_and_zoom_toggle() {
  Tracker t{};
  configure_sysinfo(t, 10800, 25200);
  const uint8_t up_f0[8] = {0x33, 0x01, 0, 0, 0, 0, 0, 0};

  auto mk = [](uint8_t op, uint32_t rx, uint32_t ry, uint8_t out[8]) {
    out[0] = op; out[1] = 0x01;
    out[2] = uint8_t(rx); out[3] = uint8_t(rx >> 8);
    out[4] = uint8_t(ry); out[5] = uint8_t(ry >> 8);
    out[6] = 0x30; out[7] = 0x00;
  };

  /* 1. In DesktopViewport (2:1), tapping Zoom pill (x=5400, y=20700) emits ZoomToggle */
  uint8_t dn[8];
  mk(0x13, 5400, 20700, dn);
  StepResult s = step(t, dn, MapMode::DesktopViewport, 1000);
  assert(s.count == 0);
  s = step(t, up_f0, MapMode::DesktopViewport, 1060);
  assert(s.bar_action == BarAction::ZoomToggle && t.zoom_toggles == 1);

  /* 2. In DesktopFull1to1 (crop_x0 = 540, y = 220..2136):
   *    - Left edge (x=0, y=11780 = mid of 2200..21360) -> inner x = 540/2160 = 16384, y = 32768
   *    - Center (x=5400, y=11780) -> inner x = 1080/2160 = 32768, y = 32768
   *    - Right edge (x=10800, y=11780) -> inner x = 1620/2160 = 49151, y = 32768 */
  mk(0x13, 0, 11780, dn);
  s = step(t, dn, MapMode::DesktopFull1to1, 2000);
  assert(s.count == 1 && s.ops[0].x >= 16380 && s.ops[0].x <= 16390 && s.ops[0].y >= 32760 && s.ops[0].y <= 32775);
  (void)step(t, up_f0, MapMode::DesktopFull1to1, 2050);

  mk(0x13, 5400, 11780, dn);
  s = step(t, dn, MapMode::DesktopFull1to1, 2200);
  assert(s.count == 1 && s.ops[0].x >= 32760 && s.ops[0].x <= 32775 && s.ops[0].y >= 32760 && s.ops[0].y <= 32775);
  (void)step(t, up_f0, MapMode::DesktopFull1to1, 2250);

  mk(0x13, 10800, 11780, dn);
  s = step(t, dn, MapMode::DesktopFull1to1, 2400);
  assert(s.count == 1 && s.ops[0].x >= 49145 && s.ops[0].x <= 49155);
  (void)step(t, up_f0, MapMode::DesktopFull1to1, 2450);

  /* 3. 5-pill bottom bar in DesktopFull1to1 (y=22400): center pill (x=5400) -> ZoomToggle, pill 4 (x=7400) -> ClockLock */
  mk(0x13, 5400, 22400, dn);
  (void)step(t, dn, MapMode::DesktopFull1to1, 3000);
  s = step(t, up_f0, MapMode::DesktopFull1to1, 3060);
  assert(s.bar_action == BarAction::ZoomToggle && t.zoom_toggles == 2);

  mk(0x13, 7400, 22400, dn);
  (void)step(t, dn, MapMode::DesktopFull1to1, 3200);
  s = step(t, up_f0, MapMode::DesktopFull1to1, 3260);
  assert(s.bar_action == BarAction::ClockLock && s.toggle_clock);

  /* 4. 1:1 crop blit copies exact pixels from [540..1620, 0..4] to [0..1080, kFullY0..kFullY0+4] */
  std::vector<uint32_t> src(2160 * 4, 0xff112233u);
  src[540] = 0xffabcdefu;
  src[1619] = 0xff445566u;
  std::vector<uint8_t> dst(2520 * 4320, 0);
  const unsigned rows = blit_1to1_crop_bgra(reinterpret_cast<const uint8_t *>(src.data()), 2160 * 4, 2160, 4,
                                            540u, dst.data(), 4320, kFullY0, 256u);
  assert(rows == 4);
  const uint32_t *row0 = reinterpret_cast<const uint32_t *>(dst.data() + kFullY0 * 4320);
  assert(row0[0] == 0xffabcdefu && row0[1079] == 0xff445566u);
}

int main() {
  test_decode_super_and_standard();
  test_viewport_and_edge_swipes();
  test_clock_mode_and_calendar_and_blit();
  test_double_tap_and_lock_pill_toggle();
  test_pocket_guard_quick_bar_and_dim_blit();
  test_swipe_up_wake_and_chin_home();
  test_full_height_1to1_mode_and_zoom_toggle();
  printf("outer-touch: PASS\n");
  return 0;
}
