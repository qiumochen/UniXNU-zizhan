/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>

/* Use a local origin: the architected counter can include time in fastboot.
 * All arguments are in the same absolute-time units, after conversion. */
static inline uint64_t zizhan_shot_deadline(uint64_t origin, uint64_t delay)
{
    return delay > UINT64_MAX - origin ? UINT64_MAX : origin + delay;
}
