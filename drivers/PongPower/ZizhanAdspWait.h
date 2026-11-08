// SPDX-License-Identifier: BSD-2-Clause-Patent
// zizhan POWER-02: shared by the kernel wait path and host race replay.
#pragma once
#include <stdint.h>

namespace zizhan_adsp {
// take atomically consumes pending bits. arm must register the current thread
// before the second take, so a doorbell cannot be lost between check and sleep.
// A zero timeout means event-only, never a zero-duration polling timer.
template <typename Take, typename Arm, typename Clear, typename Block>
uint32_t wait(uint32_t timeout_ms, Take take, Arm arm, Clear clear, Block block) {
    uint32_t bits = take();
    if (bits) return bits;
    arm(timeout_ms);
    bits = take();
    if (bits) { clear(); return bits; }
    block();
    return take();
}
inline uint32_t timeout(bool event_mode, bool pending_send) {
    return event_mode ? (pending_send ? 1000u : 0u) : 100u;
}
} // namespace zizhan_adsp
