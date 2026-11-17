/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#pragma once
#include <stdint.h>

// Append-only subscriptions: caller serializes writers; release/acquire
// publication permits dispatch in IRQ context without blocking or allocation.
// Callback/context must live until reboot. No unregister or slot reuse.
namespace zizhan_ipcc {
using Callback = void (*)(void *, uint32_t);
enum class Result { Ok, Duplicate, Conflict, Full, Invalid };
struct Slot { uint32_t id; Callback callback; void *context; mutable uint32_t delivered; };
struct Router { Slot slots[8]{}; uint32_t used = 0; };

inline Result subscribe(Router &router, uint32_t id, Callback callback, void *context) {
    if (!callback || id == UINT32_MAX) return Result::Invalid;
    const uint32_t used = __atomic_load_n(&router.used, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < used; ++i) {
        const Slot &slot = router.slots[i];
        if (slot.id != id) continue;
        return slot.callback == callback && slot.context == context ? Result::Duplicate : Result::Conflict;
    }
    if (used == 8) return Result::Full;
    router.slots[used] = {id, callback, context, 0};
    __atomic_store_n(&router.used, used + 1, __ATOMIC_RELEASE);
    return Result::Ok;
}

inline bool dispatch(const Router &router, uint32_t id) {
    const uint32_t used = __atomic_load_n(&router.used, __ATOMIC_ACQUIRE);
    for (uint32_t i = 0; i < used; ++i) {
        const Slot &slot = router.slots[i];
        if (slot.id != id) continue;
        slot.callback(slot.context, id);
        __atomic_fetch_add(&slot.delivered, 1, __ATOMIC_RELAXED);
        return true;
    }
    return false;
}

struct Drain { uint32_t received = 0, delivered = 0, unknown = 0; bool budget_used = false; };
template<typename Read, typename Clear>
Drain drain(const Router &router, Read read, Clear clear) {
    Drain result{};
    for (unsigned i = 0; i < 32; ++i) {
        const uint32_t id = read();
        if (id == UINT32_MAX) return result;
        // Same clear-before-callback order as the existing ADSP IRQ path.
        clear(id);
        ++result.received;
        if (dispatch(router, id)) ++result.delivered;
        else ++result.unknown;
    }
    // Budget consumed is not proof that another event is pending.
    result.budget_used = true;
    return result;
}
} // namespace zizhan_ipcc
