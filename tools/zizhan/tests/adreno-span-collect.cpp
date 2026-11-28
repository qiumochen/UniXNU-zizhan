/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#include "../../../drivers/PongAdreno/ZizhanSpanCollect.h"
#include <cassert>
#include <cstdint>
#include <vector>

int main()
{
    using namespace zizhan_adreno_perf;
    const uint64_t limit = UINT64_C(1) << 36;

    /* 1. Contiguous 17 MiB IOSurface (4352 pages) finishes in 1 step instead of 4352 */
    const size_t surf_bytes = 4352u * 4096u; /* 17,825,792 B */
    size_t step = 0;
    assert(!step_segment(0, 2160u * 1916u * 4u, 0x90000000ULL, surf_bytes, limit, false, 0x90000000ULL, true, &step));
    assert(step_segment(0, surf_bytes, 0x90000000ULL, surf_bytes + 8192u, limit, false, 0x90000000ULL, true, &step));
    assert(step == surf_bytes);

    /* Legacy mode steps 4096 B at a time */
    assert(step_segment(0, surf_bytes, 0x90000000ULL, surf_bytes, limit, false, 0x90000000ULL, false, &step));
    assert(step == 4096u);

    /* 2. Scattered 16 KiB pages coalesce adjacent runs and reject unaligned/out-of-range PAs */
    const uint64_t pas[4] = { 0x81000000ULL, 0x81004000ULL, 0x82000000ULL, 0x82004000ULL };
    std::vector<Span> spans;
    size_t off = 0;
    const size_t total = 4u * 16384u;
    for (int i = 0; i < 4; ++i) {
        size_t s = 0;
        assert(step_segment(off, total, pas[i], 16384u, limit, false, pas[0], true, &s));
        assert(s == 16384u);
        if (!spans.empty() && pas[i] == spans.back().physical + spans.back().bytes) {
            spans.back().bytes += s;
        } else {
            spans.push_back({ off, pas[i], s });
        }
        off += s;
    }
    assert(off == total);
    assert(spans.size() == 2u);
    assert(spans[0].bytes == 32768u && spans[1].bytes == 32768u);

    /* Contiguity check rejects a break when contiguous=true */
    assert(!step_segment(16384u, total, 0x82000000ULL, 16384u, limit, true, 0x81000000ULL, true, &step));
    /* Reject zero, unaligned, short, and above-limit segments */
    assert(!step_segment(0, 8192u, 0ULL, 8192u, limit, false, 0ULL, true, &step));
    assert(!step_segment(0, 8192u, 0x81000100ULL, 8192u, limit, false, 0ULL, true, &step));
    assert(!step_segment(0, 8192u, 0x81000000ULL, 2048u, limit, false, 0ULL, true, &step));
    assert(!step_segment(0, 8192u, limit - 4096u, 8192u, limit, false, 0ULL, true, &step));

    /* 3. Capacity progression stays bounded by max_pages */
    assert(next_span_capacity(4u, surf_bytes) == 32u);
    assert(next_span_capacity(32u, surf_bytes) == 128u);
    assert(next_span_capacity(128u, surf_bytes) == 512u);
    assert(next_span_capacity(512u, surf_bytes) == 2048u);
    assert(next_span_capacity(2048u, surf_bytes) == 4352u);
    assert(next_span_capacity(4u, 20480u) == 5u);

    /* 4. Hardware health poll gate: immediate on poison/irq_fatal/legacy, rate-limited otherwise */
    assert(should_poll_hw_health(false, 0u, 1000u, 0u, 500u));
    assert(!should_poll_hw_health(false, 0u, 1200u, 1000u, 500u));
    assert(should_poll_hw_health(false, 0u, 1500u, 1000u, 500u));
    assert(should_poll_hw_health(false, 1u, 1100u, 1000u, 500u));
    assert(should_poll_hw_health(true, 0u, 1100u, 1000u, 500u));
    assert(should_poll_hw_health(false, 0u, 1100u, 1000u, 0u));
    return 0;
}
