// SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
namespace zizhan_power_request {
// Monotonic saturated reservation, with a finite contention budget.
// Slots 0..31 log observations; 32 logs output exhaustion; later calls skip.
inline uint32_t reserve(volatile uint32_t *counter) {
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        uint32_t old = __atomic_load_n(counter, __ATOMIC_RELAXED);
        if (old >= 33) return 33;
        if (__atomic_compare_exchange_n(counter, &old, old + 1, false,
                                       __ATOMIC_RELAXED, __ATOMIC_RELAXED)) return old;
    }
    return 33;
}
} // namespace zizhan_power_request
