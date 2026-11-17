#include <assert.h>
#include <stdint.h>
#include "../../../drivers/ArmDTPlatform/ZizhanHallState.h"

int main() {
  constexpr uint64_t debounce = 15'000'000;
  zizhan_hall::State state{};

  assert(zizhan_hall::sample(state, true, 100, debounce).event == zizhan_hall::Event::Pending);
  assert(zizhan_hall::sample(state, true, 15'000'099, debounce).event == zizhan_hall::Event::Pending);
  auto baseline = zizhan_hall::sample(state, true, 15'000'100, debounce);
  assert(baseline.event == zizhan_hall::Event::Baseline && baseline.stable);

  assert(zizhan_hall::sample(state, false, 15'000'200, debounce).candidate_changed);
  assert(zizhan_hall::sample(state, true, 20'000'000, debounce).candidate_changed);
  assert(zizhan_hall::sample(state, true, 34'999'999, debounce).event == zizhan_hall::Event::Pending);
  assert(zizhan_hall::sample(state, true, 35'000'000, debounce).event == zizhan_hall::Event::Pending);
  assert(zizhan_hall::sample(state, false, 35'000'100, debounce).candidate_changed);
  auto transition = zizhan_hall::sample(state, false, 50'000'100, debounce);
  assert(transition.event == zizhan_hall::Event::Transition && !transition.stable);

  const auto invalid = zizhan_hall::sample(state, true, 49'000'000, debounce);
  assert(invalid.event == zizhan_hall::Event::InvalidTime && !invalid.stable);
  const auto recovered = zizhan_hall::sample(state, false, 70'000'000, debounce);
  assert(recovered.event == zizhan_hall::Event::Pending);
  assert(zizhan_hall::sample(state, true, 80'000'000, debounce).candidate_changed);
  assert(zizhan_hall::sample(state, true, 94'999'999, debounce).event == zizhan_hall::Event::Pending);
  const auto reopened = zizhan_hall::sample(state, true, 95'000'000, debounce);
  assert(reopened.event == zizhan_hall::Event::Transition && reopened.stable);
  assert(zizhan_hall::sample(state, true, 95'000'000, debounce).event == zizhan_hall::Event::Pending);

  // Never commit a baseline while the pin bounces; no assumed initial state.
  zizhan_hall::State bounce{};
  for (uint64_t i = 0; i < 100; ++i) {
    assert(zizhan_hall::sample(bounce, i % 2, i * 5'000'000, debounce).event == zizhan_hall::Event::Pending);
    assert(!bounce.has_stable);
  }
  auto settled = zizhan_hall::sample(bounce, true, 510'000'000, debounce);
  assert(settled.event == zizhan_hall::Event::Baseline && settled.stable);
  zizhan_hall::State late{};
  const uint64_t last = UINT64_MAX - debounce;
  assert(zizhan_hall::sample(late, false, last, debounce).event == zizhan_hall::Event::Pending);
  assert(zizhan_hall::sample(late, false, UINT64_MAX, debounce).event == zizhan_hall::Event::Baseline);
  return 0;
}
