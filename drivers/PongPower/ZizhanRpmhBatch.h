/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_RPMH_BATCH_H
#define ZIZHAN_RPMH_BATCH_H
#include <stdint.h>

// zizhan APSS DRV2 (0x17a20000), saved live DT and existing board owners:
// sleep0..2, active3..4, wake5..6. Only active TCS4 is programmed here.
// The caller holds powerLock for this entire operation. No MMIO on bad args.
namespace zizhan_rpmh {
enum Result { Ok, Argument, Capacity, Busy, Conflict, Uncertain, ClearTimeout, ResponseTimeout, CleanupTimeout };
struct BatchState { bool uncertain = false; };
inline bool arguments(const uint32_t *addresses, const uint32_t *values, unsigned count) {
    if (!addresses || !values || !count || count > 16) return false;
    for (unsigned i = 0; i < count; ++i) {
        if (!addresses[i] || (addresses[i] & 3)) return false;
        for (unsigned j = 0; j < i; ++j) if (addresses[i] == addresses[j]) return false;
    }
    return true;
}

// Window.r/w include the platform's MMIO barriers. now is monotonic ms;
// pause is microseconds. Iteration caps also bound a stalled clock source.
template <typename Window, typename Now, typename Pause>
Result batch(Window &rsc, BatchState &state, const uint32_t *addresses,
             const uint32_t *values, unsigned count, Now now, Pause pause) {
    if (!arguments(addresses, values, count)) return Argument;
    if (state.uncertain) return Uncertain;
    constexpr unsigned b = 0xd00 + 4 * 0x2a0, other = 0xd00 + 3 * 0x2a0;
    constexpr uint32_t trigger = 1u << 24, active = 1u << 16, irq = 1u << 4;
    const uint32_t caps = rsc.r(0xc);
    const unsigned tcs = (caps >> 12) & 63, capacity = (caps >> 27) & 31;
    if (((rsc.r(0) >> 16) & 255) >= 3 || tcs < 8 || capacity < count) return Capacity;
    if (!rsc.r(b + 0x18)) return Busy;
    if (!rsc.r(other + 0x18)) {
        const uint32_t enabled = rsc.r(other + 0x1c);
        const unsigned checked = capacity < 16 ? capacity : 16;
        if (enabled & ~((1u << checked) - 1)) return Conflict;
        for (unsigned i = 0; i < checked; ++i) if (enabled & (1u << i)) {
            const uint32_t address = rsc.r(other + 0x34 + 20 * i);
            for (unsigned j = 0; j < count; ++j) if (addresses[j] == address) return Conflict;
        }
    }
    // Even a pre-trigger clear failure leaves our TCS state uncertain.
    // Refuse all later batch AND legacy requests until reset; do not erase
    // a possibly in-flight payload after a deadline or late completion.
    state.uncertain = true;
    rsc.w(0xd00, rsc.r(0xd00) & ~irq);
    rsc.w(b + 0x14, rsc.r(b + 0x14) & ~(trigger | active));
    rsc.w(b + 0x1c, 0); rsc.w(0xd08, irq);
    const uint64_t clearing = now(); bool cleared = false;
    for (unsigned tries = 0; tries < 1000; ++tries) {
        if (!(rsc.r(0xd04) & irq) && !(rsc.r(b + 0x14) & (trigger | active))) { cleared = true; break; }
        if (now() - clearing >= 10) break;
        pause(10);
    }
    if (!cleared) return ClearTimeout;
    for (unsigned i = 0; i < count; ++i) {
        const unsigned at = b + 20 * i;
        rsc.w(at + 0x30, 0x10108); rsc.w(at + 0x34, addresses[i]); rsc.w(at + 0x38, values[i]);
    }
    const uint32_t mask = (1u << count) - 1;
    rsc.w(b + 0x10, mask); rsc.w(b + 0x1c, mask);
    rsc.w(b + 0x14, active); rsc.w(b + 0x14, active | trigger);
    const uint64_t began = now(); bool busy_seen = false, done = false;
    for (unsigned tries = 0; tries < 10000; ++tries) {
        const bool idle = rsc.r(b + 0x18) != 0;
        busy_seen |= !idle;
        bool complete = true;
        for (unsigned i = 0; i < count; ++i)
            complete &= (rsc.r(b + 0x3c + 20 * i) & 0x10100) == 0x10100;
        if (idle && complete && (busy_seen || (rsc.r(0xd04) & irq))) { done = true; break; }
        if (now() - began >= 100) break;
        pause(10);
    }
    if (!done) return ResponseTimeout;
    rsc.w(b + 0x1c, 0); rsc.w(0xd08, irq);
    rsc.w(b + 0x14, rsc.r(b + 0x14) & ~trigger);
    const uint64_t finishing = now(); bool clean = false;
    for (unsigned tries = 0; tries < 1000; ++tries) {
        if (!rsc.r(b + 0x1c) && !(rsc.r(b + 0x14) & trigger) && !(rsc.r(0xd04) & irq)) { clean = true; break; }
        if (now() - finishing >= 10) break;
        pause(10);
    }
    if (!clean) return CleanupTimeout;
    state.uncertain = false;
    return Ok;
}
} // namespace zizhan_rpmh
// Explicit active-only candidate entry point. The ordinary boot path does
// not call it; callers must first qualify resource identities and baselines.
extern "C" bool pong_rpmh_batch(const uint32_t *, const uint32_t *, unsigned);
#endif
