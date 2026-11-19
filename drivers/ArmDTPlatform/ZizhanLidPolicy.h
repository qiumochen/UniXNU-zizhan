/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* FOLD-01 step 1: inner panel follows the hall lid switch (zizhan_hall_observe=2).
 * A debounced lid transition becomes one short power-key press, the same input a
 * person would give, so iPadOS keeps its own lock/display state in step:
 *  - closed while the inner display is on  -> press (sleep);
 *  - opened while the inner display is off -> press (wake).
 * The boot baseline never presses, a display in transition never presses, and
 * presses are at least ZizhanLidCooldownMs apart. Pure logic, host tested. */
#pragma once
#include <stdint.h>

namespace zizhan_lid {
enum : uint64_t { ZizhanLidCooldownMs = 1500 };
enum class Action : uint8_t { None, PressSleep, PressWake, SkipBusy, SkipCooldown };
/* display: 0 off, 1 on, 2 transitioning (ZizhanDisplayState). */
inline Action decide(bool transition, bool closed, int display, uint64_t now_ms,
                     bool pressed_before, uint64_t last_press_ms) {
  if (!transition) return Action::None;
  if (display == 2) return Action::SkipBusy;
  const bool want_sleep = closed && display == 1;
  const bool want_wake = !closed && display == 0;
  if (!want_sleep && !want_wake) return Action::None;
  if (pressed_before && now_ms - last_press_ms < ZizhanLidCooldownMs) return Action::SkipCooldown;
  return want_sleep ? Action::PressSleep : Action::PressWake;
}
inline const char *name(Action a) {
  switch (a) {
  case Action::PressSleep: return "press-sleep";
  case Action::PressWake: return "press-wake";
  case Action::SkipBusy: return "skip-busy";
  case Action::SkipCooldown: return "skip-cooldown";
  default: return "none";
  }
}
} // namespace zizhan_lid
