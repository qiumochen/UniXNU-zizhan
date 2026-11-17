/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>

namespace zizhan_hall {
enum class Event : uint8_t { Pending, Baseline, Transition, InvalidTime };

struct State {
  bool initialized = false;
  bool has_stable = false;
  bool candidate = false;
  bool stable = false;
  uint64_t candidate_since_ns = 0;
  uint64_t last_sample_ns = 0;
};

struct Update {
  Event event;
  bool stable;
  bool candidate_changed;
};

inline Update sample(State &state, bool asserted, uint64_t now_ns, uint64_t debounce_ns) {
  if (state.initialized && now_ns < state.last_sample_ns)
    return {Event::InvalidTime, state.stable, false};

  bool candidate_changed = false;
  if (!state.initialized) {
    state.initialized = true;
    state.candidate = asserted;
    state.candidate_since_ns = now_ns;
    state.last_sample_ns = now_ns;
    return {Event::Pending, false, true};
  }

  state.last_sample_ns = now_ns;
  if (asserted != state.candidate) {
    state.candidate = asserted;
    state.candidate_since_ns = now_ns;
    candidate_changed = true;
  }

  if (now_ns - state.candidate_since_ns < debounce_ns ||
      (state.has_stable && state.stable == state.candidate))
    return {Event::Pending, state.stable, candidate_changed};

  const bool baseline = !state.has_stable;
  state.stable = state.candidate;
  state.has_stable = true;
  return {baseline ? Event::Baseline : Event::Transition, state.stable, candidate_changed};
}
} // namespace zizhan_hall
