/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_ZC_FENCE_H
#define ZIZHAN_ZC_FENCE_H

#include <stdint.h>

namespace zizhan_zc_fence {

enum Mode : uint32_t {
    LegacyMoving = 0,     /* legacy: refresh target inside poll loop (convoy control) */
    EntrySnapshot = 1,    /* snapshot global newest fence once at zcFlip entry */
    SwapCompositor = 2,   /* default: snapshot backboardd fence at SwapEnd (present) */
    SwapGlobal = 3,       /* snapshot global newest fence at SwapEnd (present) */
};

inline uint32_t select_present_fence(uint32_t mode, bool is_compositor,
                                     uint32_t compositor_submitted,
                                     uint32_t global_submitted)
{
    if (mode == SwapGlobal) {
        return global_submitted;
    }
    if (mode == SwapCompositor) {
        if (is_compositor && compositor_submitted != 0) {
            return compositor_submitted;
        }
        return global_submitted;
    }
    return 0;
}

inline uint32_t initial_target(uint32_t mode, uint32_t swap_fence, uint32_t entry_submitted)
{
    if ((mode == SwapCompositor || mode == SwapGlobal) && swap_fence != 0) {
        return swap_fence;
    }
    return entry_submitted;
}

inline bool needs_wait(uint32_t target, uint32_t retired)
{
    if (target == 0) {
        return false;
    }
    return static_cast<int32_t>(target - retired) > 0;
}

inline uint32_t poll_target(uint32_t mode, uint32_t current_target, uint32_t latest_submitted)
{
    if (mode == LegacyMoving) {
        return latest_submitted;
    }
    return current_target;
}

} // namespace zizhan_zc_fence

#endif
