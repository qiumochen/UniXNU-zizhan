/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef PONG_DSI_LEASE_H
#define PONG_DSI_LEASE_H
#include <stdint.h>
#include <stdbool.h>
enum { PongDsiTiming = 1, PongDsiBacklight = 2 };
static inline bool pong_dsi_lease_try(uint32_t *state, uint32_t owner)
{
    uint32_t empty = 0;
    return (owner == PongDsiTiming || owner == PongDsiBacklight) &&
        __atomic_compare_exchange_n(state, &empty, owner, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static inline bool pong_dsi_lease_release(uint32_t *state, uint32_t owner)
{
    return (owner == PongDsiTiming || owner == PongDsiBacklight) &&
        __atomic_compare_exchange_n(state, &owner, 0u, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED);
}
#ifdef __cplusplus
extern "C" {
#endif
bool pong_dsi_try_acquire(uint32_t owner);
bool pong_dsi_release(uint32_t owner);
#ifdef __cplusplus
}
#endif
#endif
