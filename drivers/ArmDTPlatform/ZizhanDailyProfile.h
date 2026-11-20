// Daily profile: features that passed unattended device rounds default ON when their boot arg is absent.
// An explicit boot arg always wins; zizhan_daily=0 restores the old all-off defaults.
// Test injectors (zizhan_power_inject, zizhan_hall_simulate, audio) are never part of this profile.
// The includer must declare PE_parse_boot_argn (pexpert/pexpert.h in the kernel, a fake in host tests).
#pragma once
#include <stdint.h>

namespace zizhan_daily {
// Values used when the profile is on and the feature's own boot arg is absent.
constexpr uint32_t kThermalMode = 2, kThermalCpuC = 90, kThermalGpuC = 85;
constexpr uint32_t kDisplayPower = 1, kPowerObserve = 1, kHallObserve = 2, kOuterFold = 1;
// POWER-04 (power-6/7): CPU 2..7 offline while the display is off; needs processor_exit (on by default with patch 0028).
constexpr uint32_t kCpuOffline = 1;

static inline bool profile_on(void) {
  uint32_t value = 1;
  if (PE_parse_boot_argn("zizhan_daily", &value, sizeof value)) {
    return value != 0;
  }
  return true;
}

// Returns true when *out holds a value: either the explicit boot arg or the daily default.
static inline bool arg(const char *name, uint32_t *out, uint32_t daily_value) {
  if (PE_parse_boot_argn(name, out, sizeof *out)) {
    return true;
  }
  if (profile_on()) {
    *out = daily_value;
    return true;
  }
  return false;
}
}  // namespace zizhan_daily
