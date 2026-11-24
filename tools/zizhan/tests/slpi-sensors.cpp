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

int main() {
  test_milli_f32();
  test_autobright_hysteresis_and_override();
  test_quiesce_state();
  printf("slpi-sensors: PASS\n");
  return 0;
}
