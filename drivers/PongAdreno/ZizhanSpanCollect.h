/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef ZIZHAN_SPAN_COLLECT_H
#define ZIZHAN_SPAN_COLLECT_H

#include <stddef.h>
#include <stdint.h>

namespace zizhan_adreno_perf {

struct Span {
    uint64_t offset;
    uint64_t physical;
    uint64_t bytes;
};

/*
 * Validate and advance across one IOMemoryDescriptor::getPhysicalSegment() run.
 * When fast_spans is true (default), steps by the whole 4 KiB-aligned contiguous
 * segment returned by IOKit instead of re-walking getPhysicalSegment() every 4 KiB
 * (4352 walks per 17 MiB IOSurface in prof-bb-1).
 */
inline bool step_segment(size_t offset, size_t total_bytes, uint64_t physical, size_t seg_len,
                         uint64_t limit, bool require_contiguous, uint64_t first_physical,
                         bool fast_spans, size_t *out_step)
{
    if (!out_step || offset >= total_bytes || ((offset | total_bytes) & 4095u)) {
        return false;
    }
    const size_t rem = total_bytes - offset;
    const size_t raw = seg_len < rem ? seg_len : rem;
    const size_t step = fast_spans ? (raw & ~size_t(4095u)) : (raw >= 4096u ? 4096u : 0u);
    if (!physical || (physical & 4095u) || step < 4096u || physical >= limit || step > limit - physical) {
        return false;
    }
    if (require_contiguous && offset > 0 && physical != first_physical + offset) {
        return false;
    }
    *out_step = step;
    return true;
}

inline size_t next_span_capacity(size_t current_cap, size_t total_bytes)
{
    const size_t max_pages = total_bytes / 4096u;
    if (max_pages <= 4u) {
        return max_pages ? max_pages : 1u;
    }
    size_t next = current_cap < 32u ? 32u : (current_cap <= max_pages / 4u ? current_cap * 4u : max_pages);
    if (next > max_pages) {
        next = max_pages;
    }
    return next;
}

/*
 * Rate-limit healthy client_healthy() SMMU+GX uncached MMIO polls while keeping
 * poisoned and irq_fatal checks instantaneous on every call.
 */
inline bool should_poll_hw_health(bool poisoned, uint32_t irq_fatal, uint64_t now_us,
                                  uint64_t last_poll_us, uint32_t min_interval_us)
{
    if (poisoned || irq_fatal != 0) {
        return true;
    }
    if (min_interval_us == 0) {
        return true;
    }
    return now_us < last_poll_us || (now_us - last_poll_us) >= (uint64_t)min_interval_us;
}

} // namespace zizhan_adreno_perf

#endif
