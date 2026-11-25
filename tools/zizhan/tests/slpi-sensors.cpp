/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include "ZizhanSlpiSensors.h"

using namespace zizhan_slpi_sensors;

static void test_milli_f32() {
  assert(milli_from_f32(0x00000000u) == 0);
  assert(milli_from_f32(0x7fc00000u) == 0);  /* NaN */
  assert(milli_from_f32(0x3f800000u) == 1000);  /* 1.0f */
  assert(milli_from_f32(0xbf800000u) == -1000); /* -1.0f */
  assert(milli_from_f32(0x411ce80au) >= 9806 && milli_from_f32(0x411ce80au) <= 9807); /* 9.80665f */
  assert(milli_from_f32(0x43a2c000u) == 325500); /* 325.5f lux */
}

static void test_autobright_hysteresis_and_override() {
  assert(lux_to_dbv(0) == 56u);
  assert(lux_to_dbv(2000) == 120u);
  assert(lux_to_dbv(200000) == 700u);
  assert(lux_to_dbv(10000000) == 2047u);

  AutoBrightness ab{};
  uint32_t target = 0;
  /* Initial seed at 200 lux (700 DBV) when current backlight is 1024 */
  assert(step_autobright(ab, 200000, 1024, 1000, &target));
  assert(target == 700u && ab.updates == 1);

  /* Minor 10% jitter (220 lux) must NOT trigger flicker */
  assert(!step_autobright(ab, 220000, 700, 5000, &target));

  /* Large jump to 1000 lux (1180 DBV): debounced until >= 2500 ms */
  assert(!step_autobright(ab, 1000000, 700, 6000, &target));
  assert(!step_autobright(ab, 1000000, 700, 8400, &target));
  assert(step_autobright(ab, 1000000, 700, 8600, &target));
  assert(target == 1180u && ab.updates == 2);

  /* User drags slider manually to 1600 -> override respected for moderate lux changes */
  assert(!step_autobright(ab, 1500000, 1600, 12000, &target));
  assert(ab.user_override);

  /* Walking into 8000 lux (>4x shift) clears override after debounce */
  assert(!step_autobright(ab, 8000000, 1600, 15000, &target));
  assert(step_autobright(ab, 8000000, 1600, 17600, &target));
  assert(!ab.user_override && target > 1800u);
}

static void test_quiesce_state() {
  QuiesceState q{};
  assert(poll_sleep_ms(q, true) == 1u);
  assert(poll_sleep_ms(q, false) == 10u);

  /* Transient <500ms off glitch ignored */
  assert(step_quiesce(q, true, 0, 1000) == QuiesceAction::None);
  assert(step_quiesce(q, true, 0, 1400) == QuiesceAction::None);
  assert(step_quiesce(q, true, 1, 1450) == QuiesceAction::None);
  assert(!q.quiesced && q.pauses == 0);

  /* Sustained >=500ms display off pauses streams and relaxes polling to 250ms */
  assert(step_quiesce(q, true, 0, 2000) == QuiesceAction::None);
  assert(step_quiesce(q, true, 0, 2500) == QuiesceAction::Pause);
  assert(q.quiesced && q.pauses == 1);
  assert(poll_sleep_ms(q, false) == 250u);

  /* Display wake resumes immediately (0ms delay) */
  assert(step_quiesce(q, true, 1, 10000) == QuiesceAction::Resume);
  assert(!q.quiesced && q.resumes == 1);
  assert(poll_sleep_ms(q, false) == 10u);
}

static void test_mag_pressure_and_hinge_helpers() {
  /* 30 uT, 40 uT, 0 uT -> 50 uT (50,000 milli-uT) */
  assert(mag_norm_milli_ut(30000, -40000, 0) == 50000u);
  assert(mag_plausible(30000, -40000, 12000));
  assert(!mag_plausible(0, 0, 0));

  /* Barometer: 1013.25 hPa (1,013,250 milli-hPa) -> 101,325 Pa */
  assert(pressure_to_pa(1013250) == 101325u);
  assert(pressure_plausible_pa(101325u));
  /* Also accepts sensors reporting directly in Pa (101,325,000 milli-Pa) */
  assert(pressure_to_pa(101325000) == 101325u);
  assert(pressure_to_pa(-100) == 0u);
  assert(!pressure_plausible_pa(0u));

  /* Hinge angle: 0 deg folded, 90 deg tent, 179.5 deg flat */
  assert(hinge_angle_plausible(0) && hinge_posture(0) == 0);
  assert(hinge_angle_plausible(90000) && hinge_posture(90000) == 1);
  assert(hinge_angle_plausible(179500) && hinge_posture(179500) == 2);
  assert(!hinge_angle_plausible(250000));

  /* Proximity (msg_id 769/1031 varint event_type 1=near, 0=far; fallback 1025 cm*1000 < 3000) */
  assert(proximity_is_near(769u, 1u, 0));
  assert(!proximity_is_near(769u, 0u, 0));
  assert(proximity_is_near(1031u, 1u, 0));
  assert(proximity_is_near(1025u, 0u, 1200));
  assert(!proximity_is_near(1025u, 0u, 5000));
  assert(proximity_keep_on_in_standby(1));
  assert(!proximity_keep_on_in_standby(0));

  /* ALS-linked outer cover night-dimming Q8 scale */
  assert(outer_cover_scale_q8(-1) == 256u);
  assert(outer_cover_scale_q8(50000) == 256u);
  assert(outer_cover_scale_q8(15000) == 208u);
  assert(outer_cover_scale_q8(1200) == 144u);
}

int main() {
  test_milli_f32();
  test_autobright_hysteresis_and_override();
  test_quiesce_state();
  test_mag_pressure_and_hinge_helpers();
  printf("slpi-sensors: PASS\n");
  return 0;
}
