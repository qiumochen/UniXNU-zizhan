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

int main() {
  test_decode_super_and_standard();
  test_viewport_and_edge_swipes();
  test_clock_mode_and_calendar_and_blit();
  printf("outer-touch: PASS\n");
  return 0;
}
