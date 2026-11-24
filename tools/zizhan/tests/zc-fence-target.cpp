/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../../../drivers/PongAdreno/ZizhanZcFence.h"
#include <cassert>
#include <cstdint>

int main()
{
    using namespace zizhan_zc_fence;

    /* 1. SwapEnd fence selection: compositor fence isolates backboardd from later client submits */
    assert(select_present_fence(SwapCompositor, true, 500, 503) == 500);
    assert(select_present_fence(SwapCompositor, true, 0, 503) == 503);
    assert(select_present_fence(SwapCompositor, false, 500, 503) == 503);
    assert(select_present_fence(SwapGlobal, true, 500, 503) == 503);
    assert(select_present_fence(EntrySnapshot, true, 500, 503) == 0);
    assert(select_present_fence(LegacyMoving, true, 500, 503) == 0);

    /* 2. Initial target selection at zcFlip entry */
    assert(initial_target(SwapCompositor, 500, 504) == 500);
    assert(initial_target(SwapCompositor, 0, 504) == 504);
    assert(initial_target(SwapGlobal, 501, 504) == 501);
    assert(initial_target(EntrySnapshot, 500, 504) == 504);
    assert(initial_target(LegacyMoving, 500, 504) == 504);

    /* 3. Poll loop target stability: fixed modes never chase newly submitted fences */
    uint32_t target = initial_target(SwapCompositor, 500, 501);
    assert(target == 500);
    assert(needs_wait(target, 499));
    /* Next frame N+1 (502) and foreground app (503) submit while N waits */
    target = poll_target(SwapCompositor, target, 503);
    assert(target == 500);
    /* As soon as 500 retires, N flips immediately even while 503 is still running */
    assert(!needs_wait(target, 500));
    assert(!needs_wait(target, 501));

    /* Legacy mode 0 reproduces the moving-goalpost convoy stall */
    uint32_t legacy = initial_target(LegacyMoving, 500, 500);
    legacy = poll_target(LegacyMoving, legacy, 503);
    assert(legacy == 503);
    assert(needs_wait(legacy, 500));

    /* 4. 32-bit fence wrap-around safety */
    const uint32_t near_wrap = 0xfffffffeu;
    const uint32_t wrapped = 2u;
    assert(needs_wait(wrapped, near_wrap));
    assert(!needs_wait(near_wrap, wrapped));
    assert(!needs_wait(0u, 0u));
    return 0;
}
